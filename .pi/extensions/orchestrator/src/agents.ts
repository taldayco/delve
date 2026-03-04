import { spawn } from "node:child_process";
import { readFileSync, existsSync, statSync, mkdirSync, writeFileSync } from "node:fs";
import * as fs from "node:fs";
import * as os from "node:os";
import { join } from "node:path";

// ─── Skill & Rule Loader ───────────────────────────────────────────────────

const PROJECT_ROOT = process.cwd();
const SKILLS_DIR = join(PROJECT_ROOT, ".pi/skills");
const RULES_DIR = join(PROJECT_ROOT, ".pi/rules");
const AGENTS_DIR = join(PROJECT_ROOT, ".pi/agents");

function loadFile(path: string): string {
  try {
    return readFileSync(path, "utf-8");
  } catch {
    return "";
  }
}

function loadSkill(name: string): string {
  return loadFile(join(SKILLS_DIR, name, `${name}.md`));
}

function loadRule(name: string): string {
  return loadFile(join(RULES_DIR, `${name}.md`));
}

function loadSystemPrompt(): string {
  return loadFile(join(PROJECT_ROOT, ".pi/SYSTEM.md"));
}

// ─── Agent Config Loader ──────────────────────────────────────────────────

interface AgentConfig {
  name: string;
  tools: string[];
  model: string;
}

const agentConfigCache = new Map<string, AgentConfig>();

/**
 * Parse YAML frontmatter from .pi/agents/<name>.md to extract tool restrictions.
 * Returns tool list and model from the agent definition file.
 */
function loadAgentConfig(agentName: string): AgentConfig {
  const cached = agentConfigCache.get(agentName);
  if (cached) return cached;

  const defaults: AgentConfig = { name: agentName, tools: [], model: "sonnet" };
  const filePath = join(AGENTS_DIR, `${agentName}.md`);
  const content = loadFile(filePath);
  if (!content) return defaults;

  // Parse YAML frontmatter between --- delimiters
  const fmMatch = content.match(/^---\n([\s\S]*?)\n---/);
  if (!fmMatch) return defaults;

  const frontmatter = fmMatch[1];
  const config: AgentConfig = { ...defaults };

  const toolsMatch = frontmatter.match(/^tools:\s*(.+)$/m);
  if (toolsMatch) {
    config.tools = toolsMatch[1].split(",").map((t) => t.trim()).filter(Boolean);
  }

  const modelMatch = frontmatter.match(/^model:\s*(.+)$/m);
  if (modelMatch) {
    config.model = modelMatch[1].trim();
  }

  agentConfigCache.set(agentName, config);
  return config;
}

/**
 * Map canonical subsystem names to their agent config file names.
 */
const SUBSYSTEM_AGENT_MAP: Record<string, string> = {
  terrain: "terrain",
  actor: "actor",
  shader: "shader",
  engine: "engine",
};

// ─── State File I/O ────────────────────────────────────────────────────────

const STATE_DIR = join(PROJECT_ROOT, ".pi/state");

export function writeState(filename: string, content: string): void {
  mkdirSync(STATE_DIR, { recursive: true });
  writeFileSync(join(STATE_DIR, filename), content, "utf-8");
}

export function readState(filename: string): string {
  const path = join(STATE_DIR, filename);
  return existsSync(path) ? readFileSync(path, "utf-8") : "";
}

// ─── File Content Loader ───────────────────────────────────────────────────

function loadFileContents(files: string[], budgetChars?: number): string {
  const perFile = budgetChars
    ? Math.floor(budgetChars / Math.max(files.length, 1))
    : 8000;

  const sections: string[] = [];
  for (const f of files) {
    const fullPath = f.startsWith("/") ? f : join(PROJECT_ROOT, f);
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

// ─── Silent Shell Execution ────────────────────────────────────────────────

export function silentShell(
  cmd: string,
  tailLines = 20,
  cwd?: string,
): { ok: boolean; summary: string; full: string } {
  const { execSync } = require("node:child_process");
  try {
    const stdout: string = execSync(cmd, {
      cwd: cwd || process.cwd(),
      encoding: "utf-8",
      timeout: 300_000,
      maxBuffer: 10 * 1024 * 1024,
      stdio: ["pipe", "pipe", "pipe"],
    });
    const lines = (stdout || "").split("\n");
    return {
      ok: true,
      summary: lines.slice(-tailLines).join("\n"),
      full: stdout || "",
    };
  } catch (e: any) {
    const output = ((e.stdout || "") + "\n" + (e.stderr || e.message || "")).toString();
    const lines = output.split("\n");
    return {
      ok: false,
      summary: lines.slice(-tailLines).join("\n"),
      full: output,
    };
  }
}

// ─── Context Budget Tracking ──────────────────────────────────────────────

// Approximate tokens as chars/4 (rough but practical).
// Model context windows (conservative estimates for usable input):
const MODEL_CONTEXT_LIMITS: Record<string, number> = {
  opus: 200_000,
  sonnet: 200_000,
  haiku: 200_000,
};

// Target: no agent should use more than 40% of its context window
const CONTEXT_BUDGET_RATIO = 0.4;

/**
 * Estimate token count from a string (rough: ~4 chars per token for English/code).
 */
function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4);
}

/**
 * Check if the combined prompt fits within the context budget.
 * If over budget, truncate the user prompt (preserving the system prompt).
 * Returns the (possibly truncated) prompt and logs a warning.
 */
/**
 * Section-aware truncation: keeps first (## Task) and last (## Constraints) sections,
 * drops middle sections (## File Contents) by size, largest first.
 */
function smartTruncate(text: string, maxChars: number): string {
  if (text.length <= maxChars) return text;

  const sections = text.split(/(?=^## )/m);
  if (sections.length <= 2) {
    // No sections to drop — hard truncate
    return text.slice(0, maxChars) +
      "\n\n... [TRUNCATED: context budget exceeded. Focus on the information above.]";
  }

  // Keep first and last section, trim middle sections largest-first
  const first = sections[0];
  const last = sections[sections.length - 1];
  const middle = sections.slice(1, -1).map((s, i) => ({ text: s, index: i, size: s.length }));

  // Sort middle by size descending — drop largest first
  middle.sort((a, b) => b.size - a.size);

  const reserved = first.length + last.length;
  let remaining = maxChars - reserved;
  const kept: { text: string; index: number }[] = [];

  for (const section of middle) {
    if (remaining >= section.size) {
      remaining -= section.size;
      kept.push(section);
    }
    // else: drop this section
  }

  // Restore original order
  kept.sort((a, b) => a.index - b.index);

  const result = [first, ...kept.map((s) => s.text), last].join("");
  if (result.length > maxChars) {
    return result.slice(0, maxChars) +
      "\n\n... [TRUNCATED: context budget exceeded. Focus on the information above.]";
  }
  return result;
}

function enforceContextBudget(
  systemPrompt: string,
  userPrompt: string,
  model: string,
): { systemPrompt: string; userPrompt: string; truncated: boolean } {
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budget = Math.floor(limit * CONTEXT_BUDGET_RATIO);

  const systemTokens = estimateTokens(systemPrompt);
  const userTokens = estimateTokens(userPrompt);
  const totalTokens = systemTokens + userTokens;
  const pct = Math.round((totalTokens / budget) * 100);

  // Always log token breakdown
  console.error(
    `[context-budget] ${model}: sys=~${systemTokens}tok user=~${userTokens}tok total=~${totalTokens}/${budget}tok (${pct}%)`
  );

  if (totalTokens <= budget) {
    return { systemPrompt, userPrompt, truncated: false };
  }

  // System prompt takes priority — truncate user prompt
  const availableForUser = Math.max(budget - systemTokens, 1000);
  const maxChars = availableForUser * 4;

  console.error(
    `[context-budget] Over budget — truncating user prompt from ${userPrompt.length} to ${maxChars} chars.`
  );

  const truncatedPrompt = smartTruncate(userPrompt, maxChars);

  return { systemPrompt, userPrompt: truncatedPrompt, truncated: true };
}

// ─── Pi Subagent Spawning ──────────────────────────────────────────────────

interface SpawnSubagentOpts {
  prompt: string;
  systemPrompt: string;
  model?: string;
  tools?: string[];
  signal?: AbortSignal;
}

/**
 * Spawn a pi subagent process with isolated context.
 * Uses --mode json to capture structured output.
 * System prompt is written to a temp file and passed via --append-system-prompt.
 * Enforces context budget before spawning.
 */
async function spawnSubagent(opts: SpawnSubagentOpts): Promise<string> {
  let tmpDir: string | null = null;
  let tmpFile: string | null = null;

  // Enforce context budget before spawning
  const model = opts.model || "sonnet";
  const budget = enforceContextBudget(opts.systemPrompt, opts.prompt, model);
  const effectiveSystemPrompt = budget.systemPrompt;
  const effectivePrompt = budget.userPrompt;

  try {
    const args: string[] = [
      "--mode", "json",
      "-p",
      "--no-session",
      "--model", model,
    ];

    if (opts.tools && opts.tools.length > 0) {
      args.push("--tools", opts.tools.join(","));
    }

    // Write system prompt to temp file
    if (effectiveSystemPrompt.trim()) {
      tmpDir = fs.mkdtempSync(join(os.tmpdir(), "delve-subagent-"));
      tmpFile = join(tmpDir, "system-prompt.md");
      fs.writeFileSync(tmpFile, effectiveSystemPrompt, { encoding: "utf-8", mode: 0o600 });
      args.push("--append-system-prompt", tmpFile);
    }

    args.push(`Task: ${effectivePrompt}`);

    return await new Promise<string>((resolve, reject) => {
      const proc = spawn("pi", args, {
        cwd: PROJECT_ROOT,
        shell: false,
        stdio: ["ignore", "pipe", "pipe"],
      });

      let buffer = "";
      let finalText = "";

      const processLine = (line: string) => {
        if (!line.trim()) return;
        let event: any;
        try {
          event = JSON.parse(line);
        } catch {
          return;
        }

        if (event.type === "message_end" && event.message) {
          const msg = event.message;
          if (msg.role === "assistant") {
            for (const part of msg.content) {
              if (part.type === "text") {
                finalText = part.text;
              }
            }
          }
        }
      };

      proc.stdout.on("data", (data: Buffer) => {
        buffer += data.toString();
        const lines = buffer.split("\n");
        buffer = lines.pop() || "";
        for (const line of lines) processLine(line);
      });

      proc.stderr.on("data", () => {});

      proc.on("close", (code: number | null) => {
        if (buffer.trim()) processLine(buffer);
        if (code !== 0 && !finalText) {
          reject(new Error(`Subagent exited with code ${code}`));
        } else {
          resolve(finalText);
        }
      });

      proc.on("error", (err: Error) => {
        reject(err);
      });

      if (opts.signal) {
        const killProc = () => {
          proc.kill("SIGTERM");
          setTimeout(() => {
            if (!proc.killed) proc.kill("SIGKILL");
          }, 5000);
        };
        if (opts.signal.aborted) killProc();
        else opts.signal.addEventListener("abort", killProc, { once: true });
      }
    });
  } finally {
    if (tmpFile) try { fs.unlinkSync(tmpFile); } catch { /* ignore */ }
    if (tmpDir) try { fs.rmdirSync(tmpDir); } catch { /* ignore */ }
  }
}

// ─── Subsystem Agent Config ──────────────────────────────────────────────────

const SUBSYSTEM_CONFIG: Record<string, { skill: string; rule: string; description: string }> = {
  terrain: {
    skill: "terrain",
    rule: "terrain",
    description: "TERRAIN SPECIALIST. You own noise generation, composition, contour detection, hex columns, lava/void, and mesh generation. Directories: src/game/terrain/.",
  },
  actor: {
    skill: "actor",
    rule: "actors",
    description: "ACTOR SPECIALIST. You own skeleton, inverse kinematics, gait cycles, proportions, and animation. Directories: src/game/render/, src/game/actor.h.",
  },
  shader: {
    skill: "shader",
    rule: "shaders",
    description: "SHADER SPECIALIST. You own GLSL 4.5, SPIR-V compilation, vertex layouts, compute shaders, and lighting. Directories: src/shaders/.",
  },
  engine: {
    skill: "engine",
    rule: "engine",
    description: "ENGINE SPECIALIST. You own application lifecycle, GPU context, camera, input, ECS, ImGui UI, and rendering pipeline. Directories: src/engine/.",
  },
};

/**
 * Build a system prompt for a subsystem agent by concatenating
 * SYSTEM.md + skill file + rule file + role description.
 */
function buildSubsystemPrompt(subsystem: string): string {
  const config = SUBSYSTEM_CONFIG[subsystem];
  if (!config) return loadSystemPrompt();

  const parts = [loadSystemPrompt()];

  const skill = loadSkill(config.skill);
  if (skill) parts.push("---\n\n" + skill);

  const rule = loadRule(config.rule);
  if (rule) parts.push("---\n\n" + rule);

  parts.push(`---

## Your Role
You are a ${config.description}

Given a task and relevant source files, produce a complete implementation.

## Output Format
For each file change, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[complete file content]
\`\`\`

## Constraints
- Output ONLY file blocks. No preamble, no explanation after.
- Follow existing code conventions exactly.
- Only change what the task requires — no unnecessary refactoring.
- Include all necessary #includes.
- Every file block must contain the COMPLETE file content.`);

  return parts.join("\n\n");
}

// ─── Subsystem Agents (Sonnet via pi subagent) ──────────────────────────────

interface SubsystemAgentOpts {
  task: string;
  files: string[];
}

async function callSubsystemAgent(
  subsystem: string,
  opts: SubsystemAgentOpts,
): Promise<string> {
  const systemPrompt = buildSubsystemPrompt(subsystem);
  const agentName = SUBSYSTEM_AGENT_MAP[subsystem] || subsystem;
  const agentConfig = loadAgentConfig(agentName);
  const model = agentConfig.model || "sonnet";

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);

  const fileContents = loadFileContents(opts.files, remainingChars);

  const prompt = `## Task\n${opts.task}\n\n## Current File Contents\n${fileContents}`;

  const result = await spawnWithEscalation({
    prompt,
    systemPrompt,
    model,
    tools: agentConfig.tools.length > 0 ? agentConfig.tools : undefined,
    expectFileBlocks: true,
  });

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
    model: plannerConfig.model || "sonnet",
    tools: plannerConfig.tools.length > 0 ? plannerConfig.tools : undefined,
  });

  writeState("plan.md", plan);
  return plan;
}

// ─── Subtask Parser ─────────────────────────────────────────────────────────

export interface Subtask {
  subsystem: string;
  task: string;
}

/**
 * Parse a planner's output into per-subsystem subtasks.
 * Expects "## Subtask N [subsystem]" headers.
 */
export function parseSubtasks(plan: string): Subtask[] {
  const subtasks: Subtask[] = [];
  const regex = /##\s*Subtask\s+\d+\s*\[(\w+)\]\s*\n([\s\S]*?)(?=##\s*Subtask\s+\d+|$)/gi;
  let match;

  while ((match = regex.exec(plan)) !== null) {
    const subsystem = match[1].toLowerCase();
    const task = match[2].trim();
    if (task.length > 0) {
      subtasks.push({ subsystem, task });
    }
  }

  return subtasks;
}

// ─── Agent Tool: ask_meta_implementer (Sonnet) ─────────────────────────────

export async function askMetaImplementer(opts: {
  plan: string;
  task: string;
  files: string[];
}): Promise<string> {
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
You are a META-IMPLEMENTER. Produce complete file implementations for every subtask in the plan.

## Output Format
For each file, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[COMPLETE file content — not a diff, not a snippet, the FULL file]
\`\`\`

## Constraints
- Output ONLY file blocks. No preamble, no explanation after.
- Follow existing code conventions exactly.
- Change only what the plan requires — no unnecessary refactoring.
- Include all necessary #includes.
- Maintain hex coordinate invariants.
- Size MapData vectors correctly.`;

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS["sonnet"] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);
  const fileContents = loadFileContents(opts.files, remainingChars);

  const prompt = `## Task
${opts.task}

## Plan
${opts.plan}

## Current File Contents
${fileContents}`;

  const result = await spawnWithEscalation({
    prompt,
    systemPrompt,
    model: "sonnet",
    expectFileBlocks: true,
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
  const systemPrompt = `${loadSystemPrompt()}

---

${loadSkill("test")}

---

## Your Role
You are a META-TESTER. Produce test code for the implementation changes.

## Output Format
For each test file, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[COMPLETE file content]
\`\`\`

Include CMakeLists.txt updates if new test files are added.

## Constraints
- Use deterministic seeds (42, 123, etc.).
- Use 256x256 maps for speed.
- Test one property per function.
- Name tests: subsystem_property_being_tested.
- Metric extractors must be pure functions — no GPU, no window.
- Use DELVE_TEST macro and EXPECT_* assertions.`;

  const testerConfig = loadAgentConfig("tester");
  const model = testerConfig.model || "sonnet";

  // Compute remaining char budget for file contents
  const limit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetTokens = Math.floor(limit * CONTEXT_BUDGET_RATIO);
  const systemTokens = estimateTokens(systemPrompt);
  const remainingChars = Math.max((budgetTokens - systemTokens) * 4 - 2000, 8000);
  const fileContents = loadFileContents(opts.changedFiles, remainingChars);

  const prompt = `## Original Task
${opts.task}

## Implementation Summary
${opts.implementationSummary}

## Changed Files
${fileContents}`;

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model,
    tools: testerConfig.tools.length > 0 ? testerConfig.tools : undefined,
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
You are a CODE REVIEWER. Return APPROVE or REQUEST_CHANGES. No middle ground.

## Output Format
## Review: [APPROVE / REQUEST_CHANGES]

### Summary
[1-2 sentences]

### Issues (if any)
1. [file:line] [severity] — [description]

### Tests
- [PASS/FAIL] All tests pass
- [YES/NO] New behavior is tested

## Constraints
- Reject on memory safety violations or GPU resource leaks.
- Reject on broken hex coordinate invariants.
- Reject on O(N^2) over large data.
- Ignore style nits unless they break conventions.`;

  const prompt = `## Task
${opts.task}

## Diff
\`\`\`
${opts.diff}
\`\`\`

## Test Results
${opts.testResults}`;

  const reviewerConfig = loadAgentConfig("reviewer");

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model: reviewerConfig.model || "sonnet",
    tools: reviewerConfig.tools.length > 0 ? reviewerConfig.tools : undefined,
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

  return spawnSubagent({
    prompt,
    systemPrompt: opts.systemPrompt,
    model: "haiku",
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

  const prompt = `## Task Type: ${opts.taskType}

## Context
${opts.context}

## Constraints
${opts.constraints.map((c) => `- ${c}`).join("\n")}`;

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model: "sonnet",
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

// ─── Agent Tool: ask_build_fixer (Haiku) ────────────────────────────────────

export async function askBuildFixer(opts: {
  buildOutput: string;
  round: number;
  maxRounds: number;
}): Promise<string> {
  const systemPrompt = `You are a BUILD FIXER for a C++20 CMake project (Delve terrain generator).
You receive compiler error output and must output the exact file changes needed to fix the errors.

## Output Format
For each file to fix, output the COMPLETE file with your fixes applied:

### FILE: <path>
#### ACTION: MODIFY
\`\`\`cpp
[COMPLETE file content with fixes applied — not a snippet, the FULL file]
\`\`\`

CRITICAL: You must output the ENTIRE file content, not just the changed lines.
The file will be overwritten with exactly what you output.

## Constraints
- Fix ONLY the compilation errors shown — don't refactor
- If a header is missing, add the #include
- If a type is wrong, fix the type
- If a function signature changed, update callers
- Preserve all existing code that doesn't need to change`;

  const config = loadAgentConfig("build-fixer");

  return spawnSubagent({
    prompt: `BUILD FAILED (attempt ${opts.round}/${opts.maxRounds}).

Compiler output (tail):
\`\`\`
${opts.buildOutput}
\`\`\`

Fix the compilation errors.`,
    systemPrompt,
    model: config.model || "haiku",
    tools: config.tools.length > 0 ? config.tools : undefined,
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
For each file to fix, output the COMPLETE file with your fixes applied:

### FILE: <path>
#### ACTION: MODIFY
\`\`\`cpp
[COMPLETE file content with fixes applied — not a snippet, the FULL file]
\`\`\`

CRITICAL: You must output the ENTIRE file content, not just the changed lines.
The file will be overwritten with exactly what you output.

## Constraints
${opts.isBuildFailure
    ? "- Fix compilation errors in test code"
    : "- Fix the implementation, not the tests, unless test expectations are clearly wrong"
  }
- Preserve all existing code that doesn't need to change
- Don't refactor — minimal fixes only`;

  const config = loadAgentConfig("test-fixer");

  return spawnSubagent({
    prompt: `${what} FAILED (attempt ${opts.round}/${opts.maxRounds}).

Output (tail):
\`\`\`
${opts.testOutput}
\`\`\`

Fix the errors.`,
    systemPrompt,
    model: config.model || "haiku",
    tools: config.tools.length > 0 ? config.tools : undefined,
  });
}

// ─── Agent Tool: ask_diagnoser (Sonnet) ──────────────────────────────────────

export async function askDiagnoser(opts: {
  task: string;
  testOutput: string;
  recentCommits: string;
}): Promise<string> {
  const systemPrompt = `${loadSystemPrompt()}

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
[relevant error messages, stack traces, or logic errors]

## Constraints
- Analyze, don't fix.
- Be specific: name the function, line, and variable causing the issue.
- If multiple failures, identify whether they share a root cause.`;

  const prompt = `## Task
${opts.task}

## Test Output
\`\`\`
${opts.testOutput}
\`\`\`

## Recent Commits
${opts.recentCommits}

Diagnose the root cause of the test failures.`;

  return spawnSubagent({
    prompt,
    systemPrompt,
    model: "sonnet",
    tools: ["Read", "Glob", "Grep"],
  });
}

// ─── Agent Tool: ask_blueprint_generator (Sonnet) ────────────────────────────

export async function askBlueprintGenerator(opts: {
  task: string;
  availablePhases: string[];
}): Promise<string> {
  const systemPrompt = `You are a PIPELINE ARCHITECT. Given a development task, design an optimal pipeline
by selecting and ordering phases from the available list.

## Output Format (JSON only)
\`\`\`json
{
  "name": "descriptive-pipeline-name",
  "description": "What this pipeline does",
  "phases": [
    { "name": "Human-readable phase name", "type": "deterministic|agentic", "handler": "handler_key" }
  ]
}
\`\`\`

## Rules
- Use ONLY handlers from the available list.
- Every pipeline MUST start with "branch" and end with "commit_pr".
- Include "build" after any code generation phase.
- Simple tasks need fewer phases — don't over-engineer.
- For refactoring: skip test-writing (behavior should be preserved).
- For bugfixes: include "diagnose" before "implement".
- For shader-only tasks: use "shader_validate" instead of "test".`;

  const prompt = `## Task
${opts.task}

## Available Phase Handlers
${opts.availablePhases.map((p) => `- ${p}`).join("\n")}

Design the optimal pipeline for this task.`;

  return spawnSubagent({
    prompt,
    systemPrompt,
    model: "sonnet",
  });
}

// ─── Failure Detection & Escalation ──────────────────────────────────────────

export interface FailureSignal {
  failed: boolean;
  reason: string;
  category: "capability" | "tool" | "context" | "unknown";
}

export function detectAgentFailure(output: string): FailureSignal {
  // Capability failures
  if (/\b(I cannot|I can't|I am unable to|I'm unable to)\b/i.test(output)) {
    return { failed: true, reason: "Agent reported inability", category: "capability" };
  }

  // Tool failures
  if (/\b(tool|command)\s+(not\s+)?(available|found|supported)\b/i.test(output)) {
    return { failed: true, reason: "Tool not available", category: "tool" };
  }
  if (/Error:\s*(ENOENT|EACCES|EPERM)\b/.test(output)) {
    return { failed: true, reason: "File system error", category: "tool" };
  }

  // Context failures
  if (/\b(context|file)\s+(missing|truncated|not found)\b/i.test(output)) {
    return { failed: true, reason: "Missing context", category: "context" };
  }

  return { failed: false, reason: "", category: "unknown" };
}

export function escalate(opts: {
  failureCategory: string;
  currentModel: string;
  currentTools: string[];
}): { model: string; tools: string[] } {
  // Never auto-escalate to opus
  if (opts.currentModel === "sonnet" || opts.currentModel === "opus") {
    return { model: opts.currentModel, tools: opts.currentTools };
  }

  switch (opts.failureCategory) {
    case "capability":
      // Upgrade haiku → sonnet
      return { model: "sonnet", tools: opts.currentTools };
    case "tool":
      // Add write tools, upgrade model
      return {
        model: "sonnet",
        tools: [...new Set([...opts.currentTools, "Write", "Edit", "Bash"])],
      };
    case "context":
      // Same model but expand file reading tools
      return {
        model: opts.currentModel,
        tools: [...new Set([...opts.currentTools, "Read", "Glob", "Grep"])],
      };
    default:
      return { model: "sonnet", tools: opts.currentTools };
  }
}

const MAX_ESCALATION_DEPTH = 2;

export async function spawnWithEscalation(
  opts: SpawnSubagentOpts & { depth?: number; expectFileBlocks?: boolean },
): Promise<string> {
  const result = await spawnSubagent(opts);
  const failure = detectAgentFailure(result);
  const noBlocks = opts.expectFileBlocks && !result.match(/###\s*FILE:/g);

  if ((failure.failed || noBlocks) && (opts.depth || 0) < MAX_ESCALATION_DEPTH) {
    const currentModel = opts.model || "sonnet";
    const esc = escalate({
      failureCategory: failure.failed ? failure.category : "capability",
      currentModel,
      currentTools: opts.tools || [],
    });

    // Never auto-escalate to opus
    if (esc.model === "opus") return result;
    // No change? Don't retry
    if (esc.model === currentModel && JSON.stringify(esc.tools) === JSON.stringify(opts.tools || [])) return result;

    console.error(
      `[escalation] ${currentModel}→${esc.model} reason=${failure.failed ? failure.reason : "no FILE blocks"}`
    );

    return spawnWithEscalation({
      ...opts,
      model: esc.model,
      tools: esc.tools.length > 0 ? esc.tools : undefined,
      depth: (opts.depth || 0) + 1,
    });
  }

  return result;
}
