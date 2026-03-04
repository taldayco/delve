import { spawn } from "node:child_process";
import { readFileSync, existsSync, statSync, mkdirSync, writeFileSync } from "node:fs";
import * as fs from "node:fs";
import * as os from "node:os";
import { join } from "node:path";
import { EventEmitter } from "node:events";

// ─── Agent Status Events ────────────────────────────────────────────────────

export const agentEvents = new EventEmitter();

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

function loadAgentSystemPrompt(): string {
  return loadFile(join(PROJECT_ROOT, ".pi/SYSTEM_AGENT.md"));
}

// ─── Agent Config Loader ──────────────────────────────────────────────────

interface AgentConfig {
  name: string;
  tools: string[];
  model: string;
  thinking?: string;
}

const agentConfigCache = new Map<string, AgentConfig>();

/**
 * Parse YAML frontmatter from .pi/agents/<name>.md to extract tool restrictions.
 * Returns tool list and model from the agent definition file.
 */
function loadAgentConfig(agentName: string): AgentConfig {
  const cached = agentConfigCache.get(agentName);
  if (cached) return cached;

  const defaults: AgentConfig = { name: agentName, tools: [], model: "anthropic/claude-sonnet-4-6" };
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

  const thinkingMatch = frontmatter.match(/^thinking:\s*(.+)$/m);
  if (thinkingMatch) {
    config.thinking = thinkingMatch[1].trim();
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
  const filePath = join(STATE_DIR, filename);

  // Archive previous version before overwriting
  if (existsSync(filePath)) {
    const historyDir = join(STATE_DIR, "history", new Date().toISOString().replace(/[:.]/g, "-"));
    mkdirSync(historyDir, { recursive: true });
    try {
      fs.copyFileSync(filePath, join(historyDir, filename));
    } catch { /* ignore archive failures */ }
    pruneStateHistory();
  }

  writeFileSync(filePath, content, "utf-8");
}

/**
 * Keep only the last 5 history snapshots.
 */
function pruneStateHistory(): void {
  const historyRoot = join(STATE_DIR, "history");
  if (!existsSync(historyRoot)) return;
  try {
    const dirs = fs.readdirSync(historyRoot)
      .filter((d) => {
        try { return fs.statSync(join(historyRoot, d)).isDirectory(); } catch { return false; }
      })
      .sort();
    while (dirs.length > 5) {
      const oldest = dirs.shift()!;
      const dirPath = join(historyRoot, oldest);
      try {
        for (const f of fs.readdirSync(dirPath)) {
          fs.unlinkSync(join(dirPath, f));
        }
        fs.rmdirSync(dirPath);
      } catch { /* ignore */ }
    }
  } catch { /* ignore */ }
}

export function readState(filename: string): string {
  const path = join(STATE_DIR, filename);
  return existsSync(path) ? readFileSync(path, "utf-8") : "";
}

// ─── Run Metrics Tracking ──────────────────────────────────────────────────

export interface RunMetricsRecord {
  runId: string;
  timestamp: string;
  agentName: string;
  model: string;
  phase: string;
  durationMs: number;
  contextUsagePct: number;
  success: boolean;
  escalated: boolean;
  truncated: boolean;
}

const METRICS_FILE = join(STATE_DIR, "metrics.jsonl");

export function appendRunMetrics(record: RunMetricsRecord): void {
  mkdirSync(STATE_DIR, { recursive: true });
  fs.appendFileSync(METRICS_FILE, JSON.stringify(record) + "\n", "utf-8");
}

export function readRunMetrics(maxRecords = 50): RunMetricsRecord[] {
  if (!existsSync(METRICS_FILE)) return [];
  const lines = readFileSync(METRICS_FILE, "utf-8").trim().split("\n").filter(Boolean);
  const records: RunMetricsRecord[] = [];
  const start = Math.max(0, lines.length - maxRecords);
  for (let i = start; i < lines.length; i++) {
    try {
      records.push(JSON.parse(lines[i]));
    } catch { /* skip malformed lines */ }
  }
  return records;
}

export function pruneMetricsLog(maxLines = 500): number {
  if (!existsSync(METRICS_FILE)) return 0;
  const lines = readFileSync(METRICS_FILE, "utf-8").trim().split("\n").filter(Boolean);
  if (lines.length <= maxLines) return 0;
  const pruned = lines.length - maxLines;
  writeFileSync(METRICS_FILE, lines.slice(pruned).join("\n") + "\n", "utf-8");
  return pruned;
}

let metricsRunCounter = 0;

// ─── File Content Loader ───────────────────────────────────────────────────

function loadFileContents(files: string[], budgetChars?: number, cwd?: string): string {
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
      timeout: 3_600_000,
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
  "anthropic/claude-opus-4-6": 200_000,
  "anthropic/claude-sonnet-4-6": 200_000,
  "anthropic/claude-haiku-4-5": 200_000,
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
interface TruncateResult {
  text: string;
  droppedSections: number;
  droppedChars: number;
}

function smartTruncate(text: string, maxChars: number): TruncateResult {
  if (text.length <= maxChars) return { text, droppedSections: 0, droppedChars: 0 };

  const sections = text.split(/(?=^## )/m);
  if (sections.length <= 2) {
    // No sections to drop — hard truncate
    const droppedChars = text.length - maxChars;
    return {
      text: text.slice(0, maxChars) +
        "\n\n... [TRUNCATED: context budget exceeded. Focus on the information above.]",
      droppedSections: 0,
      droppedChars,
    };
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
  let droppedSections = 0;
  let droppedChars = 0;

  for (const section of middle) {
    if (remaining >= section.size) {
      remaining -= section.size;
      kept.push(section);
    } else {
      droppedSections++;
      droppedChars += section.size;
    }
  }

  // Restore original order
  kept.sort((a, b) => a.index - b.index);

  const result = [first, ...kept.map((s) => s.text), last].join("");
  if (result.length > maxChars) {
    droppedChars += result.length - maxChars;
    return {
      text: result.slice(0, maxChars) +
        "\n\n... [TRUNCATED: context budget exceeded. Focus on the information above.]",
      droppedSections,
      droppedChars,
    };
  }
  return { text: result, droppedSections, droppedChars };
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

  const truncResult = smartTruncate(userPrompt, maxChars);

  console.error(
    `[context-budget] Dropped ${truncResult.droppedSections} sections (${truncResult.droppedChars} chars)`
  );

  // Prepend warning so the agent knows context was lost
  const warningHeader = `> WARNING: CONTEXT TRUNCATED — ${truncResult.droppedSections} sections (${truncResult.droppedChars} chars) were dropped to fit within context budget. Some file contents or details may be missing.\n\n`;

  return { systemPrompt, userPrompt: warningHeader + truncResult.text, truncated: true };
}

// ─── Pi Subagent Spawning ──────────────────────────────────────────────────

interface SpawnSubagentOpts {
  prompt: string;
  systemPrompt: string;
  model?: string;
  thinking?: string;
  tools?: string[];
  signal?: AbortSignal;
  agentName?: string;
  timeoutMs?: number;
  cwd?: string;
}

/**
 * Spawn a pi subagent process with isolated context.
 * Uses --mode json to capture structured output.
 * System prompt is written to a temp file and passed via --append-system-prompt.
 * Enforces context budget before spawning.
 */
async function spawnSubagent(opts: SpawnSubagentOpts): Promise<string> {
  try {
    return await spawnSubagentOnce(opts);
  } catch (err: any) {
    // Retry once on spawn failures (ENOENT, EACCES, etc.) with a short delay
    const isSpawnError = err?.code === "ENOENT" || err?.code === "EACCES";
    if (isSpawnError) {
      const agentName = opts.agentName || "subagent";
      agentEvents.emit("agent:retry", { name: agentName, error: err.message });
      await new Promise((r) => setTimeout(r, 2000));
      return await spawnSubagentOnce(opts);
    }
    throw err;
  }
}

async function spawnSubagentOnce(opts: SpawnSubagentOpts): Promise<string> {
  let tmpDir: string | null = null;
  let tmpFile: string | null = null;

  // Enforce context budget before spawning
  const model = opts.model || "anthropic/claude-sonnet-4-6";
  const budget = enforceContextBudget(opts.systemPrompt, opts.prompt, model);
  const effectiveSystemPrompt = budget.systemPrompt;
  const effectivePrompt = budget.userPrompt;

  const agentName = opts.agentName || "subagent";
  const metricsStartTime = Date.now();
  const runId = `${agentName}-${++metricsRunCounter}-${Date.now()}`;

  try {
    agentEvents.emit("agent:start", { name: agentName, model });

    const args: string[] = [
      "--mode", "json",
      "-p",
      "--no-session",
      "--no-extensions",
      "--no-skills",
      "--no-prompt-templates",
      "--model", model,
    ];

    if (opts.thinking) {
      args.push("--thinking", opts.thinking);
    }

    if (opts.tools && opts.tools.length > 0) {
      args.push("--tools", opts.tools.join(","));
    }

    // Write system prompt and user prompt to temp files to avoid E2BIG
    if (!tmpDir) {
      tmpDir = fs.mkdtempSync(join(os.tmpdir(), "delve-subagent-"));
    }

    if (effectiveSystemPrompt.trim()) {
      tmpFile = join(tmpDir, "system-prompt.md");
      fs.writeFileSync(tmpFile, effectiveSystemPrompt, { encoding: "utf-8", mode: 0o600 });
      args.push("--append-system-prompt", tmpFile);
    }

    // Write user prompt to temp file and pass via @file to avoid ARG_MAX limits
    const promptFile = join(tmpDir, "user-prompt.md");
    fs.writeFileSync(promptFile, `Task: ${effectivePrompt}`, { encoding: "utf-8", mode: 0o600 });
    args.push(`@${promptFile}`);

    const SUBAGENT_TIMEOUT_MS = opts.timeoutMs ?? 3_600_000; // 1 hour

    const proc = spawn("pi", args, {
      cwd: opts.cwd || PROJECT_ROOT,
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    });

    const spawnPromise = new Promise<string>((resolve, reject) => {
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
        agentEvents.emit("agent:progress", { name: agentName });
        buffer += data.toString();
        const lines = buffer.split("\n");
        buffer = lines.pop() || "";
        for (const line of lines) processLine(line);
      });

      proc.stderr.on("data", () => {});

      proc.on("close", (code: number | null) => {
        if (buffer.trim()) processLine(buffer);
        agentEvents.emit("agent:end", { name: agentName, model });
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
          if (!proc.killed) proc.kill("SIGKILL");
        };
        if (opts.signal.aborted) killProc();
        else opts.signal.addEventListener("abort", killProc, { once: true });
      }
    });

    let timeoutHandle: ReturnType<typeof setTimeout> | null = null;
    const timeoutPromise = new Promise<never>((_, reject) => {
      timeoutHandle = setTimeout(() => {
        reject(new Error(`Subagent '${agentName}' timed out after ${SUBAGENT_TIMEOUT_MS / 1000}s`));
      }, SUBAGENT_TIMEOUT_MS);
    });

    let result: string;
    let success = true;
    try {
      result = await Promise.race([spawnPromise, timeoutPromise]);
    } catch (err) {
      success = false;
      // Record failure metrics
      const contextLimit = MODEL_CONTEXT_LIMITS[model] || 200_000;
      const contextBudget = Math.floor(contextLimit * CONTEXT_BUDGET_RATIO);
      const totalTokens = estimateTokens(effectiveSystemPrompt) + estimateTokens(effectivePrompt);
      const pct = Math.round((totalTokens / contextBudget) * 100);
      try {
        appendRunMetrics({
          runId,
          timestamp: new Date().toISOString(),
          agentName,
          model,
          phase: agentName,
          durationMs: Date.now() - metricsStartTime,
          contextUsagePct: pct,
          success: false,
          escalated: false,
          truncated: budget.truncated,
        });
      } catch { /* ignore metrics failures */ }
      throw err;
    } finally {
      if (timeoutHandle) clearTimeout(timeoutHandle);
      if (!proc.killed) {
        proc.kill("SIGKILL");
      }
    }

    // Record success metrics
    const contextLimit = MODEL_CONTEXT_LIMITS[model] || 200_000;
    const contextBudget = Math.floor(contextLimit * CONTEXT_BUDGET_RATIO);
    const totalTokens = estimateTokens(effectiveSystemPrompt) + estimateTokens(effectivePrompt);
    const pct = Math.round((totalTokens / contextBudget) * 100);
    try {
      appendRunMetrics({
        runId,
        timestamp: new Date().toISOString(),
        agentName,
        model,
        phase: agentName,
        durationMs: Date.now() - metricsStartTime,
        contextUsagePct: pct,
        success: true,
        escalated: false,
        truncated: budget.truncated,
      });
    } catch { /* ignore metrics failures */ }

    return result;
  } finally {
    // Clean up all temp files in the directory
    if (tmpDir) {
      try {
        for (const f of fs.readdirSync(tmpDir)) {
          fs.unlinkSync(join(tmpDir, f));
        }
        fs.rmdirSync(tmpDir);
      } catch { /* ignore */ }
    }
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
  if (!config) return loadAgentSystemPrompt();

  const parts = [loadAgentSystemPrompt()];

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

// ─── Subsystem Agents (Sonnet meta → Haiku workers) ─────────────────────────

interface SubsystemAgentOpts {
  task: string;
  files: string[];
  cwd?: string;
}

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

// ─── Agent Tool: ask_meta_planner (Sonnet) ─────────────────────────────────

/**
 * Single-subsystem planner. Plans subtasks for one subsystem only.
 * Used by askParallelPlanner to fan out planning across subsystems.
 */
async function askSubsystemPlanner(opts: {
  task: string;
  subsystem: string;
  codebaseContext: string;
  cwd?: string;
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
}): Promise<string> {
  const subsystems = Object.keys(opts.subsystemContexts);

  // Fan out: spawn one planner per subsystem in parallel
  const planPromises = subsystems.map((subsystem) =>
    askSubsystemPlanner({
      task: opts.task,
      subsystem,
      codebaseContext: opts.subsystemContexts[subsystem],
      cwd: opts.cwd,
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
  });

  writeState("plan.md", plan);
  return plan;
}

// ─── Subtask Parser ─────────────────────────────────────────────────────────

export interface Subtask {
  subsystem: string;
  task: string;
}

const SUBSYSTEM_PATH_PATTERNS: [RegExp, string][] = [
  [/src\/game\/terrain\//i, "terrain"],
  [/src\/game\/render\//i, "actor"],
  [/src\/shaders\//i, "shader"],
  [/src\/engine\//i, "engine"],
  [/src\/test\//i, "engine"],
];

/**
 * Infer subsystem from subtask body text by looking for file path patterns.
 * Falls back to "engine" if no pattern matches.
 */
function inferSubsystem(text: string): string {
  for (const [pattern, subsystem] of SUBSYSTEM_PATH_PATTERNS) {
    if (pattern.test(text)) return subsystem;
  }
  // Keyword fallback
  const lower = text.toLowerCase();
  if (/\b(noise|hex|contour|plateau|lava|mesh|elevation)\b/.test(lower)) return "terrain";
  if (/\b(skeleton|ik|gait|animation|actor|arm|leg|joint)\b/.test(lower)) return "actor";
  if (/\b(glsl|spir-?v|vertex|fragment|compute|shader)\b/.test(lower)) return "shader";
  return "engine";
}

/**
 * Parse a planner's output into per-subsystem subtasks.
 * Tolerates common LLM format deviations:
 *   - ## or ### heading levels
 *   - [tag] anywhere on the header line (not just after the number)
 *   - em-dash descriptions, checkmarks, bold text in headers
 *   - Missing [tag] (infers subsystem from body content)
 */
export function parseSubtasks(plan: string): Subtask[] {
  const subtasks: Subtask[] = [];
  let match;

  // Pass 1: headers with explicit [subsystem] bracket tags
  const taggedRegex = /#{2,3}\s*Subtask\s+\d+[^[\n]*\[(\w+)\][^\n]*\n([\s\S]*?)(?=#{2,3}\s*Subtask\s+\d+|$)/gi;
  while ((match = taggedRegex.exec(plan)) !== null) {
    const subsystem = match[1].toLowerCase();
    const task = match[2].trim();
    if (task.length > 0) {
      subtasks.push({ subsystem, task });
    }
  }

  if (subtasks.length > 0) return subtasks;

  // Pass 2: headers without bracket tags — infer subsystem from body
  const untaggedRegex = /#{2,3}\s*Subtask\s+\d+[^\n]*\n([\s\S]*?)(?=#{2,3}\s*Subtask\s+\d+|$)/gi;
  while ((match = untaggedRegex.exec(plan)) !== null) {
    const body = match[1].trim();
    if (body.length > 0) {
      subtasks.push({ subsystem: inferSubsystem(body), task: body });
    }
  }

  return subtasks;
}

// ─── Meta-Agent Decomposition Infrastructure ────────────────────────────────

/**
 * A focused subtask produced by a Sonnet meta-agent decomposer.
 * Each subtask targets exactly one file and includes a self-contained
 * worker prompt so Haiku workers need no additional context.
 */
interface WorkerSubtask {
  file: string;
  action: "CREATE" | "MODIFY";
  instructions: string;
  context_files: string[];
  worker_prompt: string;
}

/**
 * Structured output from a meta-agent decomposer.
 */
interface MetaDecomposition {
  subtasks: WorkerSubtask[];
}

/**
 * Parse a Sonnet meta-agent's JSON decomposition output.
 * Tolerates markdown fences around JSON.
 */
function parseMetaDecomposition(output: string): MetaDecomposition {
  try {
    const jsonMatch = output.match(/```json\s*([\s\S]*?)```/);
    const raw = jsonMatch ? jsonMatch[1].trim() : output.trim();
    const parsed = JSON.parse(raw);
    const subtasks: WorkerSubtask[] = (parsed.subtasks || []).map((st: any) => ({
      file: st.file || "",
      action: (st.action || "MODIFY").toUpperCase(),
      instructions: st.instructions || "",
      context_files: st.context_files || st.dependencies || [],
      worker_prompt: st.worker_prompt || st.instructions || "",
    }));
    return { subtasks };
  } catch {
    // Fallback: couldn't parse decomposition — return empty
    return { subtasks: [] };
  }
}

/**
 * Build a meta-decomposer system prompt for a subsystem agent.
 * The decomposer's job is to analyze the task and produce a JSON
 * decomposition of per-file worker subtasks.
 */
function buildMetaDecomposerPrompt(subsystem: string): string {
  const config = SUBSYSTEM_CONFIG[subsystem];
  if (!config) return loadAgentSystemPrompt();

  const parts = [loadAgentSystemPrompt()];

  const skill = loadSkill(config.skill);
  if (skill) parts.push("---\n\n" + skill);

  const rule = loadRule(config.rule);
  if (rule) parts.push("---\n\n" + rule);

  parts.push(`---

## Your Role
You are a META-${config.description}

You do NOT implement changes yourself. Instead, you DECOMPOSE the task into focused per-file
worker subtasks that Haiku-tier workers can execute independently.

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/file.cpp",
      "action": "MODIFY",
      "instructions": "Detailed description of what to change in this file",
      "context_files": ["src/path/to/dependency.h"],
      "worker_prompt": "You are editing file.cpp in a C++20 terrain generator. [Focused instructions for the Haiku worker including exact function signatures, includes needed, and expected output]"
    }
  ]
}
\`\`\`

## Constraints
- Each subtask targets EXACTLY ONE file.
- worker_prompt must be self-contained — the worker has NO other context.
- Include all necessary details: function signatures, types, includes, conventions.
- Order subtasks by dependency (headers before implementations).
- Only decompose changes the task requires — no unnecessary refactoring.
- Output ONLY the JSON block. No preamble, no explanation.`);

  return parts.join("\n\n");
}

/**
 * Delegate a decomposed set of worker subtasks to Haiku workers in parallel.
 * Each worker receives the file content + focused instructions.
 * Returns aggregated FILE blocks from all workers.
 */
async function delegateToWorkers(
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

    return askWorker({
      systemPrompt: workerSystemPrompt,
      task: st.worker_prompt,
      fileContents,
      cwd,
    });
  });

  const results = await Promise.all(workerPromises);
  return results.join("\n\n");
}

// ─── Agent Tool: ask_meta_implementer (Sonnet meta → Haiku workers) ─────────

export async function askMetaImplementer(opts: {
  plan: string;
  task: string;
  files: string[];
  subsystems?: string[];
  cwd?: string;
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

// ─── Agent Tool: ask_meta_tester (Sonnet meta → Haiku workers) ──────────────

export async function askMetaTester(opts: {
  task: string;
  changedFiles: string[];
  implementationSummary: string;
  cwd?: string;
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

// ─── Agent Tool: ask_reviewer (Sonnet meta → Haiku workers → Sonnet synthesis) ──

export async function askReviewer(opts: {
  task: string;
  diff: string;
  testResults: string;
  cwd?: string;
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

// ─── Agent Tool: generate_worker_prompt (Sonnet) ────────────────────────────

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

// ─── Agent Tool: ask_build_fixer (Sonnet meta → Haiku workers) ──────────────

export async function askBuildFixer(opts: {
  buildOutput: string;
  round: number;
  maxRounds: number;
  cwd?: string;
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

// ─── Agent Tool: ask_test_fixer (Sonnet meta → Haiku workers) ────────────────

export async function askTestFixer(opts: {
  testOutput: string;
  round: number;
  maxRounds: number;
  isBuildFailure: boolean;
  cwd?: string;
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

// ─── Agent Tool: ask_diagnoser (Sonnet meta → Haiku workers → Sonnet synthesis) ─

export async function askDiagnoser(opts: {
  task: string;
  testOutput: string;
  recentCommits: string;
  cwd?: string;
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
    });
  }

  // Step 2: Delegate per-error analyses to Haiku workers in parallel
  console.error(`[meta-diagnoser] Decomposed into ${parsed.subtasks.length} per-error analyses`);
  const workerConfig = loadAgentConfig("worker");
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      // Load context files for the worker
      const basePath = opts.cwd || PROJECT_ROOT;
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

// ─── Agent Tool: ask_blueprint_generator (Sonnet meta → Haiku workers → Sonnet synthesis) ─

export async function askBlueprintGenerator(opts: {
  task: string;
  availablePhases: string[];
  cwd?: string;
}): Promise<string> {
  const bpGenConfig = loadAgentConfig("blueprint-gen");
  const model = bpGenConfig.model || "anthropic/claude-sonnet-4-6";

  // Step 1: Sonnet decomposes the task into analysis questions for workers
  const decomposerPrompt = `You are a META-PIPELINE-ARCHITECT. You do NOT design pipelines yourself.
Instead, you DECOMPOSE the task analysis into focused questions that Haiku workers can answer.

For each aspect of the task, produce a worker task that classifies one dimension:
- Task complexity (simple/moderate/complex)
- Subsystems affected (terrain/actor/shader/engine)
- Whether tests are needed
- Whether review is needed
- Whether shader validation is needed
- Whether verification is needed

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "analysis",
      "action": "MODIFY",
      "instructions": "Classify task complexity",
      "context_files": [],
      "worker_prompt": "Classify this development task's complexity as SIMPLE (single file, obvious change), MODERATE (2-4 files, clear approach), or COMPLEX (5+ files, architectural changes). Task: [description]. Output EXACTLY: COMPLEXITY: [SIMPLE|MODERATE|COMPLEX] | REASON: [1 sentence]"
    }
  ]
}
\`\`\`

## Constraints
- 3-6 subtasks covering different analysis dimensions.
- Each worker answers ONE classification question.
- Output ONLY the JSON block.`;

  const prompt = `## Task
${opts.task}

## Available Phase Handlers
${opts.availablePhases.map((p) => `- ${p}`).join("\n")}

Decompose this into classification questions.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: bpGenConfig.thinking || "off",
    agentName: "blueprint-gen-meta",
    cwd: opts.cwd,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: direct pipeline design
  if (parsed.subtasks.length === 0) {
    console.error("[meta-blueprint-gen] Decomposition failed — falling back to direct design");
    const agentBody = loadFile(join(AGENTS_DIR, "blueprint-gen.md"));
    const bodyStart = agentBody.indexOf("---", 3);
    const agentInstructions = bodyStart >= 0 ? agentBody.slice(bodyStart + 3).trim() : "";

    return spawnSubagent({
      prompt,
      systemPrompt: agentInstructions.length > 100 ? agentInstructions : decomposerPrompt,
      model,
      thinking: bpGenConfig.thinking || "off",
      agentName: "blueprint-gen",
      cwd: opts.cwd,
    });
  }

  // Step 2: Delegate classification questions to Haiku workers
  console.error(`[meta-blueprint-gen] Decomposed into ${parsed.subtasks.length} classification tasks`);
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const result = await askWorker({
        systemPrompt: `You are a task classifier for a C++20 game engine development pipeline. Answer the classification question concisely in the requested format.`,
        task: st.worker_prompt,
        cwd: opts.cwd,
      });
      return `- ${st.instructions}: ${result}`;
    }),
  );

  // Step 3: Sonnet synthesizes pipeline from worker classifications
  const agentBody = loadFile(join(AGENTS_DIR, "blueprint-gen.md"));
  const bodyStart = agentBody.indexOf("---", 3);
  const agentInstructions = bodyStart >= 0 ? agentBody.slice(bodyStart + 3).trim() : "";

  const synthesisPrompt = agentInstructions.length > 100
    ? agentInstructions
    : `You are a PIPELINE ARCHITECT. Design an optimal pipeline as JSON.
Every pipeline MUST start with "branch" and end with "commit_pr".
Use ONLY handlers from the available list. Output ONLY valid JSON.`;

  const synthesisInput = `## Task
${opts.task}

## Worker Analysis Results
${workerResults.join("\n")}

## Available Phase Handlers
${opts.availablePhases.map((p) => `- ${p}`).join("\n")}

Based on the worker analysis, design the optimal pipeline. Output ONLY the JSON blueprint.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: bpGenConfig.thinking || "off",
    agentName: "blueprint-gen-synthesizer",
    cwd: opts.cwd,
  });
}

// ─── Agent Tool: ask_verifier (Sonnet meta → Haiku domain analyzers → Sonnet synthesis) ──

export async function askVerifier(opts: {
  task: string;
  metricsDir: string;
  domains: string[];
  cwd?: string;
}): Promise<string> {
  const verifierConfig = loadAgentConfig("verifier");
  const model = verifierConfig.model || "anthropic/claude-sonnet-4-6";

  // Read all domain metric files
  const domainData: Record<string, string> = {};
  for (const domain of opts.domains) {
    const filePath = join(opts.metricsDir, `${domain}.json`);
    if (existsSync(filePath)) {
      domainData[domain] = readFileSync(filePath, "utf-8");
    }
  }

  // Step 1: Fan out Haiku domain analyzers in parallel (one per domain)
  console.error(`[meta-verifier] Delegating ${Object.keys(domainData).length} domain analyses to workers`);
  const workerResults = await Promise.all(
    Object.entries(domainData).map(([domain, metricData]) =>
      spawnDomainAnalyzer({ domain, metricData, task: opts.task, cwd: opts.cwd })
    ),
  );

  const workerSummary = workerResults
    .map((r) => `### ${r.domain}\n${r.result}`)
    .join("\n\n");

  // Step 2: Sonnet synthesizes final verification summary from worker reports
  const agentBody = loadFile(join(AGENTS_DIR, "verifier.md"));
  const bodyStart = agentBody.indexOf("---", 3);
  const verifierInstructions = bodyStart >= 0 ? agentBody.slice(bodyStart + 3).trim() : "";

  const synthesisPrompt = `${loadAgentSystemPrompt()}

${verifierInstructions ? "\n\n" + verifierInstructions : ""}

---

## Your Role
You are a VERIFICATION SYNTHESIZER. Given per-domain analysis results from workers,
produce a unified verification summary.

## Output Format
## Verification Summary

**Overall: PASS | FAIL**

### Per-Domain Worker Results
[Include the worker analysis results]

### Issues Found
- [List any FAIL items with explanation]

## Decision Rules
- If ANY domain reported FAIL → Overall FAIL
- If all domains reported PASS (with optional WARNINGs) → Overall PASS
- Include all worker-reported issues`;

  const domainSections = Object.entries(domainData)
    .map(([domain, content]) => `### ${domain}.json\n\`\`\`json\n${content}\n\`\`\``)
    .join("\n\n");

  const synthesisInput = `## Task Being Verified
${opts.task}

## Worker Domain Analysis Results
${workerSummary}

## Raw Metric Data (for reference)
${domainSections}

Synthesize a final verification verdict from the worker findings.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: verifierConfig.thinking || "low",
    agentName: "verifier-synthesizer",
    cwd: opts.cwd,
  });
}

// ─── Domain Decoupling Analyst (Sonnet meta → Haiku workers → Sonnet synthesis) ──

export async function askDecoupleAnalyst(opts: {
  domainReport: { domain: string; directory: string; fileCount: number; totalLines: number; complexityScore: number; files: string[] };
  recentMetrics: RunMetricsRecord[];
  files: string[];
  cwd?: string;
}): Promise<string> {
  const decouplerConfig = loadAgentConfig("decoupler");
  const model = decouplerConfig.model || "anthropic/claude-sonnet-4-6";

  const fileList = opts.files.map((f) => `- ${f}`).join("\n");
  const metricsSection = opts.recentMetrics.length > 0
    ? `## Recent Agent Metrics\n${opts.recentMetrics.map((m) => `- ${m.agentName}: ${m.contextUsagePct}% context, ${m.durationMs}ms, success=${m.success}`).join("\n")}`
    : "## Recent Agent Metrics\nNo recent metrics available.";

  // Step 1: Sonnet decomposes the domain analysis into per-file-group analysis tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a META-DECOUPLER. You do NOT propose splits yourself. Instead, you DECOMPOSE the domain
into file groups for Haiku workers to analyze for coupling and responsibility boundaries.

For each potential file group, produce a worker task that asks the worker to:
- Identify the group's primary responsibility
- List its public interfaces (headers used outside the group)
- Identify dependencies on other files in the domain

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/game/terrain/noise.cpp",
      "action": "MODIFY",
      "instructions": "Analyze file group: noise generation files",
      "context_files": ["src/game/terrain/noise.h", "src/game/terrain/noise_layers.cpp"],
      "worker_prompt": "You are analyzing files for domain splitting in a C++20 terrain generator. Files in this group: [list]. For each file, output: RESPONSIBILITY: [1 sentence] | PUBLIC_API: [exported symbols used outside this group] | DEPENDS_ON: [files from other groups this file imports]. Output one line per file."
    }
  ]
}
\`\`\`

## Constraints
- Group related files (e.g., noise.h + noise.cpp + noise_layers.cpp).
- Each subtask analyzes one logical file group (3-8 files).
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Domain Analysis
- Domain: ${opts.domainReport.domain}
- Directory: ${opts.domainReport.directory}
- Files: ${opts.domainReport.fileCount}
- Lines: ${opts.domainReport.totalLines}
- Complexity Score: ${opts.domainReport.complexityScore}

## File Listing
${fileList}

${metricsSection}

Decompose this domain into file groups for coupling analysis.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: decouplerConfig.thinking || "medium",
    tools: decouplerConfig.tools.length > 0 ? decouplerConfig.tools : ["read"],
    agentName: "decoupler-meta",
    cwd: opts.cwd,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use direct analysis
  if (parsed.subtasks.length === 0) {
    console.error("[meta-decoupler] Decomposition failed — falling back to direct analysis");
    const directPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a DOMAIN SPLIT SPECIALIST. Analyze this domain and propose how to split it into well-bounded sub-domains.

## Output Format
Produce a structured SPLIT_PROPOSAL in markdown with sub-domains, agent definitions, and keyword map additions.`;

    const result = await spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model,
      thinking: decouplerConfig.thinking || "medium",
      tools: decouplerConfig.tools.length > 0 ? decouplerConfig.tools : ["read"],
      agentName: "decoupler",
      cwd: opts.cwd,
    });
    writeState("decouple_proposal.md", result);
    return result;
  }

  // Step 2: Delegate per-group coupling analysis to Haiku workers in parallel
  console.error(`[meta-decoupler] Decomposed into ${parsed.subtasks.length} file group analyses`);
  const workerConfig = loadAgentConfig("worker");
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const fileContents: Record<string, string> = {};
      const allFiles = [st.file, ...st.context_files].slice(0, 5);
      const basePath = opts.cwd || PROJECT_ROOT;
      for (const f of allFiles) {
        const fullPath = f.startsWith("/") ? f : join(basePath, f);
        if (existsSync(fullPath) && statSync(fullPath).isFile()) {
          const content = readFileSync(fullPath, "utf-8");
          fileContents[f] = content.length > 3000 ? content.slice(0, 3000) + "\n... [truncated]" : content;
        }
      }
      const result = await askWorker({
        systemPrompt: `You are a code coupling analyzer for a C++20 project.
Analyze the provided files and output per-file: RESPONSIBILITY: [1 sentence] | PUBLIC_API: [exported symbols] | DEPENDS_ON: [external dependencies].`,
        task: st.worker_prompt,
        fileContents,
        cwd: opts.cwd,
      });
      return `### File Group: ${st.file}\n${result}`;
    }),
  );

  // Step 3: Sonnet synthesizes split proposal from worker coupling analyses
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a SPLIT SYNTHESIZER. Given per-file-group coupling analyses from workers,
produce a structured domain split proposal.

## Output Format
## SPLIT_PROPOSAL

### New Sub-domains
#### Sub-domain: <name>
- **Directory:** <path>
- **Files to move:** <list>
- **Responsibility:** <description>

### New Agent Definitions
### FILE: .pi/agents/<name>.md
(agent definition for each sub-domain)

### Keyword Map Additions
New entries for KEYWORD_SYNONYMS and SUBSYSTEM_DIRS.

## Constraints
- Each sub-domain must have >= 5 files
- Preserve public interfaces
- Don't cross game/engine boundary`;

  const synthesisInput = `## Domain
${opts.domainReport.domain} (${opts.domainReport.directory}, ${opts.domainReport.fileCount} files, ${opts.domainReport.totalLines} lines)

## Worker Coupling Analyses
${workerResults.join("\n\n")}

${metricsSection}

Synthesize a domain split proposal from the coupling analyses.`;

  const result = await spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: decouplerConfig.thinking || "medium",
    agentName: "decoupler-synthesizer",
    cwd: opts.cwd,
  });

  writeState("decouple_proposal.md", result);
  return result;
}

// ─── Map Updater Agent (Sonnet meta → Haiku workers) ─────────────────────────

export async function askMapUpdater(opts: {
  coverageReport: { unmappedDirs: string[]; unmappedKeywords: string[]; currentKeywordCount: number };
  currentToolsContent: string;
  cwd?: string;
}): Promise<string> {
  // Step 1: Sonnet decomposes unmapped dirs into per-directory classification tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a META-MAP-UPDATER. You do NOT write map code yourself. Instead, you DECOMPOSE
the unmapped directories into classification tasks that Haiku workers can handle.

For each unmapped directory, produce a worker task that classifies it into a canonical subsystem.

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": ".pi/extensions/orchestrator/src/tools.ts",
      "action": "MODIFY",
      "instructions": "Classify directory src/game/new_feature/ into a canonical subsystem",
      "context_files": [],
      "worker_prompt": "Classify this directory into one of these canonical subsystems: terrain, actor, shader, engine. Directory: [path]. Files in it: [list]. Output EXACTLY: SUBSYSTEM: [name] | KEYWORDS: [comma-separated relevant keywords]"
    }
  ]
}
\`\`\`

## Constraints
- One subtask per unmapped directory.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Unmapped Directories
${opts.coverageReport.unmappedDirs.map((d) => `- ${d}`).join("\n") || "None"}

## Unmapped Keywords
${opts.coverageReport.unmappedKeywords.map((k) => `- ${k}`).join("\n") || "None"}

## Current Keyword Count
${opts.coverageReport.currentKeywordCount}

## Current Map Definitions (from tools.ts)
\`\`\`typescript
${opts.currentToolsContent}
\`\`\`

Decompose unmapped directories into classification tasks.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model: "anthropic/claude-sonnet-4-6",
    thinking: "low",
    tools: ["read"],
    agentName: "map-updater-meta",
    cwd: opts.cwd,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if no unmapped dirs or decomposition failed, do direct update
  if (parsed.subtasks.length === 0) {
    console.error("[meta-map-updater] Decomposition failed — falling back to direct update");
    const directPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a MAP UPDATER. Produce updated map code for unmapped directories.
Output a single FILE block with updated KEYWORD_SYNONYMS, SUBSYSTEM_DIRS, and SUBSYSTEM_CANONICAL.`;

    return spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model: "anthropic/claude-sonnet-4-6",
      thinking: "low",
      tools: ["read"],
      agentName: "map-updater",
      cwd: opts.cwd,
    });
  }

  // Step 2: Delegate per-directory classification to Haiku workers
  console.error(`[meta-map-updater] Decomposed into ${parsed.subtasks.length} classification tasks`);
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const result = await askWorker({
        systemPrompt: `You are a directory classifier for a C++20 project. Classify directories into: terrain, actor, shader, or engine.
Output EXACTLY: SUBSYSTEM: [name] | KEYWORDS: [comma-separated keywords]`,
        task: st.worker_prompt,
        cwd: opts.cwd,
      });
      return result;
    }),
  );

  // Step 3: Sonnet synthesizes map updates from worker classifications
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a MAP SYNTHESIZER. Given per-directory classifications from workers,
produce the updated map code for tools.ts.

## Output Format
### FILE: .pi/extensions/orchestrator/src/tools.ts
\`\`\`typescript
// Updated KEYWORD_SYNONYMS, SUBSYSTEM_DIRS, and SUBSYSTEM_CANONICAL objects
\`\`\`

## Constraints
- Preserve ALL existing mappings
- Only ADD new entries based on worker classifications`;

  const synthesisInput = `## Worker Classifications
${workerResults.map((r, i) => `### Directory ${i + 1}\n${r}`).join("\n\n")}

## Current Map Definitions
\`\`\`typescript
${opts.currentToolsContent}
\`\`\`

Produce the updated map code incorporating these classifications.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model: "anthropic/claude-sonnet-4-6",
    thinking: "low",
    agentName: "map-updater-synthesizer",
    cwd: opts.cwd,
  });
}

// ─── Dynamic Domain Analyzer (Haiku throwaway agents) ────────────────────────

export async function spawnDomainAnalyzer(opts: {
  domain: string;
  metricData: string;
  task: string;
  cwd?: string;
}): Promise<{ domain: string; result: string }> {
  const systemPrompt = `You are a METRICS ANALYZER for the "${opts.domain}" domain of the Delve game engine.
You receive JSON metric data and must determine if the metrics are healthy.

Output EXACTLY one of:
- PASS: [brief reason]
- FAIL: [brief reason with the specific problematic metric and value]
- WARNING: [brief reason]

Be concise. One line only.`;

  const prompt = `## Task Context
${opts.task}

## ${opts.domain}.json
\`\`\`json
${opts.metricData}
\`\`\`

Analyze these metrics. Is this domain healthy?`;

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model: "anthropic/claude-haiku-4-5",
    thinking: "off",
    agentName: `analyzer-${opts.domain}`,
    cwd: opts.cwd,
  });

  return { domain: opts.domain, result };
}

// ─── Failure Detection & Escalation ──────────────────────────────────────────

export interface FailureSignal {
  failed: boolean;
  reason: string;
  category: "capability" | "tool" | "context" | "unknown";
}

export function detectAgentFailure(output: string): FailureSignal {
  // Only check the beginning of output for refusal/failure patterns
  // to avoid false positives from legitimate content deeper in the response
  const head = output.slice(0, 500);

  // Capability failures
  if (/\b(I cannot|I can't|I am unable to|I'm unable to)\b/i.test(head)) {
    return { failed: true, reason: "Agent reported inability", category: "capability" };
  }

  // Tool failures
  if (/\b(tool|command)\s+(not\s+)?(available|found|supported)\b/i.test(head)) {
    return { failed: true, reason: "Tool not available", category: "tool" };
  }
  if (/Error:\s*(ENOENT|EACCES|EPERM)\b/.test(head)) {
    return { failed: true, reason: "File system error", category: "tool" };
  }

  // Context failures
  if (/\b(context|file)\s+(missing|truncated|not found)\b/i.test(head)) {
    return { failed: true, reason: "Missing context", category: "context" };
  }

  // Explicit escalation request from worker — check full output
  const escalateMatch = output.match(/\bESCALATE:\s*(.+)/i);
  if (escalateMatch) {
    const reason = escalateMatch[1].trim();
    // Infer category from escalation reason
    if (/\b(tool|command|permission)\b/i.test(reason)) {
      return { failed: true, reason: `Escalation requested: ${reason}`, category: "tool" };
    }
    if (/\b(context|file|missing|truncated)\b/i.test(reason)) {
      return { failed: true, reason: `Escalation requested: ${reason}`, category: "context" };
    }
    return { failed: true, reason: `Escalation requested: ${reason}`, category: "capability" };
  }

  return { failed: false, reason: "", category: "unknown" };
}

export function escalate(opts: {
  failureCategory: string;
  currentModel: string;
  currentTools: string[];
}): { model: string; tools: string[] } {
  // Never auto-escalate beyond opus
  if (opts.currentModel === "anthropic/claude-opus-4-6") {
    return { model: opts.currentModel, tools: opts.currentTools };
  }

  // For Sonnet, don't upgrade model but allow tool expansion
  if (opts.currentModel === "anthropic/claude-sonnet-4-6") {
    switch (opts.failureCategory) {
      case "tool":
        return {
          model: opts.currentModel,
          tools: [...new Set([...opts.currentTools, "write", "edit", "bash"])],
        };
      case "context":
        return {
          model: opts.currentModel,
          tools: [...new Set([...opts.currentTools, "read", "bash"])],
        };
      default:
        // No model upgrade available for capability failures on Sonnet
        return { model: opts.currentModel, tools: opts.currentTools };
    }
  }

  // Haiku → Sonnet escalation
  switch (opts.failureCategory) {
    case "capability":
      return { model: "anthropic/claude-sonnet-4-6", tools: opts.currentTools };
    case "tool":
      return {
        model: "anthropic/claude-sonnet-4-6",
        tools: [...new Set([...opts.currentTools, "write", "edit", "bash"])],
      };
    case "context":
      return {
        model: opts.currentModel,
        tools: [...new Set([...opts.currentTools, "read", "bash"])],
      };
    default:
      return { model: "anthropic/claude-sonnet-4-6", tools: opts.currentTools };
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
    const currentModel = opts.model || "anthropic/claude-sonnet-4-6";
    const esc = escalate({
      failureCategory: failure.failed ? failure.category : "capability",
      currentModel,
      currentTools: opts.tools || [],
    });

    // Never auto-escalate to opus
    if (esc.model === "anthropic/claude-opus-4-6") return result;
    // No change? Don't retry
    if (esc.model === currentModel && JSON.stringify(esc.tools) === JSON.stringify(opts.tools || [])) return result;

    console.error(
      `[escalation] ${currentModel}→${esc.model} reason=${failure.failed ? failure.reason : "no FILE blocks"}`
    );

    // Record escalation in metrics for the failed attempt
    try {
      const recentMetrics = readRunMetrics(1);
      if (recentMetrics.length > 0) {
        const last = recentMetrics[recentMetrics.length - 1];
        if (last.agentName === (opts.agentName || "subagent")) {
          // Update last record to mark as escalated
          appendRunMetrics({
            ...last,
            runId: last.runId + "-escalated",
            escalated: true,
            success: false,
          });
        }
      }
    } catch { /* ignore */ }

    return spawnWithEscalation({
      ...opts,
      model: esc.model,
      tools: esc.tools.length > 0 ? esc.tools : undefined,
      depth: (opts.depth || 0) + 1,
    });
  }

  return result;
}
