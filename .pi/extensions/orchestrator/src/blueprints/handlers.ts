import {
  askMetaPlanner,
  askParallelPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askBuildFixer,
  askTestFixer,
  askDiagnoser,
  askVerifier,
  spawnDomainAnalyzer,
  getSubsystemAgent,
  parseSubtasks,
  writeState,
  readState,
} from "../agents.js";
import {
  gitBranch,
  cleanupWorktree,
  runBuild,
  runTests,
  runMetrics,
  gitCommitAndPr,
  getChangedFiles,
  getDiff,
  getCodebaseContext,
  resolveSubsystems,
  runShaderValidation,
  applyFileBlocks,
  getSubsystemCodebaseContext,
  isVikingAvailable,
  vikingCreateSession,
  vikingAddMessage,
  vikingCommitSession,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
} from "../tools.js";
import type { BlueprintContext } from "./types.js";
import { SELF_PHASE_HANDLERS } from "./handlers-self.js";

// ─── Phase Handlers ──────────────────────────────────────────────────────────

type PhaseResult = { ok: boolean; output: string };
type PhaseHandler = (ctx: BlueprintContext) => Promise<PhaseResult>;

function extractFilePaths(text: string): string[] {
  const regex = /\b([a-zA-Z0-9_.][a-zA-Z0-9_./+-]*\/[a-zA-Z0-9_./+-]*\.[a-zA-Z]{1,10})\b/g;
  const paths = new Set<string>();
  let match;
  while ((match = regex.exec(text)) !== null) {
    const p = match[1];
    if (p.includes("://") || p.includes("..")) continue;
    paths.add(p);
  }
  if (text.includes("CMakeLists.txt")) paths.add("CMakeLists.txt");
  return Array.from(paths);
}

export const PHASE_HANDLERS: Record<string, PhaseHandler> = {
  branch: async (ctx) => {
    const result = gitBranch(ctx.branch);
    if (result.ok && result.worktreePath) {
      ctx.data.worktreePath = result.worktreePath;
    }
    return { ok: result.ok, output: result.summary };
  },

  resolve_subsystem: async (ctx) => {
    const wt = ctx.data.worktreePath;

    // Start a Viking memory session if available
    if (await isVikingAvailable()) {
      const sessionId = await vikingCreateSession(`blueprint-${Date.now()}`);
      if (sessionId) {
        ctx.data.vikingSessionId = sessionId;
        await vikingAddMessage(sessionId, `Task: ${ctx.prompt}`, "user");
      }
    }

    const subsystems = await resolveSubsystems(ctx.prompt);
    const codebaseContext = await getCodebaseContext(ctx.prompt, wt);
    const contextFiles = extractFilePaths(codebaseContext);
    ctx.data.subsystems = subsystems;
    ctx.data.contextFiles = contextFiles;
    return { ok: true, output: `Subsystems: [${subsystems.join(", ")}]` };
  },

  plan: async (ctx) => {
    const wt = ctx.data.worktreePath;
    const subsystems = ctx.data.subsystems || await resolveSubsystems(ctx.prompt);
    let plan: string;

    if (subsystems.length > 1) {
      // Parallel planning for multi-subsystem tasks
      const subsystemContexts: Record<string, string> = {};
      for (const sub of subsystems) {
        subsystemContexts[sub] = await getSubsystemCodebaseContext(sub, wt);
      }
      plan = await askParallelPlanner({ task: ctx.prompt, subsystemContexts, cwd: wt, signal: ctx.signal });
    } else {
      // Single-agent planning for single-subsystem tasks
      const codebaseContext = await getCodebaseContext(ctx.prompt, wt);
      plan = await askMetaPlanner({ task: ctx.prompt, codebaseContext, cwd: wt, signal: ctx.signal });
    }

    ctx.data.plan = plan;
    return { ok: plan.length > 0, output: plan };
  },

  implement: async (ctx) => {
    const wt = ctx.data.worktreePath;
    const subsystems = ctx.data.subsystems || await resolveSubsystems(ctx.prompt);
    const contextFiles = ctx.data.contextFiles || [];
    const taskWithDiagnosis = ctx.data.diagnosis
      ? `${ctx.prompt}\n\n## Diagnosis\n${ctx.data.diagnosis}`
      : ctx.prompt;

    let implementation: string;

    if (ctx.data.plan) {
      let subtasks = parseSubtasks(ctx.data.plan);

      // Retry planner with format correction if no subtasks parsed
      if (subtasks.length === 0) {
        const codebaseContext = await getCodebaseContext(ctx.prompt, wt);
        const correctedPlan = await askMetaPlanner({
          task: `Your previous output was:\n\n${ctx.data.plan}\n\nReformat this into the EXACT required format. Each subtask MUST use this header format:\n## Subtask N [subsystem]\n- Files: ...\n- Changes: ...\n- Acceptance criteria: ...\n\nValid subsystem tags: terrain, actor, shader, engine. Do NOT use ### or em-dashes. Use ## and [tag].`,
          codebaseContext: "",
          cwd: wt,
          signal: ctx.signal,
        });
        subtasks = parseSubtasks(correctedPlan);
      }

      if (subtasks.length > 0) {
        const implementations: string[] = [];
        for (const sub of subtasks) {
          const agent = getSubsystemAgent(sub.subsystem);
          const subFiles = extractFilePaths(sub.task).concat(contextFiles);
          const enrichedTask = `## Original Request\n${ctx.prompt}\n\n## Plan Subtask\n${sub.task}`;
          implementations.push(await agent({ task: enrichedTask, files: subFiles, cwd: wt, signal: ctx.signal }));
        }
        implementation = implementations.join("\n\n");
      } else {
        implementation = await askMetaImplementer({
          plan: ctx.data.plan,
          task: taskWithDiagnosis,
          files: contextFiles,
          subsystems: subsystems,
          cwd: wt,
          signal: ctx.signal,
        });
      }
    } else {
      const targetSubsystem = subsystems[0] || "engine";
      const agent = getSubsystemAgent(targetSubsystem);
      implementation = await agent({ task: taskWithDiagnosis, files: contextFiles, cwd: wt, signal: ctx.signal });
    }

    const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
    if (fileBlockCount === 0) {
      return { ok: false, output: "No FILE blocks produced" };
    }

    applyFileBlocks(implementation, wt);
    ctx.data.implementation = implementation;
    return { ok: true, output: `Applied ${fileBlockCount} files` };
  },

  build: async (ctx) => {
    const wt = ctx.data.worktreePath;
    let buildOk = false;
    for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
      const build = runBuild(wt);
      if (build.ok) { buildOk = true; break; }
      if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
      const fix = await askBuildFixer({
        buildOutput: build.summary,
        round: round + 1,
        maxRounds: MAX_BUILD_FIX_ROUNDS,
        cwd: wt,
        signal: ctx.signal,
      });
      applyFileBlocks(fix, wt);
    }
    ctx.data.buildOk = buildOk;
    return { ok: buildOk, output: buildOk ? "Build PASSED" : "Build FAILED after max attempts" };
  },

  write_tests: async (ctx) => {
    const wt = ctx.data.worktreePath;
    const changedFiles = getChangedFiles(wt);
    const testCode = await askMetaTester({
      task: ctx.prompt,
      changedFiles,
      implementationSummary: (ctx.data.implementation || "").slice(0, 2000),
      cwd: wt,
      signal: ctx.signal,
    });
    applyFileBlocks(testCode, wt);
    return { ok: true, output: "Tests written" };
  },

  test: async (ctx) => {
    const wt = ctx.data.worktreePath;
    let testsOk = false;
    for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
      const testResult = runTests(wt);
      if (testResult.buildOk && testResult.testsOk) { testsOk = true; break; }
      if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;
      const fix = await askTestFixer({
        testOutput: testResult.summary,
        round: round + 1,
        maxRounds: MAX_TEST_FIX_ROUNDS,
        isBuildFailure: !testResult.buildOk,
        cwd: wt,
        signal: ctx.signal,
      });
      applyFileBlocks(fix, wt);
    }
    ctx.data.testsOk = testsOk;
    return { ok: testsOk, output: testsOk ? "Tests PASSED" : "Tests FAILED" };
  },

  review: async (ctx) => {
    const wt = ctx.data.worktreePath;
    const diff = getDiff(wt);
    const testResults = readState("test_results.json");
    const review = await askReviewer({
      task: ctx.prompt,
      diff,
      testResults: ctx.data.testsOk ? "All tests PASSED." : `Tests FAILED.\n${testResults}`,
      cwd: wt,
      signal: ctx.signal,
    });

    const decision = /\bAPPROVE\b/.test(review) && !/\bREQUEST_CHANGES\b/.test(review)
      ? "APPROVE" : "REQUEST_CHANGES";
    ctx.data.reviewDecision = decision;
    return { ok: decision === "APPROVE", output: `Review: ${decision}` };
  },

  commit_pr: async (ctx) => {
    const wt = ctx.data.worktreePath;
    // Pre-flight gate: verify build+tests pass before shipping
    const preflight_build = runBuild(wt);
    if (!preflight_build.ok) {
      return { ok: false, output: "Pre-commit build check FAILED — aborting" };
    }
    const preflight_tests = runTests(wt);
    if (!preflight_tests.buildOk || !preflight_tests.testsOk) {
      return { ok: false, output: "Pre-commit test check FAILED — aborting" };
    }
    ctx.data.buildOk = true;
    ctx.data.testsOk = true;

    const result = gitCommitAndPr({
      prompt: ctx.prompt,
      branch: ctx.branch,
      buildOk: true,
      testsOk: true,
      cwd: wt,
    });
    return { ok: result.ok, output: result.summary };
  },

  diagnose: async (ctx) => {
    const testResult = runTests(ctx.data.worktreePath);
    const { execSync } = require("node:child_process");
    let recentCommits = "";
    try {
      recentCommits = execSync("git log --oneline -10 2>/dev/null", { encoding: "utf-8", cwd: ctx.data.worktreePath || process.cwd() });
    } catch { /* ignore */ }

    const diagnosis = await askDiagnoser({
      task: ctx.prompt,
      testOutput: testResult.summary,
      recentCommits,
      cwd: ctx.data.worktreePath,
      signal: ctx.signal,
    });
    ctx.data.diagnosis = diagnosis;
    writeState("diagnosis.md", diagnosis);
    return { ok: diagnosis.length > 0, output: "Diagnosis complete" };
  },

  shader_validate: async (ctx) => {
    const result = runShaderValidation(ctx.data.worktreePath);
    return { ok: result.ok, output: result.ok ? "Shader validation PASSED" : result.summary };
  },

  verify: async (ctx) => {
    const wt = ctx.data.worktreePath;

    // Run headless metrics pipeline
    const metrics = runMetrics(wt);
    if (!metrics.ok) {
      return { ok: false, output: metrics.summary };
    }

    if (metrics.domains.length === 0) {
      return { ok: false, output: "No metric domains emitted" };
    }

    // Spawn throwaway Haiku analyzers per domain in parallel
    const { readFileSync, existsSync } = require("node:fs");
    const { join } = require("node:path");

    const analyzerPromises = metrics.domains.map((domain: string) => {
      const filePath = join(metrics.outputDir, `${domain}.json`);
      if (!existsSync(filePath)) return null;
      const metricData = readFileSync(filePath, "utf-8");
      return spawnDomainAnalyzer({
        domain,
        metricData,
        task: ctx.prompt,
      });
    }).filter(Boolean);

    const results = await Promise.all(analyzerPromises);

    // Aggregate results
    const summary: string[] = [];
    let allPass = true;

    for (const r of results) {
      if (!r) continue;
      const passed = /^PASS/i.test(r.result.trim());
      const warned = /^WARNING/i.test(r.result.trim());
      if (!passed && !warned) allPass = false;
      summary.push(`${r.domain}: ${r.result.trim()}`);
    }

    const verificationOutput = summary.join("\n");
    writeState("verification.md", verificationOutput);

    return {
      ok: allPass,
      output: allPass
        ? `Verification PASSED (${metrics.domains.length} domains)`
        : `Verification FAILED:\n${verificationOutput}`,
    };
  },

  memory_iterate: async (ctx) => {
    const sessionId = ctx.data.vikingSessionId;
    if (!sessionId || !(await isVikingAvailable())) {
      return { ok: true, output: "Viking unavailable — memory phase skipped" };
    }

    // Record task outcome as a session message
    const subsystems = ctx.data.subsystems || [];
    const completedPhases = Object.keys(ctx.data).filter(
      (k) => ctx.data[k] === true || (typeof ctx.data[k] === "string" && ctx.data[k].length > 0),
    );
    const outcome = ctx.data.buildOk && ctx.data.testsOk !== false ? "success" : "partial";

    const summary = [
      `## Run Summary`,
      `- Subsystems: [${subsystems.join(", ")}]`,
      `- Outcome: ${outcome}`,
      `- Build: ${ctx.data.buildOk ? "PASS" : "FAIL/SKIP"}`,
      `- Tests: ${ctx.data.testsOk === true ? "PASS" : ctx.data.testsOk === false ? "FAIL" : "SKIP"}`,
      `- Data keys: ${completedPhases.join(", ")}`,
    ].join("\n");

    await vikingAddMessage(sessionId, summary, "assistant");
    const committed = await vikingCommitSession(sessionId);

    return {
      ok: true,
      output: committed ? "Memory committed" : "Memory commit failed (non-fatal)",
    };
  },

  // ─── B4+: Self-management handlers (merged from handlers-self.ts) ──────────
  ...SELF_PHASE_HANDLERS,
};

