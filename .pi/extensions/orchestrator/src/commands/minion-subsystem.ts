import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  askBuildFixer,
  askReviewer,
  readState,
} from "../agents.js";
import {
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
      let success = false;
      try {
        cleanupMergedBranches();

        // Branch
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state!.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath!;
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
          setPhase("fix_build", state);
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
        setPhase("commit_pr", state);
        const prResult = gitCommitAndPr({ prompt: "Domain decoupling", branch, buildOk, testsOk: false, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        success = prResult.ok;
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
        if (worktreePath) {
          if (success) {
            cleanupWorktree(worktreePath);
          } else {
            console.error(`[decouple-execute] Worktree preserved for debugging: ${worktreePath}`);
          }
        }
      }
    },
  });
}
