import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  executeBlueprint,
  loadBlueprint,
  validateBlueprint,
  getAvailablePhases,
  type BlueprintContext,
  type BlueprintResult,
} from "../blueprints.js";
import {
  askBuildFixer,
  askReviewer,
  askBlueprintGenerator,
  writeState,
  readState,
} from "../agents.js";
import {
  slugify,
  elapsed,
  gitBranch,
  cleanupWorktree,
  runBuild,
  gitCommitAndPr,
  getDiff,
  cleanupMergedBranches,
  applyFileBlocks,
  MAX_BUILD_FIX_ROUNDS,
} from "../tools.js";
import { state, getState, setState, attachAgentListeners, detachAgentListeners, setPhase, recordFailure } from "./display.js";
import { parseReviewDecision } from "./helpers.js";
import { acquireRunLock, releaseRunLock } from "../tools/state.js";

export function registerSubsystemCommands(pi: ExtensionAPI) {
  // ── /minion-meta command ─────────────────────────────────────────────
  // Adaptive pipeline: AI generates the blueprint from the task description.

  pi.registerCommand("minion-meta", {
    description:
      "Adaptive pipeline: AI generates an optimal pipeline blueprint for the task, then executes it",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion-meta <task description>", "error");
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
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-meta/${timestamp}-${slugify(prompt)}`;

      setState({ prompt, branch, phase: "plan", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 });
      attachAgentListeners(ctx, state!);
      ctx.ui.notify(`Minion-meta started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      let blueprintResult: BlueprintResult | undefined;
      const context: BlueprintContext = {
        prompt,
        branch,
        startTime: Date.now(),
        ctx,
        data: {},
      };

      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Generate blueprint via AI
        setPhase("plan", state);
        ctx.ui.notify("Generating pipeline blueprint...", "info");

        const availablePhases = getAvailablePhases();
        const blueprintOutput = await askBlueprintGenerator({
          task: prompt,
          availablePhases,
        });

        // Parse generated blueprint
        let blueprint: any = null;
        try {
          const jsonMatch = blueprintOutput.match(/```json\s*([\s\S]*?)```/);
          blueprint = JSON.parse(jsonMatch ? jsonMatch[1].trim() : blueprintOutput);
        } catch {
          ctx.ui.notify("Failed to parse generated blueprint — falling back to 'full'", "warning");
        }

        // Validate or fall back
        if (!blueprint || !validateBlueprint(blueprint)) {
          ctx.ui.notify("Blueprint validation failed — falling back to 'full'", "warning");
          blueprint = loadBlueprint("full");
        }

        ctx.ui.notify(
          `Blueprint: ${blueprint.name} (${blueprint.phases.length} phases)`,
          "success"
        );
        writeState("blueprint.json", JSON.stringify(blueprint, null, 2));

        // Execute the blueprint
        blueprintResult = await executeBlueprint(blueprint, context);
        worktreePath = blueprintResult.worktreePath;

        if (!blueprintResult.ok) {
          recordFailure("blueprint", `Failed at phase: ${blueprintResult.failedPhase}`);
          const sMeta = getState(); if (sMeta) sMeta.phase = "failed";
        } else {
          const sMetaDone = getState();
          if (sMetaDone) sMetaDone.phase = "done";
          ctx.ui.notify(
            `Minion-meta complete in ${elapsed(getState()!.startTime)}. Blueprint: ${blueprint.name}`,
            "success"
          );
        }
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        worktreePath = worktreePath || context.data.worktreePath;
        const sErr = getState(); if (sErr) sErr.phase = "failed";
        recordFailure("blueprint", error.message);
        ctx.ui.notify(`Minion-meta error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) {
          if (blueprintResult?.ok) {
            cleanupWorktree(worktreePath);
          } else {
            console.error(`[minion-meta] Worktree preserved for debugging: ${worktreePath}`);
          }
        }
      }
    },
  });

  // ── /minion-decouple-execute command ────────────────────────────────────

  pi.registerCommand("minion-decouple-execute", {
    description: "Execute a previously generated decouple proposal (reads from .pi/state/decouple_proposal.md)",
    handler: async (_args, ctx) => {
      const proposal = readState("decouple_proposal.md");
      if (!proposal || proposal.trim().length === 0) {
        ctx.ui.notify("No decouple proposal found. Run /minion-decouple first.", "error");
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

      const prompt = `Apply the following domain decoupling proposal:\n\n${proposal}`;
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-decouple/${timestamp}`;

      setState({ prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 });
      attachAgentListeners(ctx, state!);
      ctx.ui.notify("Executing decouple proposal...", "info");

      let worktreePath: string | undefined;
      try {
        cleanupMergedBranches();

        // Branch
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state!.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state!.worktreePath = wt;

        // Apply proposal file blocks
        setPhase("implement", state);
        const appliedCount = applyFileBlocks(proposal, wt);
        ctx.ui.notify(`Applied ${appliedCount} files from proposal`, "success");

        // Build
        setPhase("build", state);
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state!.buildFixRound = round + 1;
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", state);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }

        if (!buildOk) {
          state!.phase = "failed";
          recordFailure("build", "Build failed after max fix attempts");
          ctx.ui.notify("Build FAILED after max attempts — decouple aborted", "error");
          return;
        }

        // Review
        setPhase("review", state);
        const diff = getDiff(wt);
        const review = await askReviewer({
          task: "Domain decoupling refactor",
          diff,
          testResults: "No tests run (structural refactor).",
          cwd: wt,
        });
        const reviewDecision = parseReviewDecision(review);
        ctx.ui.notify(`Review: ${reviewDecision}`, reviewDecision === "APPROVE" ? "success" : "warning");

        // Commit + PR
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({ prompt: "Domain decoupling", branch, buildOk, testsOk: false, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state!.phase = "done";
        ctx.ui.notify(`Decouple-execute complete in ${elapsed(state!.startTime)}`, "success");
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s4 = getState(); if (s4) s4.phase = "failed";
        recordFailure(getState()?.phase || "unknown", error.message);
        ctx.ui.notify(`Decouple-execute error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });
}
