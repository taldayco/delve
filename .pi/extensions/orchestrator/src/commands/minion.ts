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

export function registerMinionCommand(pi: ExtensionAPI) {
  pi.registerCommand("minion", {
    description:
      "Run the meta-agentic development pipeline: branch → plan → implement → build → test → review → PR",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion <feature description>", "error");
        return;
      }

      if (state) {
        ctx.ui.notify(`A minion is already running (phase: ${state.phase}). Wait or restart.`, "error");
        return;
      }

      const lock = acquireRunLock();
      if (!lock.acquired) {
        ctx.ui.notify(lock.reason, "error");
        return;
      }

      const prompt = args.trim();
      const timestamp = new Date()
        .toISOString()
        .replace(/[:.]/g, "-")
        .slice(0, 19);
      const branch = `minion/${timestamp}-${slugify(prompt)}`;

      setState({
        prompt,
        branch,
        phase: "branch",
        startTime: Date.now(),
        buildFixRound: 0,
        testFixRound: 0,
      });
      attachAgentListeners(ctx, state!);

      ctx.ui.notify(`Minion started: ${prompt}`, "info");

      const blueprint = loadBlueprint("branching-full")!;
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
            `Minion complete in ${elapsed(getState()!.startTime)}`,
            "success",
          );
        }
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s = getState(); if (s) s.phase = "failed";
        recordFailure("blueprint", error.message);
        ctx.ui.notify(`Minion error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        const wt = blueprintResult?.worktreePath || context.data.worktreePath;
        if (wt) {
          if (blueprintResult?.ok) {
            cleanupWorktree(wt);
          } else {
            console.error(`[minion] Worktree preserved for debugging: ${wt}`);
          }
        }
      }
    },
  });
}
