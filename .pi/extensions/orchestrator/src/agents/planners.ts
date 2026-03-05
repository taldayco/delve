// ─── Planner Agents ───────────────────────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, loadSkill } from "./config.js";
import { writeState } from "./state.js";

/**
 * Single-subsystem planner. Plans subtasks for one subsystem only.
 * Used by askParallelPlanner to fan out planning across subsystems.
 */
async function askSubsystemPlanner(opts: {
  task: string;
  subsystem: string;
  codebaseContext: string;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const systemPrompt = `${loadAgentSystemPrompt()}

---

${loadSkill("plan")}

---

## Your Role
You are a SUBSYSTEM PLANNER for the **${opts.subsystem}** subsystem.
Plan ONLY the subtasks that belong to the **${opts.subsystem}** subsystem.

## Output Format
Return markdown. Each subtask MUST have the [${opts.subsystem}] tag:

## Subtask 1 [${opts.subsystem}]
- Files: ...
- Changes: ...
- Acceptance criteria: ...

## Constraints
- Only plan changes for the **${opts.subsystem}** subsystem.
- 1-4 subtasks maximum.
- Be specific. State exactly what changes, not vague descriptions.
- Every subtask MUST have the [${opts.subsystem}] tag.
- Only plan changes to files in the codebase context.`;

  const prompt = `## Task
${opts.task}

## Codebase Context (${opts.subsystem})
${opts.codebaseContext}`;

  const plannerConfig = loadAgentConfig("planner");

  return spawnSubagent({
    prompt,
    systemPrompt,
    model: plannerConfig.model || "anthropic/claude-sonnet-4-6",
    thinking: plannerConfig.thinking || "medium",
    tools: plannerConfig.tools.length > 0 ? plannerConfig.tools : undefined,
    agentName: `planner-${opts.subsystem}`,
    cwd: opts.cwd,
    signal: opts.signal,
  });
}

/**
 * Parallel planner: spawns one planner per subsystem concurrently,
 * then merges results into a single ordered plan.
 *
 * @param subsystemContexts - Map of canonical subsystem name → scoped codebase context
 */
export async function askParallelPlanner(opts: {
  task: string;
  subsystemContexts: Record<string, string>;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const subsystems = Object.keys(opts.subsystemContexts);

  // Fan out: spawn one planner per subsystem in parallel
  const planPromises = subsystems.map((subsystem) =>
    askSubsystemPlanner({
      task: opts.task,
      subsystem,
      codebaseContext: opts.subsystemContexts[subsystem],
      cwd: opts.cwd,
      signal: opts.signal,
    })
  );

  const subPlans = await Promise.all(planPromises);

  // Merge: concatenate per-subsystem plans and renumber subtasks sequentially
  let subtaskIndex = 1;
  const mergedSections: string[] = [];

  for (let i = 0; i < subsystems.length; i++) {
    const plan = subPlans[i];
    // Renumber subtask headers to maintain sequential ordering across subsystems
    const renumbered = plan.replace(
      /^##\s+Subtask\s+\d+/gm,
      () => `## Subtask ${subtaskIndex++}`
    );
    mergedSections.push(renumbered);
  }

  // Append test subtask placeholder if none exists
  const merged = mergedSections.join("\n\n");
  const hasTestSubtask = /\[engine\].*(?:test|acceptance)/i.test(merged);
  const finalPlan = hasTestSubtask
    ? merged
    : `${merged}\n\n## Subtask ${subtaskIndex} [engine]\n- Files: src/test/\n- Changes: Add or update tests for the above changes\n- Acceptance criteria: All tests pass`;

  writeState("plan.md", finalPlan);
  return finalPlan;
}

/**
 * Legacy single-agent planner. Still used for format-correction retries
 * and when called via the ask_meta_planner tool directly.
 */
export async function askMetaPlanner(opts: {
  task: string;
  codebaseContext: string;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const systemPrompt = `${loadAgentSystemPrompt()}

---

${loadSkill("plan")}

---

## Your Role
You are a META-PLANNER. Decompose the task into ordered subtasks. Tag each subtask with its subsystem.

## Output Format
Return markdown. Each subtask MUST have a subsystem tag in brackets:

## Subtask 1 [terrain]
- Files: ...
- Changes: ...
- Acceptance criteria: ...

## Subtask 2 [actor]
- Files: ...
- Changes: ...
- Acceptance criteria: ...

Valid subsystem tags: terrain, actor, shader, engine.

End with a test subtask tagged [engine].

## Constraints
- Only plan changes to files in the codebase context.
- 2-8 subtasks maximum.
- Be specific. State exactly what changes, not vague descriptions.
- Every subtask MUST have exactly one subsystem tag.`;

  const prompt = `## Task
${opts.task}

## Codebase Context
${opts.codebaseContext}`;

  const plannerConfig = loadAgentConfig("planner");

  const plan = await spawnSubagent({
    prompt,
    systemPrompt,
    model: plannerConfig.model || "anthropic/claude-sonnet-4-6",
    thinking: plannerConfig.thinking || "medium",
    tools: plannerConfig.tools.length > 0 ? plannerConfig.tools : undefined,
    agentName: "planner",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  writeState("plan.md", plan);
  return plan;
}
