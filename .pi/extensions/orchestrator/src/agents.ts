import { ask, type ModelTier } from "./models.js";
import { readFileSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";

// ─── Skill & Rule Loader ───────────────────────────────────────────────────

const PROJECT_ROOT = process.cwd();
const SKILLS_DIR = join(PROJECT_ROOT, ".pi/skills");
const RULES_DIR = join(PROJECT_ROOT, ".pi/rules");

function loadFile(path: string): string {
  try {
    return readFileSync(path, "utf-8");
  } catch {
    return "";
  }
}

function loadSkill(name: string): string {
  // Skills live in subdirectories: .pi/skills/<name>/<name>.md
  return loadFile(join(SKILLS_DIR, name, `${name}.md`));
}

function loadRule(name: string): string {
  return loadFile(join(RULES_DIR, `${name}.md`));
}

function loadSystemPrompt(): string {
  return loadFile(join(PROJECT_ROOT, ".pi/SYSTEM.md"));
}

// ─── State File I/O ────────────────────────────────────────────────────────

const STATE_DIR = join(PROJECT_ROOT, ".pi/state");

export function writeState(filename: string, content: string): void {
  const { mkdirSync, writeFileSync } = require("node:fs");
  mkdirSync(STATE_DIR, { recursive: true });
  writeFileSync(join(STATE_DIR, filename), content, "utf-8");
}

export function readState(filename: string): string {
  const path = join(STATE_DIR, filename);
  return existsSync(path) ? readFileSync(path, "utf-8") : "";
}

// ─── File Content Loader ───────────────────────────────────────────────────

function loadFileContents(files: string[]): string {
  const sections: string[] = [];
  for (const f of files) {
    const fullPath = f.startsWith("/") ? f : join(PROJECT_ROOT, f);
    if (existsSync(fullPath) && statSync(fullPath).isFile()) {
      const content = readFileSync(fullPath, "utf-8");
      // Truncate very large files to keep token count manageable
      const truncated = content.length > 8000
        ? content.slice(0, 8000) + "\n... [truncated]"
        : content;
      sections.push(`### ${f}\n\`\`\`\n${truncated}\n\`\`\``);
    } else {
      sections.push(`### ${f}\n[file not found]`);
    }
  }
  return sections.join("\n\n");
}

// ─── Agent Tool: ask_meta_planner (Sonnet) ─────────────────────────────────

export async function askMetaPlanner(opts: {
  task: string;
  codebaseContext: string;
}): Promise<string> {
  const systemPrompt = `${loadSystemPrompt()}

---

${loadSkill("plan")}

---

## Your Role
You are a META-PLANNER. You decompose a task into ordered subtasks with file paths and acceptance criteria. You identify which subsystems are affected and which files need changes.

## Output Format
Return a structured plan in markdown:
- Numbered subtasks, each with: files, changes, acceptance criteria, dependencies
- End with a test subtask
- Keep the plan concise — the orchestrator will pass subtasks to implementer agents

## Constraints
- Only plan changes to files mentioned in the codebase context
- Keep subtask count reasonable (2-8 subtasks)
- Be specific about what changes, not vague`;

  const userPrompt = `## Task
${opts.task}

## Codebase Context
${opts.codebaseContext}`;

  const plan = await ask({
    system: systemPrompt,
    user: userPrompt,
    tier: "sonnet",
    maxTokens: 4096,
  });

  writeState("plan.md", plan);
  return plan;
}

// ─── Agent Tool: ask_meta_implementer (Sonnet) ─────────────────────────────

export async function askMetaImplementer(opts: {
  plan: string;
  task: string;
  files: string[];
}): Promise<string> {
  // The meta-implementer generates scoped worker prompts per subtask,
  // then calls ask_worker (Haiku) for each one. This is "agents that build agents."

  const fileContents = loadFileContents(opts.files);

  const systemPrompt = `${loadSystemPrompt()}

---

${loadSkill("terrain")}

---

${loadSkill("engine")}

---

${loadSkill("shader")}

---

${loadSkill("actor")}

---

## Your Role
You are a META-IMPLEMENTER. Given a plan and the current file contents, you produce a complete implementation.

For each subtask in the plan:
1. Determine exactly what changes are needed
2. Output the changes as complete file edits

## Output Format
For each file change, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[complete new file content, or for MODIFY: the full updated file content]
\`\`\`

## Constraints
- Follow existing code conventions exactly
- Only change what the plan requires — no unnecessary refactoring
- Include all necessary #includes
- Ensure hex coordinate invariants are maintained
- Ensure MapData vectors are properly sized`;

  const userPrompt = `## Task
${opts.task}

## Plan
${opts.plan}

## Current File Contents
${fileContents}`;

  const result = await ask({
    system: systemPrompt,
    user: userPrompt,
    tier: "sonnet",
    maxTokens: 8192,
  });

  writeState("changes.md", result);
  return result;
}

// ─── Agent Tool: ask_meta_tester (Sonnet) ───────────────────────────────────

export async function askMetaTester(opts: {
  task: string;
  changedFiles: string[];
  implementationSummary: string;
}): Promise<string> {
  const fileContents = loadFileContents(opts.changedFiles);

  const systemPrompt = `${loadSystemPrompt()}

---

${loadSkill("test")}

---

## Your Role
You are a META-TESTER. Given the task and implementation changes, you produce test code.

## Output Format
For each test file, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[complete file content]
\`\`\`

If CMakeLists.txt needs updating to register new test files, include that too.

## Constraints
- Use deterministic seeds (42, 123, etc.)
- Use smaller maps (256x256) for speed
- Test one property per test function
- Name tests: subsystem_property_being_tested
- Metric extractors must be pure functions — no GPU, no window
- Use DELVE_TEST macro and EXPECT_* assertions`;

  const userPrompt = `## Original Task
${opts.task}

## Implementation Summary
${opts.implementationSummary}

## Changed Files
${fileContents}`;

  const result = await ask({
    system: systemPrompt,
    user: userPrompt,
    tier: "sonnet",
    maxTokens: 4096,
  });

  return result;
}

// ─── Agent Tool: ask_reviewer (Sonnet) ──────────────────────────────────────

export async function askReviewer(opts: {
  task: string;
  diff: string;
  testResults: string;
}): Promise<string> {
  const systemPrompt = `${loadSystemPrompt()}

---

${loadSkill("review")}

---

## Your Role
You are a CODE REVIEWER. Evaluate the diff against the review checklist.

## Output Format
\`\`\`
## Review: [APPROVE / REQUEST_CHANGES]

### Summary
[1-2 sentence summary]

### Issues (if any)
1. [file:line] [severity] — [description]
2. ...

### Tests
- [PASS/FAIL] All tests pass
- [YES/NO] New behavior is tested
\`\`\`

## Constraints
- Be strict on memory safety and GPU resource leaks
- Be strict on hex coordinate invariants
- Flag O(N^2) on large data
- Don't flag style nits unless they break conventions`;

  const userPrompt = `## Task
${opts.task}

## Diff
\`\`\`
${opts.diff}
\`\`\`

## Test Results
${opts.testResults}`;

  const result = await ask({
    system: systemPrompt,
    user: userPrompt,
    tier: "sonnet",
    maxTokens: 2048,
  });

  writeState("review.md", result);
  return result;
}

// ─── Agent Tool: ask_worker (Haiku) ─────────────────────────────────────────

export async function askWorker(opts: {
  systemPrompt: string;
  task: string;
  fileContents?: Record<string, string>;
  maxTokens?: number;
}): Promise<string> {
  let userPrompt = `## Task\n${opts.task}`;

  if (opts.fileContents && Object.keys(opts.fileContents).length > 0) {
    const fileSection = Object.entries(opts.fileContents)
      .map(([path, content]) => {
        const truncated = content.length > 6000
          ? content.slice(0, 6000) + "\n... [truncated]"
          : content;
        return `### ${path}\n\`\`\`\n${truncated}\n\`\`\``;
      })
      .join("\n\n");
    userPrompt += `\n\n## File Contents\n${fileSection}`;
  }

  return ask({
    system: opts.systemPrompt,
    user: userPrompt,
    tier: "haiku",
    maxTokens: opts.maxTokens ?? 4096,
  });
}

// ─── Agent Tool: generate_worker_prompt (Sonnet) ────────────────────────────

export async function generateWorkerPrompt(opts: {
  taskType: string;
  context: string;
  constraints: string[];
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

  const userPrompt = `## Task Type: ${opts.taskType}

## Context
${opts.context}

## Constraints
${opts.constraints.map((c) => `- ${c}`).join("\n")}`;

  const result = await ask({
    system: systemPrompt,
    user: userPrompt,
    tier: "sonnet",
    maxTokens: 2048,
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
    // Fallback: return the raw result as the system prompt
    return {
      systemPrompt: result,
      userPrompt: opts.context,
      requiredFiles: [],
    };
  }
}

// ─── Agent Tool: ask_build_fixer (Haiku) ────────────────────────────────────

export async function askBuildFixer(opts: {
  buildOutput: string;
  round: number;
  maxRounds: number;
}): Promise<string> {
  const systemPrompt = `You are a BUILD FIXER for a C++20 CMake project (Delve terrain generator).
You receive compiler error output and must output the exact file changes needed to fix the errors.

## Output Format
For each file to fix:

### FILE: <path>
#### CHANGE: Replace lines N-M with:
\`\`\`cpp
[fixed code]
\`\`\`

## Constraints
- Fix ONLY the compilation errors shown — don't refactor
- If a header is missing, add the #include
- If a type is wrong, fix the type
- If a function signature changed, update callers
- Be precise about line numbers and context`;

  return ask({
    system: systemPrompt,
    user: `BUILD FAILED (attempt ${opts.round}/${opts.maxRounds}).

Compiler output (tail):
\`\`\`
${opts.buildOutput}
\`\`\`

Fix the compilation errors.`,
    tier: "haiku",
    maxTokens: 4096,
  });
}

// ─── Agent Tool: ask_test_fixer (Haiku) ──────────────────────────────────────

export async function askTestFixer(opts: {
  testOutput: string;
  round: number;
  maxRounds: number;
  isBuildFailure: boolean;
}): Promise<string> {
  const what = opts.isBuildFailure ? "TEST BUILD" : "TESTS";

  const systemPrompt = `You are a TEST FIXER for a C++20 project (Delve terrain generator).
You receive ${opts.isBuildFailure ? "test compilation" : "test execution"} output and must output exact file changes.

## Output Format
For each file to fix:

### FILE: <path>
#### CHANGE: Replace lines N-M with:
\`\`\`cpp
[fixed code]
\`\`\`

## Constraints
${opts.isBuildFailure
    ? "- Fix compilation errors in test code"
    : "- Fix the implementation, not the tests, unless test expectations are clearly wrong"
  }
- Be precise about line numbers and context
- Don't refactor — minimal fixes only`;

  return ask({
    system: systemPrompt,
    user: `${what} FAILED (attempt ${opts.round}/${opts.maxRounds}).

Output (tail):
\`\`\`
${opts.testOutput}
\`\`\`

Fix the errors.`,
    tier: "haiku",
    maxTokens: 4096,
  });
}
