import type { Blueprint, BlueprintContext } from "./types.js";
import { PHASE_HANDLERS } from "./handlers.js";

// ─── Phase Output Validation ─────────────────────────────────────────────────

interface ValidationResult {
  valid: boolean;
  warnings: string[];
}

const PHASE_VALIDATORS: Record<string, (output: string, ctx: BlueprintContext) => ValidationResult> = {
  plan: (output) => {
    const warnings: string[] = [];
    if (output.length < 50) warnings.push("Plan is suspiciously short");
    if (!/##?\s*(subtask|step|task)/i.test(output) && !/\d+\.\s/m.test(output)) {
      warnings.push("Plan lacks structured subtasks (no ## Subtask or numbered steps found)");
    }
    // Check for implementation-ready detail (function signatures or file paths)
    const subtaskBodies = output.split(/##?\s*Subtask/i).slice(1);
    for (const body of subtaskBodies) {
      if (!/\b(void|bool|int|float|double|std::|glm::|auto)\b/.test(body) &&
          !/\w+\s*\([^)]*\)/.test(body)) {
        warnings.push("Plan subtask lacks function signatures — may cause implementation failure");
        break;
      }
    }
    return { valid: warnings.length === 0, warnings };
  },

  // Note: implement and write_tests validators removed — handlers already check
  // for FILE blocks and return ok:false. Validators received the summary string
  // (e.g., "Applied 3 files"), not the raw output, causing false warnings.

  review: (output) => {
    const warnings: string[] = [];
    const hasVerdict = /\b(APPROVE|REQUEST_CHANGES)\b/.test(output);
    if (!hasVerdict) warnings.push("Review lacks APPROVE/REQUEST_CHANGES verdict");
    if (output.length < 30) warnings.push("Review is suspiciously short");
    return { valid: hasVerdict, warnings };
  },

  diagnose: (output) => {
    const warnings: string[] = [];
    if (output.length < 50) warnings.push("Diagnosis is suspiciously short");
    return { valid: output.length >= 50, warnings };
  },
};

export function validatePhaseOutput(handler: string, output: string, ctx: BlueprintContext): ValidationResult {
  const validator = PHASE_VALIDATORS[handler];
  if (!validator) return { valid: true, warnings: [] };
  return validator(output, ctx);
}

// ─── Blueprint Validator ─────────────────────────────────────────────────────

export function validateBlueprint(blueprint: any): blueprint is Blueprint {
  if (!blueprint || !blueprint.name || !Array.isArray(blueprint.phases)) return false;

  const phaseNames = new Set(blueprint.phases.map((p: any) => p.name));

  for (const phase of blueprint.phases) {
    if (!phase.handler || !PHASE_HANDLERS[phase.handler]) return false;
    if (phase.on_failure && !phaseNames.has(phase.on_failure)) return false;
    if (phase.on_success && !phaseNames.has(phase.on_success)) return false;
  }

  return true;
}
