// ─── Pi Subagent Spawning ──────────────────────────────────────────────────

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as os from "node:os";
import { join } from "node:path";
import { agentEvents } from "./events.js";
import { PROJECT_ROOT } from "./config.js";
import { appendRunMetrics, incrementMetricsRunCounter } from "./state.js";
import type { SpawnSubagentOpts, TruncateResult } from "./types.js";

// ─── Context Budget Tracking ──────────────────────────────────────────────

// Approximate tokens as chars/4 (rough but practical).
// Model context windows (conservative estimates for usable input):
export const MODEL_CONTEXT_LIMITS: Record<string, number> = {
  "anthropic/claude-opus-4-6": 200_000,
  "anthropic/claude-sonnet-4-6": 200_000,
  "anthropic/claude-haiku-4-5": 200_000,
};

// Target: no agent should use more than 40% of its context window
export const CONTEXT_BUDGET_RATIO = 0.4;

/**
 * Estimate token count from a string (rough: ~4 chars per token for English/code).
 */
export function estimateTokens(text: string): number {
  return Math.ceil(text.length / 4);
}

/**
 * Section-aware truncation: keeps first (## Task) and last (## Constraints) sections,
 * drops middle sections (## File Contents) by size, largest first.
 */
export function smartTruncate(text: string, maxChars: number): TruncateResult {
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

export function enforceContextBudget(
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

// ─── Silent Shell Execution ────────────────────────────────────────────────

export function silentShell(
  cmd: string,
  tailLines: number,
  cwd: string,
): { ok: boolean; summary: string; full: string } {
  const { execSync } = require("node:child_process");
  try {
    const stdout: string = execSync(cmd, {
      cwd,
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

// ─── Subagent Spawn ────────────────────────────────────────────────────────

/**
 * Spawn a pi subagent process with isolated context.
 * Uses --mode json to capture structured output.
 * System prompt is written to a temp file and passed via --append-system-prompt.
 * Enforces context budget before spawning.
 * A5: Exponential backoff on spawn failures: 500ms / 1500ms / 3000ms.
 */
export async function spawnSubagent(opts: SpawnSubagentOpts): Promise<string> {
  const RETRY_DELAYS = [500, 1500, 3000];
  let lastErr: any;

  for (let attempt = 0; attempt <= RETRY_DELAYS.length; attempt++) {
    try {
      return await spawnSubagentOnce(opts);
    } catch (err: any) {
      lastErr = err;
      const isSpawnError = err?.code === "ENOENT" || err?.code === "EACCES";
      if (!isSpawnError || attempt >= RETRY_DELAYS.length) break;

      const delay = RETRY_DELAYS[attempt];
      const agentName = opts.agentName || "subagent";
      agentEvents.emit("agent:retry", { name: agentName, error: err.message, attempt: attempt + 1, delayMs: delay });
      console.error(`[spawn-backoff] ${agentName}: attempt ${attempt + 1} failed, retrying in ${delay}ms…`);
      await new Promise((r) => setTimeout(r, delay));
    }
  }

  throw lastErr;
}

export async function spawnSubagentOnce(opts: SpawnSubagentOpts): Promise<string> {
  let tmpDir: string | null = null;
  let tmpFile: string | null = null;

  // Enforce context budget before spawning
  const model = opts.model || "anthropic/claude-sonnet-4-6";
  const budget = enforceContextBudget(opts.systemPrompt, opts.prompt, model);
  const effectiveSystemPrompt = budget.systemPrompt;
  const effectivePrompt = budget.userPrompt;

  const agentName = opts.agentName || "subagent";
  const metricsStartTime = Date.now();
  const runId = `${agentName}-${incrementMetricsRunCounter()}-${Date.now()}`;

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

    // suppress unused variable warning
    void success;
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
