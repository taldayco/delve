// ─── Implementer Agents ───────────────────────────────────────────────────────

import { spawnSubagent, MODEL_CONTEXT_LIMITS, CONTEXT_BUDGET_RATIO, estimateTokens } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, loadSkill, SUBSYSTEM_AGENT_MAP } from "./config.js";
import { writeState } from "./state.js";
import { loadFileContents, delegateToWorkers } from "./workers.js";
import { buildMetaDecomposerPrompt, buildSubsystemPrompt, parseMetaDecomposition } from "./parsing.js";
import { spawnWithEscalation } from "./escalation.js";
import type { SubsystemAgentOpts } from "./types.js";

async function callSubsystemAgent(
  subsystem: string,
  opts: SubsystemAgentOpts,
): Promise<string> {
  const agentName = SUBSYSTEM_AGENT_MAP[subsystem] || subsystem;
  const agentConfig = loadAgentConfig(agentName);
  const model = agentConfig.model || "anthropic/claude-sonnet-4-6";

  // Build meta-decomposer prompt (Sonnet analyzes + decomposes)
  const systemPrompt = buildMetaDecomposerPrompt(subsystem);

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);

  const fileContents = loadFileContents(opts.files, remainingChars, opts.cwd);

  const prompt = `## Task\n${opts.task}\n\n## Current File Contents\n${fileContents}`;

  // Step 1: Sonnet decomposes task into per-file worker subtasks
  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt,
    model,
    thinking: agentConfig.thinking || "low",
    tools: agentConfig.tools.length > 0 ? agentConfig.tools : undefined,
    agentName: `${subsystem}-meta`,
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use legacy direct implementation
  if (parsed.subtasks.length === 0) {
    console.error(`[meta-${subsystem}] Decomposition failed — falling back to direct implementation`);
    const fallbackPrompt = buildSubsystemPrompt(subsystem);
    const fallbackResult = await spawnWithEscalation({
      prompt,
      systemPrompt: fallbackPrompt,
      model,
      thinking: agentConfig.thinking,
      tools: agentConfig.tools.length > 0 ? agentConfig.tools : undefined,
      expectFileBlocks: true,
      agentName: subsystem,
      cwd: opts.cwd,
    });
    writeState(`${subsystem}_changes.md`, fallbackResult);
    return fallbackResult;
  }

  // Step 2: Delegate to Haiku workers in parallel
  console.error(`[meta-${subsystem}] Decomposed into ${parsed.subtasks.length} worker subtasks`);
  const result = await delegateToWorkers(parsed, subsystem, opts.cwd);

  writeState(`${subsystem}_changes.md`, result);
  return result;
}

export async function askTerrainAgent(opts: SubsystemAgentOpts): Promise<string> {
  return callSubsystemAgent("terrain", opts);
}

export async function askActorAgent(opts: SubsystemAgentOpts): Promise<string> {
  return callSubsystemAgent("actor", opts);
}

export async function askShaderAgent(opts: SubsystemAgentOpts): Promise<string> {
  return callSubsystemAgent("shader", opts);
}

export async function askEngineAgent(opts: SubsystemAgentOpts): Promise<string> {
  return callSubsystemAgent("engine", opts);
}

/** Map subsystem name → agent function */
export function getSubsystemAgent(subsystem: string): (opts: SubsystemAgentOpts) => Promise<string> {
  const agents: Record<string, (opts: SubsystemAgentOpts) => Promise<string>> = {
    terrain: askTerrainAgent,
    actor: askActorAgent,
    shader: askShaderAgent,
    engine: askEngineAgent,
  };
  return agents[subsystem] || askEngineAgent;
}

export async function askMetaImplementer(opts: {
  plan: string;
  task: string;
  files: string[];
  subsystems?: string[];
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const skillNames = opts.subsystems || ["terrain", "engine", "shader", "actor"];
  const skillSections = skillNames
    .map((s) => loadSkill(s))
    .filter(Boolean)
    .map((s) => "---\n\n" + s)
    .join("\n\n");

  const systemPrompt = `${loadAgentSystemPrompt()}

${skillSections}

---

## Your Role
You are a META-IMPLEMENTER. You do NOT write code yourself. Instead, you DECOMPOSE the plan
into focused per-file worker subtasks that Haiku-tier workers can execute independently.

For each file that needs to change, produce a self-contained worker prompt that includes:
- Exact function signatures to add/modify
- Required #includes
- Type definitions the worker needs to know about
- Code conventions to follow
- The complete context the worker needs (it has NO other knowledge)

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/file.cpp",
      "action": "CREATE" or "MODIFY",
      "instructions": "Brief description of what changes",
      "context_files": ["src/path/to/dependency.h"],
      "worker_prompt": "You are editing file.cpp in a C++20 terrain generator using SDL3-GPU. [Detailed, self-contained instructions for the Haiku worker. Include exact code to add, function signatures, types, includes, and expected behavior. The worker has the current file content but no other context.]"
    }
  ]
}
\`\`\`

## Constraints
- Each subtask targets EXACTLY ONE file.
- worker_prompt must be SELF-CONTAINED — the worker has NO other context beyond the file contents.
- worker_prompt MUST include exact filepath and line numbers for every function or code block to add/modify (e.g., "Add function bar after line 87 in src/foo.cpp", "Modify baz at line 23 to..."). Line numbers come from the Current File Contents above.
- Order subtasks by dependency (headers before implementations, declarations before usage).
- Only decompose changes the plan requires — no unnecessary refactoring.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const implementerConfig = loadAgentConfig("implementer");
  const model = implementerConfig.model || "anthropic/claude-sonnet-4-6";

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);
  const fileContents = loadFileContents(opts.files, remainingChars, opts.cwd);

  const prompt = `## Task
${opts.task}

## Plan
${opts.plan}

## Current File Contents
${fileContents}`;

  // Step 1: Sonnet decomposes the plan into per-file worker subtasks
  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt,
    model,
    thinking: implementerConfig.thinking || "low",
    tools: implementerConfig.tools.length > 0 ? implementerConfig.tools : undefined,
    agentName: "implementer-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use legacy direct implementation
  if (parsed.subtasks.length === 0) {
    console.error("[meta-implementer] Decomposition failed — falling back to direct implementation");
    const fallbackResult = await spawnWithEscalation({
      prompt,
      systemPrompt: systemPrompt.replace("You do NOT write code yourself. Instead, you DECOMPOSE",
        "Produce complete file implementations for every subtask in"),
      model,
      thinking: implementerConfig.thinking || "low",
      tools: implementerConfig.tools.length > 0 ? implementerConfig.tools : undefined,
      expectFileBlocks: true,
      agentName: "implementer",
      cwd: opts.cwd,
    });
    writeState("changes.md", fallbackResult);
    return fallbackResult;
  }

  // Step 2: Delegate to Haiku workers in parallel
  console.error(`[meta-implementer] Decomposed into ${parsed.subtasks.length} worker subtasks`);
  const result = await delegateToWorkers(parsed, "implementer", opts.cwd);

  writeState("changes.md", result);
  return result;
}
