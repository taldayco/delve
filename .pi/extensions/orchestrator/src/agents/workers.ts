// ─── Worker Agent Delegation ──────────────────────────────────────────────────

import { readFileSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";
import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, PROJECT_ROOT } from "./config.js";
import { writeState } from "./state.js";
import { isVikingAvailable, vikingRead, pathToVikingUri } from "../tools/viking.js";
import { sanitizeJsonOutput } from "./parsing.js";
import type { MetaDecomposition, WorkerSubtask } from "./types.js";

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
    const json = JSON.parse(sanitizeJsonOutput(result));
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

const MAX_WORKER_CONCURRENCY = 4;

/** Run async tasks with a concurrency limit (semaphore pattern). */
async function runWithConcurrency<T>(tasks: (() => Promise<T>)[], limit: number): Promise<T[]> {
  const results: T[] = new Array(tasks.length);
  let next = 0;
  async function worker() {
    while (next < tasks.length) {
      const idx = next++;
      results[idx] = await tasks[idx]();
    }
  }
  await Promise.all(Array.from({ length: Math.min(limit, tasks.length) }, () => worker()));
  return results;
}

// ─── Cross-Worker Synchronization ─────────────────────────────────────────────

interface ConflictGroup {
  safe: WorkerSubtask[];
  contested: WorkerSubtask[];  // topologically sorted: if Y reads X's file, X comes first
}

/**
 * Detect conflicts between worker subtasks and split into safe (parallel)
 * and contested (serial, topologically sorted) groups.
 */
function detectWorkerConflicts(subtasks: WorkerSubtask[]): ConflictGroup {
  const targetSet = new Set(subtasks.map((st) => st.file));
  const contestedSet = new Set<number>();

  // Build adjacency: edge X -> Y means Y's context_files includes X's file
  const adj = new Map<number, number[]>();  // X -> [Y, ...]
  const inDegree = new Map<number, number>();

  for (let i = 0; i < subtasks.length; i++) {
    adj.set(i, []);
    inDegree.set(i, 0);
  }

  for (let j = 0; j < subtasks.length; j++) {
    for (let i = 0; i < subtasks.length; i++) {
      if (i === j) continue;
      // If subtask j reads subtask i's output file
      if (subtasks[j].context_files.includes(subtasks[i].file)) {
        contestedSet.add(i);
        contestedSet.add(j);
        adj.get(i)!.push(j);
        inDegree.set(j, (inDegree.get(j) || 0) + 1);
      }
    }
  }

  const safe = subtasks.filter((_, idx) => !contestedSet.has(idx));
  const contestedIndices = Array.from(contestedSet);

  // Topological sort (Kahn's algorithm) on contested indices only
  const subInDegree = new Map<number, number>();
  const subAdj = new Map<number, number[]>();
  for (const idx of contestedIndices) {
    subInDegree.set(idx, 0);
    subAdj.set(idx, []);
  }
  for (const idx of contestedIndices) {
    for (const dep of adj.get(idx) || []) {
      if (contestedSet.has(dep)) {
        subAdj.get(idx)!.push(dep);
        subInDegree.set(dep, (subInDegree.get(dep) || 0) + 1);
      }
    }
  }

  const queue: number[] = [];
  for (const idx of contestedIndices) {
    if (subInDegree.get(idx) === 0) queue.push(idx);
  }

  const sorted: WorkerSubtask[] = [];
  while (queue.length > 0) {
    const curr = queue.shift()!;
    sorted.push(subtasks[curr]);
    for (const next of subAdj.get(curr) || []) {
      const newDeg = subInDegree.get(next)! - 1;
      subInDegree.set(next, newDeg);
      if (newDeg === 0) queue.push(next);
    }
  }

  // Cycle detection: if sorted is shorter than contested, there's a cycle
  if (sorted.length < contestedIndices.length) {
    console.error("[worker-sync] Cycle detected in worker dependencies — falling back to original order");
    const remaining = contestedIndices
      .filter((idx) => !sorted.some((st) => st === subtasks[idx]))
      .map((idx) => subtasks[idx]);
    sorted.push(...remaining);
  }

  return { safe, contested: sorted };
}

/**
 * Re-invoke Sonnet to regenerate a worker prompt from current disk state.
 * Used for contested workers whose upstream dependencies have already written.
 */
async function regenerateWorkerPrompt(
  st: WorkerSubtask,
  cwd?: string,
): Promise<WorkerSubtask> {
  const basePath = cwd || PROJECT_ROOT;

  // Read current disk state of target + context files
  const fileStates: string[] = [];
  const allFiles = [st.file, ...st.context_files].slice(0, 5);
  for (const f of allFiles) {
    const fullPath = f.startsWith("/") ? f : join(basePath, f);
    if (existsSync(fullPath) && statSync(fullPath).isFile()) {
      const content = readFileSync(fullPath, "utf-8");
      const truncated = content.length > 6000 ? content.slice(0, 6000) + "\n... [truncated]" : content;
      fileStates.push(`### ${f}\n\`\`\`\n${truncated}\n\`\`\``);
    }
  }

  try {
    const result = await spawnSubagent({
      prompt: `A previous worker has modified files that this worker depends on.
Regenerate the worker_prompt to reflect the CURRENT state of the files on disk.

## Original Instructions
${st.instructions}

## Original Worker Prompt
${st.worker_prompt}

## Current File Contents (after upstream changes)
${fileStates.join("\n\n")}

Output ONLY a JSON object with a single "worker_prompt" field containing the updated prompt:
\`\`\`json
{"worker_prompt": "..."}
\`\`\``,
      systemPrompt: `You are a prompt regeneration agent. Given original worker instructions and updated file contents,
produce an updated worker_prompt that reflects the current state of the codebase.
The updated prompt must be self-contained and include all context the worker needs.
Output ONLY valid JSON with a "worker_prompt" field.`,
      model: "anthropic/claude-sonnet-4-6",
      thinking: "low",
      agentName: "worker-reprompt",
      cwd,
    });

    const json = JSON.parse(sanitizeJsonOutput(result));
    if (json.worker_prompt && typeof json.worker_prompt === "string") {
      return { ...st, worker_prompt: json.worker_prompt };
    }
    return st;
  } catch {
    console.error(`[worker-reprompt] Failed to regenerate prompt for ${st.file} — using original`);
    return st;
  }
}

/**
 * Dispatch a batch of subtasks, handling cross-worker conflicts.
 * Safe tasks run in parallel; contested tasks run serially in topological order
 * with prompt regeneration between each.
 */
async function dispatchBatch(
  batch: WorkerSubtask[],
  makeWorkerFn: (st: WorkerSubtask) => () => Promise<string>,
  cwd?: string,
): Promise<string[]> {
  if (batch.length === 0) return [];

  const { safe, contested } = detectWorkerConflicts(batch);
  const results: string[] = [];

  // Safe group: parallel execution
  if (safe.length > 0) {
    const safeResults = await runWithConcurrency(safe.map(makeWorkerFn), MAX_WORKER_CONCURRENCY);
    results.push(...safeResults);
  }

  // Contested group: serial execution in topological order
  for (let i = 0; i < contested.length; i++) {
    let st = contested[i];
    // Regenerate prompt for all contested workers except the first (no upstream changes yet)
    if (i > 0) {
      st = await regenerateWorkerPrompt(st, cwd);
    }
    const result = await makeWorkerFn(st)();
    results.push(result);
  }

  return results;
}

/**
 * Delegate a decomposed set of worker subtasks to Haiku workers with concurrency cap.
 * Headers (.h, .glsl) run first, then implementations (.cpp, .comp.glsl) — both
 * capped at MAX_WORKER_CONCURRENCY to avoid API rate limits.
 * Cross-worker conflicts are detected and handled via serial execution with prompt regeneration.
 */
export async function delegateToWorkers(
  decomposition: MetaDecomposition,
  subsystem: string,
  cwd?: string,
): Promise<string> {
  const basePath = cwd || PROJECT_ROOT;
  const workerConfig = loadAgentConfig("worker");
  const workerModel = workerConfig.model || "anthropic/claude-haiku-4-5";

  const isHeader = (f: string) => /\.(h|hpp|glsl)$/i.test(f) && !/\.(comp\.glsl)$/i.test(f);
  const headerTasks = decomposition.subtasks.filter((st) => isHeader(st.file));
  const implTasks = decomposition.subtasks.filter((st) => !isHeader(st.file));

  const makeWorkerFn = (st: typeof decomposition.subtasks[0]) => async () => {
    // Load the target file (always direct read — need exact current contents)
    const fileContents: Record<string, string> = {};
    const targetPath = st.file.startsWith("/") ? st.file : join(basePath, st.file);
    if (existsSync(targetPath) && statSync(targetPath).isFile()) {
      fileContents[st.file] = readFileSync(targetPath, "utf-8");
    }

    // Load context files: Viking direct read with file-read fallback
    const useViking = await isVikingAvailable();
    for (const dep of st.context_files.slice(0, 3)) {
      if (useViking) {
        // Try Viking direct read via mapped URI — returns full indexed content
        const uri = pathToVikingUri(dep);
        if (uri) {
          const content = await vikingRead(uri);
          if (content) {
            fileContents[dep] = content.length > 4000 ? content.slice(0, 4000) + "\n... [truncated]" : content;
            continue;
          }
        }
      }
      // Fallback: direct file read
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
  };

  // suppress unused variable warning
  void workerModel;
  void subsystem;

  // Run headers first (dependencies), then implementations
  // Cross-worker conflicts are detected and handled within each batch
  const headerResults = await dispatchBatch(headerTasks, makeWorkerFn, cwd);
  const implResults = await dispatchBatch(implTasks, makeWorkerFn, cwd);
  return [...headerResults, ...implResults].join("\n\n");
}
