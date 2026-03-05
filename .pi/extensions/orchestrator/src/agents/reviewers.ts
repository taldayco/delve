// ─── Reviewer Agent ───────────────────────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, loadSkill } from "./config.js";
import { writeState } from "./state.js";
import { parseMetaDecomposition } from "./parsing.js";

export async function askReviewer(opts: {
  task: string;
  diff: string;
  testResults: string;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const reviewerConfig = loadAgentConfig("reviewer");
  const model = reviewerConfig.model || "anthropic/claude-sonnet-4-6";

  // Step 1: Sonnet decomposes diff into per-file review tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

${loadSkill("review")}

---

## Your Role
You are a META-REVIEWER. You do NOT review code yourself. Instead, you DECOMPOSE the diff
into focused per-file review tasks that Haiku-tier workers can execute independently.

For each changed file, produce a self-contained worker prompt that tells the worker:
- What the file is supposed to do (from the task context)
- Specific things to check (memory safety, hex invariants, O(N^2), includes)
- The diff for ONLY that file

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/file.cpp",
      "action": "MODIFY",
      "instructions": "Review changes to file.cpp for correctness and safety",
      "context_files": [],
      "worker_prompt": "You are reviewing changes to file.cpp in a C++20 terrain generator. The task was: [task context]. Review the following diff for: (1) compilation errors and missing includes, (2) memory safety (no dangling pointers, buffer overflows, GPU resource leaks), (3) correctness (does the logic match the intent?), (4) performance (no O(N^2) on large data). Output EXACTLY: PASS or FAIL: [brief reason with file:line references]."
    }
  ]
}
\`\`\`

## Constraints
- One subtask per changed file.
- worker_prompt must include the relevant diff section.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Task
${opts.task}

## Diff
\`\`\`
${opts.diff}
\`\`\`

## Test Results
${opts.testResults}`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: "low",
    tools: reviewerConfig.tools.length > 0 ? reviewerConfig.tools : undefined,
    agentName: "reviewer-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed (e.g., single-file diff), do direct review
  if (parsed.subtasks.length === 0) {
    console.error("[meta-reviewer] Decomposition failed — falling back to direct review");
    const directPrompt = `${loadAgentSystemPrompt()}

---

${loadSkill("review")}

---

## Your Role
You are a CODE REVIEWER. Return APPROVE or REQUEST_CHANGES. No middle ground.

## Output Format
## Review: [APPROVE / REQUEST_CHANGES]

### Summary
[1-2 sentences]

### Issues (if any)
1. [file:line] [severity] — [description]

### Tests
- [PASS/FAIL] All tests pass
- [YES/NO] New behavior is tested`;

    const result = await spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model,
      thinking: reviewerConfig.thinking || "medium",
      tools: reviewerConfig.tools.length > 0 ? reviewerConfig.tools : undefined,
      agentName: "reviewer",
      cwd: opts.cwd,
      signal: opts.signal,
    });
    writeState("review.md", result);
    return result;
  }

  // Step 2: Delegate per-file reviews to Haiku workers in parallel
  console.error(`[meta-reviewer] Decomposed into ${parsed.subtasks.length} per-file reviews`);
  const workerConfig = loadAgentConfig("worker");
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const result = await spawnSubagent({
        prompt: st.worker_prompt,
        systemPrompt: `You are a code review worker for a C++20 terrain generator (SDL3-GPU).
Review the provided diff for correctness, safety, and compilation issues.
Output EXACTLY one line: PASS or FAIL: [brief reason with file:line references if applicable].`,
        model: workerConfig.model || "anthropic/claude-haiku-4-5",
        thinking: "off",
        agentName: `reviewer-worker-${st.file.split("/").pop()}`,
        cwd: opts.cwd,
      });
      return `### ${st.file}\n${result}`;
    }),
  );

  // Step 3: Sonnet synthesizes final verdict from worker reports
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a REVIEW SYNTHESIZER. Given per-file review results from workers, produce a final verdict.

## Output Format
## Review: [APPROVE / REQUEST_CHANGES]

### Summary
[1-2 sentences]

### Per-File Results
[Include the worker results]

### Issues (if any)
1. [file:line] [severity] — [description]

### Tests
- [PASS/FAIL] All tests pass
- [YES/NO] New behavior is tested

## Decision Rules
- If ANY worker reported FAIL → REQUEST_CHANGES
- If all workers reported PASS and tests pass → APPROVE
- Include all worker-reported issues in the Issues section`;

  const synthesisInput = `## Task
${opts.task}

## Worker Review Results
${workerResults.join("\n\n")}

## Test Results
${opts.testResults}

Synthesize a final review verdict.`;

  const result = await spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: reviewerConfig.thinking || "medium",
    agentName: "reviewer-synthesizer",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  writeState("review.md", result);
  return result;
}
