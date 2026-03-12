import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  askBuildFixer,
  askTestFixer,
  askReviewer,
  askDiagnoser,
  getSubsystemAgent,
  askShaderAgent,
  writeState,
} from "../agents.js";
import {
  slugify,
  elapsed,
  gitBranch,
  cleanupWorktree,
  runBuild,
  runTests,
  gitCommitAndPr,
  getDiff,
  getCodebaseContext,
  resolveSubsystems,
  cleanupMergedBranches,
  applyFileBlocks,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  runShaderValidation,
} from "../tools.js";
import { state, getState, setState, attachAgentListeners, detachAgentListeners, setPhase, recordFailure } from "./display.js";
import { extractFilePaths, parseReviewDecision } from "./helpers.js";
import { acquireRunLock, releaseRunLock } from "../tools/state.js";

export function registerMinionVariantCommands(pi: ExtensionAPI) {
  // ── /minion-quick command ─────────────────────────────────────────────
  // Lightweight pipeline: branch → implement → build → fix → commit+PR
  // No planner, no tests, no review. For simple tasks.

  pi.registerCommand("minion-quick", {
    description:
      "Quick pipeline for simple tasks: branch → implement → build → PR (no planner, no tests, no review)",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion-quick <task description>", "error");
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
      const branch = `minion-quick/${timestamp}-${slugify(prompt)}`;

      setState({
        prompt,
        branch,
        phase: "branch",
        startTime: Date.now(),
        buildFixRound: 0,
        testFixRound: 0,
      });
      attachAgentListeners(ctx, state!);

      ctx.ui.notify(`Minion-quick started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // ── PHASE: Branch ────────────────────────────────────────────
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

        // ── PHASE: Implement (single agent, no planner) ──────────────
        setPhase("implement", state);
        const subsystems = await resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = await getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);

        ctx.ui.notify(`Subsystem: ${targetSubsystem}`, "info");
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) {
          state!.phase = "failed";
          recordFailure("implement", "No file changes produced");
          ctx.ui.notify("No file changes produced — FAIL", "error");
          return;
        }

        const appliedCount = applyFileBlocks(implementation, wt);
        ctx.ui.notify(`Applied ${appliedCount} files`, "success");

        // ── PHASE: Build + fix (max 3 rounds) ───────────────────────
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

        // ── PHASE: Commit + PR ───────────────────────────────────────
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({
          prompt,
          branch,
          buildOk: true,
          testsOk: false,
          cwd: wt,
        });

        if (prResult.ok) {
          ctx.ui.notify(prResult.summary, "success");
        } else {
          ctx.ui.notify(prResult.summary, "warning");
        }

        state!.phase = "done";
        ctx.ui.notify(
          `Minion-quick complete in ${elapsed(state!.startTime)}. Build: PASS`,
          "success"
        );
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s0 = getState(); if (s0) s0.phase = "failed";
        recordFailure(getState()?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-quick error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });

  // ── /minion-refactor command ────────────────────────────────────────────
  // Pipeline: branch → implement → build → fix → review → PR
  // No test-writing (behavior preservation). Reviewer rejects if behavior changes.

  pi.registerCommand("minion-refactor", {
    description:
      "Refactoring pipeline: branch → implement → build → review → PR (no tests, behavior-preserving)",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion-refactor <refactoring description>", "error");
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
      const branch = `minion-refactor/${timestamp}-${slugify(prompt)}`;

      setState({ prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 });
      attachAgentListeners(ctx, state!);
      ctx.ui.notify(`Minion-refactor started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state!.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state!.worktreePath = wt;

        // Implement
        setPhase("implement", state);
        const subsystems = await resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = await getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state!.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state!.buildFixRound = round + 1;
          setPhase("build", state);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", state);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state!.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Review (with extra constraint: reject if behavior changes)
        setPhase("review", state);
        const diff = getDiff(wt);
        const review = await askReviewer({
          task: prompt + "\n\nCRITICAL REVIEW CONSTRAINT: Reject if observable behavior changes. This is a refactoring — behavior must be preserved.",
          diff,
          testResults: "No tests run (refactoring pipeline).",
          cwd: wt,
        });
        const reviewDecision = parseReviewDecision(review);
        ctx.ui.notify(`Review: ${reviewDecision}`, reviewDecision === "APPROVE" ? "success" : "warning");

        // Commit + PR
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: false, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state!.phase = "done";
        ctx.ui.notify(`Minion-refactor complete in ${elapsed(state!.startTime)}`, "success");
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s1 = getState(); if (s1) s1.phase = "failed";
        recordFailure(getState()?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-refactor error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });

  // ── /minion-bugfix command ─────────────────────────────────────────────
  // Pipeline: branch → diagnose → implement → build → test → PR
  // Diagnose phase runs tests first, collects failures, identifies root cause.

  pi.registerCommand("minion-bugfix", {
    description:
      "Bugfix pipeline: branch → diagnose → implement → build → test → PR (no review, fast)",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion-bugfix <bug description>", "error");
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
      const branch = `minion-bugfix/${timestamp}-${slugify(prompt)}`;

      setState({ prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 });
      attachAgentListeners(ctx, state!);
      ctx.ui.notify(`Minion-bugfix started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state!.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state!.worktreePath = wt;

        // Diagnose: run tests first to collect failure output
        setPhase("diagnose", state);
        const initialTests = runTests(wt);
        const { execSync } = require("node:child_process");
        let recentCommits = "";
        try {
          recentCommits = execSync("git log --oneline -10 2>/dev/null", { encoding: "utf-8", cwd: wt });
        } catch { /* ignore */ }

        const diagnosis = await askDiagnoser({
          task: prompt,
          testOutput: initialTests.summary,
          recentCommits,
          cwd: wt,
        });
        ctx.ui.notify("Diagnosis complete", "success");
        writeState("diagnosis.md", diagnosis);

        // Implement with diagnosis context
        setPhase("implement", state);
        const subsystems = await resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = await getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({
          task: `${prompt}\n\n## Diagnosis\n${diagnosis}`,
          files: contextFiles,
          cwd: wt,
        });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state!.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state!.buildFixRound = round + 1;
          setPhase("build", state);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", state);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state!.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Test (no test-writing — bugfixes should make existing tests pass)
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state!.testFixRound = round + 1;
          setPhase("test", state);
          const testResult = runTests(wt);
          if (testResult.buildOk && testResult.testsOk) { testsOk = true; break; }
          if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;
          setPhase("fix-tests", state);
          const fix = await askTestFixer({
            testOutput: testResult.summary,
            round: round + 1,
            maxRounds: MAX_TEST_FIX_ROUNDS,
            isBuildFailure: !testResult.buildOk,
            cwd: wt,
          });
          applyFileBlocks(fix, wt);
        }

        // Commit + PR (skip review for bugfixes)
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state!.phase = "done";
        ctx.ui.notify(
          `Minion-bugfix complete in ${elapsed(state!.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s2 = getState(); if (s2) s2.phase = "failed";
        recordFailure(getState()?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-bugfix error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });

  // ── /minion-shader command ─────────────────────────────────────────────
  // Pipeline: branch → implement → build → shader-validate → PR
  // Forces subsystem to "shader". Uses glslc validation instead of test suite.

  pi.registerCommand("minion-shader", {
    description:
      "Shader pipeline: branch → implement → build → shader-validate → PR (shader-only tasks)",
    handler: async (args, ctx) => {
      if (!args || args.trim().length === 0) {
        ctx.ui.notify("Usage: /minion-shader <shader task description>", "error");
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
      const branch = `minion-shader/${timestamp}-${slugify(prompt)}`;

      setState({ prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 });
      attachAgentListeners(ctx, state!);
      ctx.ui.notify(`Minion-shader started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", state);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state!.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state!.worktreePath = wt;

        // Implement (force shader subsystem)
        setPhase("implement", state);
        const codebaseContext = await getCodebaseContext("shader " + prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const implementation = await askShaderAgent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state!.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state!.buildFixRound = round + 1;
          setPhase("build", state);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", state);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state!.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Shader validation via glslc
        setPhase("shader-validate", state);
        const shaderResult = runShaderValidation(wt);
        ctx.ui.notify(
          shaderResult.ok ? "Shader validation PASSED" : `Shader validation FAILED:\n${shaderResult.summary}`,
          shaderResult.ok ? "success" : "warning"
        );

        // Commit + PR
        setPhase("commit-pr", state);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: shaderResult.ok, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state!.phase = "done";
        ctx.ui.notify(
          `Minion-shader complete in ${elapsed(state!.startTime)}. Build: PASS | Shaders: ${shaderResult.ok ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        setState(null);
      } catch (error: any) {
        const s3 = getState(); if (s3) s3.phase = "failed";
        recordFailure(getState()?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-shader error: ${error.message}`, "error");
        detachAgentListeners();
        setState(null);
      } finally {
        releaseRunLock();
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });
}
