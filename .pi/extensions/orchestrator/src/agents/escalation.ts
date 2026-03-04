// ─── Failure Detection & Escalation ──────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { appendRunMetrics, readRunMetrics } from "./state.js";
import type { SpawnSubagentOpts, FailureSignal } from "./types.js";

export { FailureSignal };

export const MAX_ESCALATION_DEPTH = 2;

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
