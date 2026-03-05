// ─── Tester Agents ────────────────────────────────────────────────────────────

import { spawnSubagent, MODEL_CONTEXT_LIMITS, CONTEXT_BUDGET_RATIO, estimateTokens } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, loadSkill } from "./config.js";
import { loadFileContents, delegateToWorkers } from "./workers.js";
import { parseMetaDecomposition } from "./parsing.js";
import { spawnWithEscalation } from "./escalation.js";

export async function askMetaTester(opts: {
  task: string;
  changedFiles: string[];
  implementationSummary: string;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const systemPrompt = `${loadAgentSystemPrompt()}

---

${loadSkill("test")}

---

## Your Role
You are a META-TESTER. You do NOT write test code yourself. Instead, you DECOMPOSE the testing
task into focused per-file worker subtasks that Haiku-tier workers can execute independently.

For each test file that needs to be created or modified, produce a self-contained worker prompt
that includes:
- Exact test function signatures and names (subsystem_property_being_tested)
- Required #includes and metric extractors to use
- Test infrastructure conventions (DELVE_TEST macro, EXPECT_* assertions)
- Deterministic seeds, map sizes, and expected value ranges

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/test/test_terrain_noise.cpp",
      "action": "CREATE" or "MODIFY",
      "instructions": "Brief description of test coverage",
      "context_files": ["src/game/terrain/noise.h"],
      "worker_prompt": "You are writing C++20 tests for the Delve terrain generator. [Detailed instructions including exact test function signatures, DELVE_TEST usage, EXPECT_* assertions, seed values, map sizes, and expected value ranges. The worker has the current file content but no other context.]"
    }
  ]
}
\`\`\`

## Constraints
- Each subtask targets EXACTLY ONE file.
- worker_prompt must be SELF-CONTAINED — the worker has NO other context.
- Use deterministic seeds (42, 123, etc.).
- Use 256x256 maps for speed.
- Test one property per function.
- Metric extractors must be pure functions — no GPU, no window.
- Include CMakeLists.txt subtask if new test files are added.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const testerConfig = loadAgentConfig("tester");
  const model = testerConfig.model || "anthropic/claude-sonnet-4-6";

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);
  const fileContents = loadFileContents(opts.changedFiles, remainingChars, opts.cwd);

  const prompt = `## Original Task
${opts.task}

## Implementation Summary
${opts.implementationSummary}

## Changed Files
${fileContents}`;

  // Step 1: Sonnet decomposes testing task into per-file worker subtasks
  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt,
    model,
    thinking: testerConfig.thinking || "low",
    tools: testerConfig.tools.length > 0 ? testerConfig.tools : undefined,
    agentName: "tester-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use legacy direct implementation
  if (parsed.subtasks.length === 0) {
    console.error("[meta-tester] Decomposition failed — falling back to direct implementation");
    const fallbackResult = await spawnWithEscalation({
      prompt,
      systemPrompt: systemPrompt.replace("You do NOT write test code yourself. Instead, you DECOMPOSE",
        "Produce test code for the implementation changes. For each test file, decompose"),
      model,
      thinking: testerConfig.thinking || "low",
      tools: testerConfig.tools.length > 0 ? testerConfig.tools : undefined,
      agentName: "tester",
      cwd: opts.cwd,
    });
    return fallbackResult;
  }

  // Step 2: Delegate to Haiku workers in parallel
  console.error(`[meta-tester] Decomposed into ${parsed.subtasks.length} worker subtasks`);
  const result = await delegateToWorkers(parsed, "tester", opts.cwd);

  return result;
}
