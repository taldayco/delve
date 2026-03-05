// ─── Worker Agent Delegation ──────────────────────────────────────────────────

import { readFileSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";
import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, PROJECT_ROOT } from "./config.js";
import { writeState } from "./state.js";
import type { MetaDecomposition } from "./types.js";

export function loadFileContents(files: string[], budgetChars?: number, cwd?: string): string {
  const basePath = cwd || PROJECT_ROOT;
  const perFile = budgetChars
    ? Math.floor(budgetChars / Math.max(files.length, 1))
    : 8000;

  const sections: string[] = [];
  for (const f of files) {
    const fullPath = f.startsWith("/") ? f : join(basePath, f);
    if (existsSync(fullPath) && statSync(fullPath).isFile()) {
      const content = readFileSync(fullPath, "utf-8");
      const truncated = content.length > perFile
        ? content.slice(0, perFile) + "\n... [truncated]"
        : content;
      sections.push(`### ${f}\n\`\`\`\n${truncated}\n\`\`\``);
    } else {
      sections.push(`### ${f}\n[file not found]`);
    }
  }
  return sections.join("\n\n");
}

export async function askWorker(opts: {
  systemPrompt: string;
  task: string;
  fileContents?: Record<string, string>;
  maxTokens?: number;
  cwd?: string;
}): Promise<string> {
  let prompt = `## Task\n${opts.task}`;

  if (opts.fileContents && Object.keys(opts.fileContents).length > 0) {
    const fileSection = Object.entries(opts.fileContents)
      .map(([path, content]) => {
        const truncated = content.length > 6000
          ? content.slice(0, 6000) + "\n... [truncated]"
          : content;
        return `### ${path}\n\`\`\`\n${truncated}\n\`\`\``;
      })
      .join("\n\n");
    prompt += `\n\n## File Contents\n${fileSection}`;
  }

  const workerConfig = loadAgentConfig("worker");

  return spawnSubagent({
    prompt,
    systemPrompt: opts.systemPrompt,
    model: workerConfig.model || "anthropic/claude-haiku-4-5",
    thinking: workerConfig.thinking || "off",
    tools: workerConfig.tools.length > 0 ? workerConfig.tools : undefined,
    agentName: "worker",
    cwd: opts.cwd,
  });
}

export async function generateWorkerPrompt(opts: {
  taskType: string;
  context: string;
  constraints: string[];
  cwd?: string;
}): Promise<{
  systemPrompt: string;
  userPrompt: string;
  requiredFiles: string[];
}> {
  const systemPrompt = `You are a PROMPT ENGINEER for a C++20 terrain generator codebase.
Your job is to create a highly focused system prompt and user prompt for a Haiku-tier worker agent.

The worker will receive ONLY the files you specify. It has no other context.

## Output Format (JSON)
\`\`\`json
{
  "system_prompt": "...",
  "user_prompt": "...",
  "required_files": ["path/to/file.h", "path/to/file.cpp"]
}
\`\`\`

## Rules
- System prompt should be under 500 tokens
- Include only the minimal context the worker needs
- Be extremely specific about what to change and how
- List only files the worker actually needs to read
- Constraints from the caller are non-negotiable`;

  const prompt = `## Task Type: ${opts.taskType}

## Context
${opts.context}

## Constraints
${opts.constraints.map((c) => `- ${c}`).join("\n")}`;

  const promptEngConfig = loadAgentConfig("prompt-engineer");

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model: promptEngConfig.model || "anthropic/claude-sonnet-4-6",
    thinking: promptEngConfig.thinking || "off",
    agentName: "prompt-engineer",
    cwd: opts.cwd,
  });

  // Parse JSON from the response
  try {
    const jsonMatch = result.match(/```json\s*([\s\S]*?)```/);
    const json = JSON.parse(jsonMatch ? jsonMatch[1].trim() : result);
    return {
      systemPrompt: json.system_prompt || "",
      userPrompt: json.user_prompt || "",
      requiredFiles: json.required_files || [],
    };
  } catch {
    return {
      systemPrompt: result,
      userPrompt: opts.context,
      requiredFiles: [],
    };
  }
}

/**
 * Delegate a decomposed set of worker subtasks to Haiku workers in parallel.
 * Each worker receives the file content + focused instructions.
 * Returns aggregated FILE blocks from all workers.
 */
export async function delegateToWorkers(
  decomposition: MetaDecomposition,
  subsystem: string,
  cwd?: string,
): Promise<string> {
  const basePath = cwd || PROJECT_ROOT;
  const workerConfig = loadAgentConfig("worker");
  const workerModel = workerConfig.model || "anthropic/claude-haiku-4-5";

  const workerPromises = decomposition.subtasks.map(async (st) => {
    // Load the target file and any context files
    const fileContents: Record<string, string> = {};
    const targetPath = st.file.startsWith("/") ? st.file : join(basePath, st.file);
    if (existsSync(targetPath) && statSync(targetPath).isFile()) {
      fileContents[st.file] = readFileSync(targetPath, "utf-8");
    }
    for (const dep of st.context_files.slice(0, 3)) {
      const depPath = dep.startsWith("/") ? dep : join(basePath, dep);
      if (existsSync(depPath) && statSync(depPath).isFile()) {
        const content = readFileSync(depPath, "utf-8");
        fileContents[dep] = content.length > 4000 ? content.slice(0, 4000) + "\n... [truncated]" : content;
      }
    }

    const workerSystemPrompt = `You are a C++20 code worker for the Delve terrain generator.
You receive a single file to modify and focused instructions.

## Output Format
### FILE: ${st.file}
#### ACTION: ${st.action}
\`\`\`cpp
[COMPLETE file content — the FULL file, not a snippet or diff]
\`\`\`

## Constraints
- Output ONLY the file block above. No preamble, no explanation.
- Include ALL existing code that doesn't need to change.
- Follow existing code conventions exactly.
- Include all necessary #includes.`;

    const firstResult = await askWorker({
      systemPrompt: workerSystemPrompt,
      task: st.worker_prompt,
      fileContents,
      cwd,
    });

    // If worker produced a FILE block, we're done.
    if (/###\s*FILE:/i.test(firstResult)) return firstResult;

    // ── ISOLATION FALLBACK ─────────────────────────────────────────────────
    // Worker failed to produce changes. Implement the new feature in isolation
    // (no project context), validate the logic, then integrate into the project.
    console.error(`[isolation-fallback] Worker produced no FILE block for ${st.file} — retrying in isolation`);

    const isolationWorkerConfig = loadAgentConfig("worker");
    const isolationWorkerModel = isolationWorkerConfig.model || "anthropic/claude-haiku-4-5";

    // Step 1: Implement the feature in isolation (stripped of all project context).
    // The agent gets only the algorithm/API requirements, no file contents.
    const isolationSystemPrompt = `You are a C++20 algorithm specialist.
Implement the requested feature as a self-contained standalone function or class.
Do NOT reference any project files, headers, or types beyond standard C++20.
Output ONLY valid C++20 code in a single fenced code block:
\`\`\`cpp
[isolated implementation]
\`\`\`
No preamble, no explanation.`;

    const isolatedImpl = await spawnSubagent({
      prompt: `Implement the following feature in isolation as standalone C++20 code:\n\n${st.instructions}\n\nRequirements extracted from task:\n${st.worker_prompt}`,
      systemPrompt: isolationSystemPrompt,
      model: isolationWorkerModel,
      thinking: "off",
      agentName: `isolation-${st.file.split("/").pop()}`,
      cwd,
    });

    // Step 2: Integration worker — gets project file context + isolated implementation,
    // integrates it properly into the target file.
    const integrationSystemPrompt = `You are a C++20 integration worker for the Delve terrain generator.
You receive a target file and an isolated implementation of a new feature.
Integrate the isolated implementation into the target file, adapting types and includes as needed.

## Output Format
### FILE: ${st.file}
#### ACTION: ${st.action}
\`\`\`cpp
[COMPLETE file content with the isolated implementation integrated]
\`\`\`

## Constraints
- Output ONLY the file block above. No preamble, no explanation.
- Include ALL existing code that does not need to change.
- Adapt the isolated implementation to use the project's existing types and conventions.
- Include all necessary #includes.`;

    return askWorker({
      systemPrompt: integrationSystemPrompt,
      task: `Integrate this isolated implementation into the project file:\n\n${isolatedImpl}\n\nOriginal task context:\n${st.worker_prompt}`,
      fileContents,
      cwd,
    });
  });

  // suppress unused variable warning
  void workerModel;
  void subsystem;

  const results = await Promise.all(workerPromises);
  return results.join("\n\n");
}
