// ─── Fixer Agents ─────────────────────────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, PROJECT_ROOT } from "./config.js";
import { askWorker, delegateToWorkers } from "./workers.js";
import { parseMetaDecomposition } from "./parsing.js";
import { readFileSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";

export async function askBuildFixer(opts: {
  buildOutput: string;
  round: number;
  maxRounds: number;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const config = loadAgentConfig("build-fixer");
  const model = config.model || "anthropic/claude-sonnet-4-6";

  // Step 1: Sonnet decomposes build errors into per-file fix tasks
  const decomposerPrompt = `You are a META-BUILD-FIXER for a C++20 CMake project (Delve terrain generator).
You do NOT fix code yourself. Instead, you DECOMPOSE build errors into per-file fix tasks
that Haiku-tier workers can execute independently.

For each file with errors, produce a self-contained worker prompt that includes:
- The exact compiler errors for that file
- What needs to change (missing #include, wrong type, updated signature, etc.)
- Enough context for the worker to produce the complete fixed file

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/broken_file.cpp",
      "action": "MODIFY",
      "instructions": "Fix compilation errors in broken_file.cpp",
      "context_files": ["src/path/to/related.h"],
      "worker_prompt": "You are fixing compilation errors in broken_file.cpp (C++20). Errors: [exact errors]. Fix: [specific instructions: add #include X, change type Y to Z, etc.]. Output the COMPLETE fixed file."
    }
  ]
}
\`\`\`

## Constraints
- One subtask per file with errors.
- worker_prompt must include the EXACT error messages for that file.
- Include specific fix instructions (the worker should not need to reason about the fix).
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `BUILD FAILED (attempt ${opts.round}/${opts.maxRounds}).

Compiler output (tail):
\`\`\`
${opts.buildOutput}
\`\`\`

Decompose these errors into per-file fix tasks.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: "low",
    tools: config.tools.length > 0 ? config.tools : undefined,
    agentName: "build-fixer-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use direct Haiku fix
  if (parsed.subtasks.length === 0) {
    console.error("[meta-build-fixer] Decomposition failed — falling back to direct fix");
    return spawnSubagent({
      prompt,
      systemPrompt: `You are a BUILD FIXER for a C++20 CMake project (Delve terrain generator).
Fix compilation errors. Output COMPLETE file content for each file:
### FILE: <path>
#### ACTION: MODIFY
\`\`\`cpp
[COMPLETE file content with fixes]
\`\`\``,
      model: "anthropic/claude-haiku-4-5",
      thinking: "off",
      tools: config.tools.length > 0 ? config.tools : undefined,
      agentName: "build-fixer",
      cwd: opts.cwd,
    });
  }

  // Step 2: Delegate per-file fixes to Haiku workers in parallel
  console.error(`[meta-build-fixer] Decomposed into ${parsed.subtasks.length} per-file fix tasks`);
  const result = await delegateToWorkers(parsed, "build-fixer", opts.cwd);
  return result;
}

export async function askTestFixer(opts: {
  testOutput: string;
  round: number;
  maxRounds: number;
  isBuildFailure: boolean;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const what = opts.isBuildFailure ? "TEST BUILD" : "TESTS";
  const config = loadAgentConfig("test-fixer");
  const model = config.model || "anthropic/claude-sonnet-4-6";

  const fixStrategy = opts.isBuildFailure
    ? "Fix compilation errors in the test code"
    : "Fix the implementation (not the tests) unless test expectations are clearly wrong";

  // Step 1: Sonnet decomposes test failures into per-file fix tasks
  const decomposerPrompt = `You are a META-TEST-FIXER for a C++20 project (Delve terrain generator).
You do NOT fix code yourself. Instead, you DECOMPOSE test failures into per-file fix tasks
that Haiku-tier workers can execute independently.

Strategy: ${fixStrategy}

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/file.cpp",
      "action": "MODIFY",
      "instructions": "Fix test failure in file.cpp",
      "context_files": ["src/path/to/related.h"],
      "worker_prompt": "You are fixing ${opts.isBuildFailure ? "compilation errors" : "test failures"} in file.cpp (C++20). Errors: [exact errors]. Fix: [specific instructions]. Output the COMPLETE fixed file."
    }
  ]
}
\`\`\`

## Constraints
- One subtask per file that needs fixing.
- worker_prompt must include the EXACT error messages relevant to that file.
- Include specific fix instructions.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `${what} FAILED (attempt ${opts.round}/${opts.maxRounds}).

Output (tail):
\`\`\`
${opts.testOutput}
\`\`\`

Decompose these failures into per-file fix tasks.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: "low",
    tools: config.tools.length > 0 ? config.tools : undefined,
    agentName: "test-fixer-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use direct Haiku fix
  if (parsed.subtasks.length === 0) {
    console.error("[meta-test-fixer] Decomposition failed — falling back to direct fix");
    return spawnSubagent({
      prompt,
      systemPrompt: `You are a TEST FIXER for a C++20 project (Delve terrain generator).
${fixStrategy}. Output COMPLETE file content for each file:
### FILE: <path>
#### ACTION: MODIFY
\`\`\`cpp
[COMPLETE file content with fixes]
\`\`\``,
      model: "anthropic/claude-haiku-4-5",
      thinking: "off",
      tools: config.tools.length > 0 ? config.tools : undefined,
      agentName: "test-fixer",
      cwd: opts.cwd,
    });
  }

  // Step 2: Delegate per-file fixes to Haiku workers in parallel
  console.error(`[meta-test-fixer] Decomposed into ${parsed.subtasks.length} per-file fix tasks`);
  const result = await delegateToWorkers(parsed, "test-fixer", opts.cwd);
  return result;
}

export async function askDiagnoser(opts: {
  task: string;
  testOutput: string;
  recentCommits: string;
  cwd?: string;
  signal?: AbortSignal;
}): Promise<string> {
  const diagnoserConfig = loadAgentConfig("diagnoser");
  const model = diagnoserConfig.model || "anthropic/claude-sonnet-4-6";

  // Step 1: Sonnet decomposes test failures into per-error analysis tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a META-DIAGNOSTICIAN. You do NOT diagnose bugs yourself. Instead, you DECOMPOSE the
test failures into focused per-error analysis tasks that Haiku-tier workers can investigate independently.

For each distinct error or failure, produce a self-contained worker prompt that tells the worker:
- The specific error message or assertion failure
- The file(s) likely involved
- What to look for (wrong logic, missing initialization, type mismatch, etc.)

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/suspected_file.cpp",
      "action": "MODIFY",
      "instructions": "Analyze assertion failure in test_terrain_noise",
      "context_files": ["src/game/terrain/noise.h"],
      "worker_prompt": "You are analyzing a test failure in a C++20 terrain generator. The error is: [exact error message]. The suspected file is [file]. Look at the function [name] and determine: (1) What is the root cause? (2) Which variable or expression is wrong? (3) What is the expected vs actual behavior? Output: ROOT_CAUSE: [1 sentence] | AFFECTED: [file:function] | EVIDENCE: [brief quote from code or error]"
    }
  ]
}
\`\`\`

## Constraints
- One subtask per distinct error/failure.
- Group related assertion failures into one subtask if they share a root cause.
- worker_prompt must include the exact error text.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Task
${opts.task}

## Test Output
\`\`\`
${opts.testOutput}
\`\`\`

## Recent Commits
${opts.recentCommits}

Decompose these test failures into per-error analysis tasks.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: diagnoserConfig.thinking || "low",
    tools: diagnoserConfig.tools.length > 0 ? diagnoserConfig.tools : ["read", "bash"],
    agentName: "diagnoser-meta",
    cwd: opts.cwd,
    signal: opts.signal,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, do direct diagnosis
  if (parsed.subtasks.length === 0) {
    console.error("[meta-diagnoser] Decomposition failed — falling back to direct diagnosis");
    const directPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a BUG DIAGNOSTICIAN. Analyze test failures and identify the root cause.
Do NOT propose fixes — only diagnose.

## Output Format
## Diagnosis

### Root Cause
[1-2 sentences identifying the exact cause]

### Affected Files
- [file paths that need to change]

### Evidence
[relevant error messages, stack traces, or logic errors]`;

    return spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model,
      thinking: diagnoserConfig.thinking || "low",
      tools: diagnoserConfig.tools.length > 0 ? diagnoserConfig.tools : ["read", "bash"],
      agentName: "diagnoser",
      cwd: opts.cwd,
      signal: opts.signal,
    });
  }

  // Step 2: Delegate per-error analyses to Haiku workers in parallel
  console.error(`[meta-diagnoser] Decomposed into ${parsed.subtasks.length} per-error analyses`);
  const basePath = opts.cwd || PROJECT_ROOT;
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      // Load context files for the worker
      const fileContents: Record<string, string> = {};
      const allFiles = [st.file, ...st.context_files].slice(0, 3);
      for (const f of allFiles) {
        const fullPath = f.startsWith("/") ? f : join(basePath, f);
        if (existsSync(fullPath) && statSync(fullPath).isFile()) {
          const content = readFileSync(fullPath, "utf-8");
          fileContents[f] = content.length > 4000 ? content.slice(0, 4000) + "\n... [truncated]" : content;
        }
      }

      const result = await askWorker({
        systemPrompt: `You are a bug analysis worker for a C++20 terrain generator.
Analyze the test failure described below by examining the provided source code.
Output EXACTLY: ROOT_CAUSE: [1 sentence] | AFFECTED: [file:function] | EVIDENCE: [brief explanation]`,
        task: st.worker_prompt,
        fileContents,
        cwd: opts.cwd,
      });
      return `### ${st.file}\n${result}`;
    }),
  );

  // Step 3: Sonnet synthesizes final diagnosis from worker reports
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a DIAGNOSIS SYNTHESIZER. Given per-error analysis results from workers,
produce a unified root cause diagnosis.

## Output Format
## Diagnosis

### Root Cause
[1-2 sentences identifying the exact root cause — consolidate if workers found a shared cause]

### Affected Files
- [file paths that need to change]

### Evidence
[relevant error messages, worker findings, and logic errors]

### Worker Findings
[Include the per-error worker results]

## Constraints
- Synthesize, don't re-analyze.
- If multiple errors share a root cause, identify the common cause.
- Be specific: name the function, line, and variable causing the issue.`;

  const synthesisInput = `## Task
${opts.task}

## Worker Analysis Results
${workerResults.join("\n\n")}

## Original Test Output
\`\`\`
${opts.testOutput}
\`\`\`

Synthesize a unified diagnosis from the worker findings.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: diagnoserConfig.thinking || "low",
    agentName: "diagnoser-synthesizer",
    cwd: opts.cwd,
  });
}
