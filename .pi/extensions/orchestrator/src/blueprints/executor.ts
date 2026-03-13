import { readFileSync, existsSync } from "node:fs";
import { join } from "node:path";
import type { Blueprint, BlueprintContext, BlueprintResult } from "./types.js";
import type { Phase } from "../commands/types.js";

export interface ExecuteBlueprintOptions {
  onPhaseChange?: (phase: Phase) => void;
}
import { BUILTIN_BLUEPRINTS } from "./builtin.js";
import { PHASE_HANDLERS } from "./handlers.js";
import { validatePhaseOutput } from "./validators.js";

// ─── Available Phase Names ───────────────────────────────────────────────────

export function getAvailablePhases(): string[] {
  return Object.keys(PHASE_HANDLERS);
}

// ─── Blueprint Loader ────────────────────────────────────────────────────────

export function loadBlueprint(name: string): Blueprint | null {
  // Check for custom blueprint file first
  const customPath = join(process.cwd(), `.pi/blueprints/${name}.json`);
  if (existsSync(customPath)) {
    try {
      const content = readFileSync(customPath, "utf-8");
      return JSON.parse(content) as Blueprint;
    } catch {
      // Fall through to builtins
    }
  }

  return BUILTIN_BLUEPRINTS[name] || null;
}

// ─── Blueprint Execution Engine ──────────────────────────────────────────────

export async function executeBlueprint(
  blueprint: Blueprint,
  context: BlueprintContext,
  options?: ExecuteBlueprintOptions,
): Promise<BlueprintResult> {
  const completedPhases: string[] = [];
  let failureRouted = false;

  // B2: Build named-phase map for graph traversal
  const phaseMap = new Map<string, import("./types.js").BlueprintPhase>();
  for (const phase of blueprint.phases) {
    phaseMap.set(phase.name, phase);
  }
  // Track phase order for default next-phase resolution
  const phaseOrder = blueprint.phases.map((p) => p.name);

  // Track visit counts per phase for max_retries enforcement
  const visitCounts = new Map<string, number>();

  // Start at first phase
  let currentPhaseName: string | null = phaseOrder[0] ?? null;

  try {
    while (currentPhaseName !== null) {
      // Check for cancellation
      if (context.signal?.aborted) {
        console.error("[blueprint] Aborted before phase:", currentPhaseName);
        return { ok: false, failedPhase: currentPhaseName, completedPhases, worktreePath: context.data.worktreePath };
      }

      const phase = phaseMap.get(currentPhaseName);
      if (!phase) {
        throw new Error(`Unknown phase: ${currentPhaseName}`);
      }

      // Enforce max_retries
      const visits = (visitCounts.get(phase.name) || 0) + 1;
      visitCounts.set(phase.name, visits);
      const maxRetries = phase.max_retries ?? 0;
      if (visits > maxRetries + 1) {
        // Exceeded retry limit — treat as failure, follow on_failure
        context.ctx.ui.notify(`Phase ${phase.name} exceeded max_retries (${maxRetries})`, "error");
        if (phase.on_failure) {
          failureRouted = true;
          currentPhaseName = phase.on_failure;
        } else {
          return { ok: false, failedPhase: phase.name, completedPhases, worktreePath: context.data.worktreePath };
        }
        continue;
      }

      // skip_if: skip phase if named context data key is truthy
      if (phase.skip_if && context.data[phase.skip_if]) {
        context.ctx.ui.notify(`Skipping phase ${phase.name} (skip_if: ${phase.skip_if})`, "info");
        completedPhases.push(phase.name);
        // Advance to next phase
        const idx = phaseOrder.indexOf(phase.name);
        currentPhaseName = idx >= 0 && idx + 1 < phaseOrder.length ? phaseOrder[idx + 1] : null;
        continue;
      }

      const handler = PHASE_HANDLERS[phase.handler];
      if (!handler) {
        throw new Error(`Unknown phase handler: ${phase.handler}`);
      }

      context.ctx.ui.notify(`Phase: ${phase.name}`, "info");
      options?.onPhaseChange?.(phase.name as Phase);

      const result = await handler(context);

      // Validate agentic phase output quality
      if (result.ok && phase.type === "agentic") {
        const validation = validatePhaseOutput(phase.handler, result.output, context);
        if (validation.warnings.length > 0) {
          for (const w of validation.warnings) {
            context.ctx.ui.notify(`[validation] ${phase.name}: ${w}`, "warning");
          }
        }
      }

      if (result.ok) {
        context.ctx.ui.notify(`${phase.name}: ${result.output}`, "success");
        completedPhases.push(phase.name);

        // Determine next phase
        if (phase.on_success) {
          currentPhaseName = phase.on_success;
        } else {
          // Default: next in array
          const idx = phaseOrder.indexOf(phase.name);
          currentPhaseName = idx >= 0 && idx + 1 < phaseOrder.length ? phaseOrder[idx + 1] : null;
        }
      } else {
        if (phase.optional) {
          context.ctx.ui.notify(`${phase.name} (optional): ${result.output}`, "warning");
          completedPhases.push(phase.name);
          // Optional failure: still follow on_success or advance
          if (phase.on_success) {
            currentPhaseName = phase.on_success;
          } else {
            const idx = phaseOrder.indexOf(phase.name);
            currentPhaseName = idx >= 0 && idx + 1 < phaseOrder.length ? phaseOrder[idx + 1] : null;
          }
        } else {
          context.ctx.ui.notify(`Phase ${phase.name} FAILED: ${result.output}`, "error");
          context.data.lastFailedPhase = phase.name;
          if (phase.on_failure) {
            failureRouted = true;
            currentPhaseName = phase.on_failure;
          } else {
            return { ok: false, failedPhase: phase.name, completedPhases, worktreePath: context.data.worktreePath };
          }
        }
      }
    }
  } catch (error: any) {
    const msg = error?.message || String(error);
    const stack = error?.stack || "";
    console.error(`[blueprint] Unhandled error in phase ${currentPhaseName}:\n${msg}\n${stack}`);
    context.ctx.ui.notify(`Blueprint error in ${currentPhaseName}: ${msg}`, "error");
    return {
      ok: false,
      failedPhase: currentPhaseName ?? "unknown",
      errorMessage: msg,
      completedPhases,
      worktreePath: context.data.worktreePath,
    };
  }

  return { ok: !failureRouted, completedPhases, worktreePath: context.data.worktreePath };
}
