import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  executeBlueprint,
  loadBlueprint,
  type BlueprintContext,
  type BlueprintResult,
} from "../blueprints.js";
import {
  slugify,
  elapsed,
  cleanupWorktree,
  cleanupMergedBranches,
} from "../tools.js";
import { state, getState, setState, attachAgentListeners, detachAgentListeners, recordFailure } from "./display.js";
import { acquireRunLock, releaseRunLock } from "../tools/state.js";

/** Shared runner for all blueprint-backed minion variants. */
export async function runBlueprint(
  commandName: string,
  branchPrefix: string,
  blueprintName: string,
  prompt: string,
  ctx: any,
): Promise<void> {
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
  const branch = `${branchPrefix}/${timestamp}-${slugify(prompt)}`;

  setState({
    prompt,
    branch,
    phase: "branch",
    startTime: Date.now(),
    buildFixRound: 0,
    testFixRound: 0,
  });
  attachAgentListeners(ctx, state!);

  ctx.ui.notify(`${commandName} started: ${prompt}`, "info");

  const blueprint = loadBlueprint(blueprintName)!;
  const context: BlueprintContext = {
    prompt,
    branch,
    startTime: Date.now(),
    ctx,
    data: {},
  };

  let blueprintResult: BlueprintResult | undefined;
  try {
    cleanupMergedBranches();
    blueprintResult = await executeBlueprint(blueprint, context);

    if (!blueprintResult.ok) {
      recordFailure("blueprint", `Failed at phase: ${blueprintResult.failedPhase}`);
      const s = getState(); if (s) s.phase = "failed";
    } else {
      const s = getState(); if (s) s.phase = "done";
      ctx.ui.notify(
        `${commandName} complete in ${elapsed(getState()!.startTime)}`,
        "success",
      );
    }
    detachAgentListeners();
    setState(null);
  } catch (error: any) {
    const s = getState(); if (s) s.phase = "failed";
    recordFailure("blueprint", error.message);
    ctx.ui.notify(`${commandName} error: ${error.message}`, "error");
    detachAgentListeners();
    setState(null);
  } finally {
    releaseRunLock();
    const wt = blueprintResult?.worktreePath || context.data.worktreePath;
    if (wt) {
      if (blueprintResult?.ok) {
        cleanupWorktree(wt);
      } else {
        console.error(`[${commandName}] Worktree preserved for debugging: ${wt}`);
      }
    }
  }
}

/** Guard: validate args and acquire run lock. Returns prompt or null on failure. */
export function guardArgs(
  commandName: string,
  args: string | undefined,
  ctx: any,
): { prompt: string } | null {
  if (!args || args.trim().length === 0) {
    ctx.ui.notify(`Usage: /${commandName} <task description>`, "error");
    return null;
  }
  if (state) {
    ctx.ui.notify(`A minion is already running (phase: ${state.phase}). Wait or restart.`, "error");
    return null;
  }
  const lock = acquireRunLock();
  if (!lock.acquired) {
    ctx.ui.notify(lock.reason, "error");
    return null;
  }
  return { prompt: args.trim() };
}

export function registerMinionVariantCommands(pi: ExtensionAPI) {
  // ── /minion-quick ──────────────────────────────────────────────────────
  pi.registerCommand("minion-quick", {
    description:
      "Quick pipeline: branch → implement → build → PR (no planner, no tests, no review)",
    handler: async (args, ctx) => {
      const g = guardArgs("minion-quick", args, ctx);
      if (!g) return;
      await runBlueprint("Minion-quick", "minion-quick", "quick", g.prompt, ctx);
    },
  });

  // ── /minion-refactor ───────────────────────────────────────────────────
  pi.registerCommand("minion-refactor", {
    description:
      "Refactoring pipeline: branch → implement → build → review → PR (no tests, behavior-preserving)",
    handler: async (args, ctx) => {
      const g = guardArgs("minion-refactor", args, ctx);
      if (!g) return;
      await runBlueprint("Minion-refactor", "minion-refactor", "refactor", g.prompt, ctx);
    },
  });

  // ── /minion-bugfix ─────────────────────────────────────────────────────
  pi.registerCommand("minion-bugfix", {
    description:
      "Bugfix pipeline: branch → diagnose → implement → build → test → PR (no review, fast)",
    handler: async (args, ctx) => {
      const g = guardArgs("minion-bugfix", args, ctx);
      if (!g) return;
      await runBlueprint("Minion-bugfix", "minion-bugfix", "bugfix", g.prompt, ctx);
    },
  });

  // ── /minion-shader ─────────────────────────────────────────────────────
  pi.registerCommand("minion-shader", {
    description:
      "Shader pipeline: branch → implement → build → shader-validate → PR (shader-only tasks)",
    handler: async (args, ctx) => {
      const g = guardArgs("minion-shader", args, ctx);
      if (!g) return;
      await runBlueprint("Minion-shader", "minion-shader", "shader", g.prompt, ctx);
    },
  });

}
