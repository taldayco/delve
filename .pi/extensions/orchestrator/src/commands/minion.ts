import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  askParallelPlanner,
  askMetaPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askBuildFixer,
  askTestFixer,
  getSubsystemAgent,
  parseSubtasks,
  readState,
} from "../agents.js";
import {
  slugify,
  elapsed,
  gitBranch,
  cleanupWorktree,
  runBuild,
  runTests,
  gitCommitAndPr,
  getChangedFiles,
  getDiff,
  getCodebaseContext,
  resolveSubsystems,
  cleanupMergedBranches,
  applyFileBlocks,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  getSubsystemCodebaseContext,
} from "../tools.js";
import { state, getState, setState, attachAgentListeners, detachAgentListeners, setPhase, recordFailure } from "./display.js";
import { extractFilePaths, parseReviewDecision, MAX_REVIEW_ROUNDS } from "./helpers.js";
import { acquireRunLock, releaseRunLock } from "../tools/state.js";

export function registerMinionCommand(pi: ExtensionAPI) {
  // ── /minion command ─────────────────────────────────────────────────────
  // Binary decision tree orchestrator:
  //   1. Resolve subsystems → ONE or MULTIPLE?
  //   2. Call subsystem agent(s) directly or decompose via planner
  //   3. Build pass? YES → continue, NO → fix or FAIL
  //   4. Tests pass? YES → continue, NO → fix or FAIL
  //   5. Review approve? YES → done, NO → fix + rebuild + retest (max 2 rounds)

  pi.registerCommand("minion", {
    description:
      "Run the meta-agentic development pipeline: branch → route → implement → build → test → review → PR",
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

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // ── PHASE: Branch (deterministic) ─────────────────────────────
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) {
          ctx.ui.notify(branchResult.summary, "error");
          state!.phase = "failed";
          recordFailure("branch", branchResult.summary);
          return;
        }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state!.worktreePath = wt;
        ctx.ui.notify(branchResult.summary, "success");

        // ── DECISION 1: Route by subsystem count ─────────────────────
        setPhase("plan", state);
        const subsystems = await resolveSubsystems(prompt);
        const codebaseContext = await getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        let implementation: string;

        if (subsystems.length <= 1) {
          // SINGLE SUBSYSTEM: call its agent directly (skip planner)
          const targetSubsystem = subsystems[0] || "engine";
          ctx.ui.notify(`Single subsystem: ${targetSubsystem} — calling specialist directly`, "info");
          setPhase("implement", state);
          const agent = getSubsystemAgent(targetSubsystem);
          implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });
        } else {
          // MULTIPLE SUBSYSTEMS: parallel planning — one planner per subsystem
          ctx.ui.notify(`Multiple subsystems: [${subsystems.join(", ")}] — parallel planning`, "info");

          // Build per-subsystem scoped contexts
          const subsystemContexts: Record<string, string> = {};
          for (const sub of subsystems) {
            subsystemContexts[sub] = await getSubsystemCodebaseContext(sub, wt);
          }

          const plan = await askParallelPlanner({ task: prompt, subsystemContexts, cwd: wt });
          ctx.ui.notify(`Parallel plan complete (${plan.length} chars, ${subsystems.length} subsystems)`, "success");

          // Parse per-subsystem subtasks from plan
          let subtasks = parseSubtasks(plan);

          // Retry with single-agent planner as fallback if no subtasks parsed
          if (subtasks.length === 0) {
            ctx.ui.notify("Parallel planner output missing subtask tags — falling back to single planner", "warning");
            const fallbackPlan = await askMetaPlanner({ task: prompt, codebaseContext, cwd: wt });
            subtasks = parseSubtasks(fallbackPlan);

            if (subtasks.length === 0) {
              ctx.ui.notify("Retrying with format correction", "warning");
              const correctedPlan = await askMetaPlanner({
                task: `Your previous output was:\n\n${fallbackPlan}\n\nReformat this into the EXACT required format. Each subtask MUST use this header format:\n## Subtask N [subsystem]\n- Files: ...\n- Changes: ...\n- Acceptance criteria: ...\n\nValid subsystem tags: terrain, actor, shader, engine. Do NOT use ### or em-dashes. Use ## and [tag].`,
                codebaseContext: "",
                cwd: wt,
              });
              subtasks = parseSubtasks(correctedPlan);
            }
          }

          const implementations: string[] = [];

          setPhase("implement", state);
          if (subtasks.length > 0) {
            for (const sub of subtasks) {
              ctx.ui.notify(`Implementing subtask [${sub.subsystem}]`, "info");
              const agent = getSubsystemAgent(sub.subsystem);
              const subFiles = extractFilePaths(sub.task).concat(contextFiles);
              const subImpl = await agent({ task: sub.task, files: subFiles, cwd: wt });
              implementations.push(subImpl);
            }
          } else {
            ctx.ui.notify("Planner output has no tagged subtasks — using generic implementer", "warning");
            const planFiles = extractFilePaths(plan);
            const genImpl = await askMetaImplementer({ plan, task: prompt, files: planFiles, cwd: wt });
            implementations.push(genImpl);
          }

          implementation = implementations.join("\n\n");
        }

        // ── DECISION 2: Did implementation produce file changes? ──────
        let finalImplementation = implementation;
        const fileBlockCount = (finalImplementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) {
          ctx.ui.notify("No file changes produced — retrying with explicit instruction", "warning");
          const retryAgent = getSubsystemAgent(subsystems[0] || "engine");
          finalImplementation = await retryAgent({
            task: prompt + "\n\nCRITICAL: Output ### FILE: blocks with COMPLETE file content. No explanations.",
            files: contextFiles,
            cwd: wt,
          });
          const retryCount = (finalImplementation.match(/###\s*FILE:/g) || []).length;
          if (retryCount === 0) {
            state!.phase = "failed";
            recordFailure("implement", "No file changes produced after retry");
            ctx.ui.notify("No file changes after retry — FAIL", "error");
            return;
          }
        }

        // Apply implementation changes directly by parsing FILE blocks
        const appliedCount = applyFileBlocks(finalImplementation, wt);
        ctx.ui.notify(`Implementation applied (${appliedCount} files)`, "success");

        // ── DECISION 3: Build pass? ──────────────────────────────────
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state!.buildFixRound = round + 1;
          setPhase("build", state);

          const build = runBuild(wt);

          if (build.ok) {
            ctx.ui.notify(`Build PASSED (round ${round + 1})`, "success");
            buildOk = true;
            break;
          }

          ctx.ui.notify(`Build FAILED (round ${round + 1}/${MAX_BUILD_FIX_ROUNDS})`, "warning");
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;

          setPhase("fix-build", state);
          const fix = await askBuildFixer({
            buildOutput: build.summary,
            round: round + 1,
            maxRounds: MAX_BUILD_FIX_ROUNDS,
            cwd: wt,
          });

          applyFileBlocks(fix, wt);
        }

        if (!buildOk) {
          state!.phase = "failed";
          recordFailure("build", "Build failed after max fix attempts");
          ctx.ui.notify("Build FAILED after max attempts — FAIL", "error");
          return;
        }

        // ── PHASE: Write Tests ───────────────────────────────────────
        setPhase("write-tests", state);
        const changedFiles = getChangedFiles(wt);
        const testCode = await askMetaTester({
          task: prompt,
          changedFiles,
          implementationSummary: implementation.slice(0, 2000),
          cwd: wt,
        });

        applyFileBlocks(testCode, wt);
        ctx.ui.notify("Tests written", "success");

        // ── DECISION 4: Tests pass? ──────────────────────────────────
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state!.testFixRound = round + 1;
          setPhase("test", state);

          const testResult = runTests(wt);

          if (testResult.buildOk && testResult.testsOk) {
            ctx.ui.notify(`Tests PASSED (round ${round + 1})`, "success");
            testsOk = true;
            break;
          }

          const isBuildFailure = !testResult.buildOk;
          ctx.ui.notify(
            `${isBuildFailure ? "Test build" : "Tests"} FAILED (round ${round + 1}/${MAX_TEST_FIX_ROUNDS})`,
            "warning"
          );
          if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;

          setPhase("fix-tests", state);
          const fix = await askTestFixer({
            testOutput: testResult.summary,
            round: round + 1,
            maxRounds: MAX_TEST_FIX_ROUNDS,
            isBuildFailure,
            cwd: wt,
          });

          applyFileBlocks(fix, wt);
        }

        // ── DECISION 5: Review approve? (max 2 rounds) ──────────────
        let reviewDecision: "APPROVE" | "REQUEST_CHANGES" = "REQUEST_CHANGES";

        for (let reviewRound = 0; reviewRound < MAX_REVIEW_ROUNDS; reviewRound++) {
          setPhase("review", state);
          const diff = getDiff(wt);
          const testResults = readState("test_results.json");
          const review = await askReviewer({
            task: prompt,
            diff,
            testResults: testsOk ? "All tests PASSED." : `Tests FAILED.\n${testResults}`,
            cwd: wt,
          });
          reviewDecision = parseReviewDecision(review);
          ctx.ui.notify(
            `Review round ${reviewRound + 1}: ${reviewDecision}`,
            reviewDecision === "APPROVE" ? "success" : "warning"
          );

          if (reviewDecision === "APPROVE") break;

          // Apply review fixes — re-run the subsystem agent with review feedback
          const reviewFixAgent = getSubsystemAgent(subsystems[0] || "engine");
          const reviewFix = await reviewFixAgent({
            task: `Fix the following code review issues:\n\n${review}\n\nOriginal task: ${prompt}`,
            files: getChangedFiles(wt),
            cwd: wt,
          });
          applyFileBlocks(reviewFix, wt);

          // Re-build after review fixes
          setPhase("build", state);
          let fixBuild = runBuild(wt);
          if (!fixBuild.ok) {
            const buildFix = await askBuildFixer({
              buildOutput: fixBuild.summary,
              round: 1,
              maxRounds: 1,
              cwd: wt,
            });
            applyFileBlocks(buildFix, wt);
            fixBuild = runBuild(wt);
            if (!fixBuild.ok) {
              ctx.ui.notify("Build failed during review loop", "warning");
            }
          }

          // Re-test after review fixes
          setPhase("test", state);
          const retest = runTests(wt);
          testsOk = retest.buildOk && retest.testsOk;
        }

        // ── GATE: Final build+test verification after review ─────────
        setPhase("build", state);
        const finalBuild = runBuild(wt);
        if (!finalBuild.ok) {
          state!.phase = "failed";
          recordFailure("build", "Final build check failed after review");
          ctx.ui.notify("Final build check FAILED after review — aborting pipeline", "error");
          return;
        }
        ctx.ui.notify("Final build check PASSED", "success");

        setPhase("test", state);
        const finalTests = runTests(wt);
        if (!finalTests.buildOk || !finalTests.testsOk) {
          state!.phase = "failed";
          recordFailure("test", "Final test check failed after review");
          ctx.ui.notify("Final test check FAILED after review — aborting pipeline", "error");
          return;
        }
        testsOk = true;
        buildOk = true;
        ctx.ui.notify("Final test check PASSED", "success");

        // ── PHASE: Commit + PR (deterministic) ───────────────────────
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({
          prompt,
          branch,
          buildOk,
          testsOk,
          cwd: wt,
        });

        if (prResult.ok) {
          ctx.ui.notify(prResult.summary, "success");
        } else {
          ctx.ui.notify(prResult.summary, "warning");
        }

        // ── Done ──────────────────────────────────────────────────────
        state!.phase = "done";
        ctx.ui.notify(
          `Minion complete in ${elapsed(state!.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"} | Review: ${reviewDecision}`,
          "success"
        );
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s = getState();
        if (s) s.phase = "failed";
        recordFailure(s?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });
}
