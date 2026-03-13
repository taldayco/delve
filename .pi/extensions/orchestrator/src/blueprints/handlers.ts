import {
  askMetaPlanner,
  askParallelPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askBuildFixer,
  askTestFixer,
  askDiagnoser,
  classifyBuildError,
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
  getCodebaseContextBudgeted,
  resolveSubsystems,
  runShaderValidation,
  applyFileBlocks,
  getSubsystemCodebaseContext,
  isVikingAvailable,
  requireViking,
  vikingSearch,
  vikingCreateSession,
  vikingAddMessage,
  vikingCommitSession,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  shell,
} from "../tools.js";
import type { BlueprintContext } from "./types.js";
import { SELF_PHASE_HANDLERS } from "./handlers-self.js";

// ─── Visual Test Helpers ──────────────────────────────────────────────────────

const VISUAL_TEST_SIMILARITY_THRESHOLD = 99;
const VISUAL_TEST_BASELINE = ".pi/baselines/visual_baseline.png";
const VISUAL_TEST_CAPTURE = "/tmp/delve-visual-test-capture.png";
const VISUAL_TEST_SOCKET = "/tmp/delve-visual-test.sock";
const VISUAL_TEST_SETTLE_MS = 2000; // wait for scene to render

/** Send a command to the running app via Unix socket IPC. */
function agentCommand(
  socketPath: string,
  cmd: string,
  params: Record<string, any> = {},
): Promise<{ ok: boolean; data?: any; error?: string }> {
  const { createConnection } = require("node:net");
  return new Promise((resolve) => {
    const socket = createConnection({ path: socketPath }, () => {
      socket.write(JSON.stringify({ cmd, params }) + "\n");
    });
    let buffer = "";
    socket.on("data", (data: Buffer) => {
      buffer += data.toString();
      const nl = buffer.indexOf("\n");
      if (nl >= 0) {
        socket.end();
        try { resolve(JSON.parse(buffer.substring(0, nl))); }
        catch { resolve({ ok: false, error: "Parse error" }); }
      }
    });
    socket.on("error", (err: Error) => resolve({ ok: false, error: err.message }));
    socket.setTimeout(10_000, () => { socket.end(); resolve({ ok: false, error: "Timeout" }); });
  });
}

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
    const wt = ctx.data.worktreePath!;

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
    const wt = ctx.data.worktreePath!;
    const subsystems = ctx.data.subsystems || await resolveSubsystems(ctx.prompt);
    let plan: string;

    if (subsystems.length > 1) {
      // Parallel planning for multi-subsystem tasks
      const subsystemContexts: Record<string, string> = {};
      for (const sub of subsystems) {
        let subCtx = await getSubsystemCodebaseContext(sub, wt);
        // If context lacks function-level detail, supplement with L2 header reads
        const hasSignatures = /\b\w+\s+\w+\s*\(/.test(subCtx);
        if (!hasSignatures && await isVikingAvailable()) {
          const { vikingSearch: vs, vikingRead: vr, SUBSYSTEM_VIKING_URI: uris } = await import("../tools/viking.js");
          const uri = uris[sub];
          if (uri) {
            const headers = await vs(`${sub} header declarations`, "L2", uri, 3);
            for (const h of headers) {
              if (h.uri.endsWith(".h") || h.uri.includes(".h/")) {
                const content = await vr(h.uri);
                if (content && content.trim().length > 0) {
                  subCtx += `\n\n### ${h.uri} (L2 Full)\n${content.slice(0, 8000)}`;
                }
              }
            }
          }
        }
        subsystemContexts[sub] = subCtx;
      }
      plan = await askParallelPlanner({
        task: ctx.prompt, subsystemContexts, cwd: wt, signal: ctx.signal,
        tools: ["code_list_subsystem", "code_find_definition", "code_find_usages", "git_status", "git_diff_from_main", "project_overview", "viking_search", "viking_read"],
      });
    } else {
      // Single-agent planning — use budgeted context (L1 → L0 → keyword)
      const codebaseContext = await getCodebaseContextBudgeted(
        ctx.prompt,
        "anthropic/claude-sonnet-4-6",
        wt,
      );
      plan = await askMetaPlanner({
        task: ctx.prompt, codebaseContext, cwd: wt, signal: ctx.signal,
        tools: ["code_list_subsystem", "code_find_definition", "code_find_usages", "git_status", "git_diff_from_main", "project_overview", "viking_search", "viking_read"],
      });
    }

    ctx.data.plan = plan;
    return { ok: plan.length > 0, output: plan };
  },

  implement: async (ctx) => {
    const wt = ctx.data.worktreePath!;
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
    const wt = ctx.data.worktreePath!;
    let buildOk = false;
    for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
      const build = runBuild(wt);
      if (build.ok) { buildOk = true; break; }

      // Classify error before attempting fix
      const classification = await classifyBuildError({
        buildOutput: build.summary,
        cwd: wt,
        signal: ctx.signal,
      });
      ctx.data.errorClassification = classification;

      // Architectural errors need re-planning, not fix loops
      if (classification.severity === "architectural") {
        ctx.data.buildFailureReason = `Architectural: ${classification.summary}`;
        return { ok: false, output: `Build FAILED (architectural): ${classification.summary}` };
      }

      // Structural errors get at most 1 fix round
      if (classification.severity === "structural" && round >= 1) {
        ctx.data.buildFailureReason = `Structural (exceeded fix budget): ${classification.summary}`;
        return { ok: false, output: `Build FAILED (structural): ${classification.summary}` };
      }

      if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
      const fix = await askBuildFixer({
        buildOutput: build.summary,
        round: round + 1,
        maxRounds: MAX_BUILD_FIX_ROUNDS,
        tools: ["build_configure", "build_compile", "build_clean", "code_find_definition", "code_find_usages", "viking_search", "viking_read"],
        cwd: wt,
        signal: ctx.signal,
      });
      applyFileBlocks(fix, wt);
    }
    ctx.data.buildOk = buildOk;
    return { ok: buildOk, output: buildOk ? "Build PASSED" : "Build FAILED after max attempts" };
  },

  write_tests: async (ctx) => {
    const wt = ctx.data.worktreePath!;
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
    const wt = ctx.data.worktreePath!;
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
    const wt = ctx.data.worktreePath!;
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
    const wt = ctx.data.worktreePath!;
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

  visual_test: async (ctx) => {
    const { spawn } = require("node:child_process");
    const { existsSync, unlinkSync } = require("node:fs");
    const { join } = require("node:path");

    if (!ctx.data.worktreePath) {
      return { ok: false, output: "Visual test skipped — no worktree path provided" };
    }
    const wt = ctx.data.worktreePath!;
    const binaryPath = join(wt, "build/topogen");
    const baselinePath = join(wt, VISUAL_TEST_BASELINE);

    // Skip if no baseline exists (first run — nothing to compare against)
    if (!existsSync(baselinePath)) {
      return { ok: true, output: "Visual test skipped — no baseline image found" };
    }

    // Skip if binary doesn't exist (build failed earlier but was optional)
    if (!existsSync(binaryPath)) {
      return { ok: false, output: "Visual test skipped — build/topogen not found" };
    }

    // Clean stale socket
    try { unlinkSync(VISUAL_TEST_SOCKET); } catch { /* ignore */ }

    // Launch app in agent mode
    const appProc = spawn(binaryPath, ["--agent-mode", "--agent-socket", VISUAL_TEST_SOCKET], {
      cwd: wt,
      stdio: ["ignore", "pipe", "pipe"],
    });

    try {
      // Wait for socket to appear (up to 10s)
      const startTime = Date.now();
      while (Date.now() - startTime < 10_000) {
        if (existsSync(VISUAL_TEST_SOCKET)) break;
        await new Promise((r) => setTimeout(r, 200));
      }
      if (!existsSync(VISUAL_TEST_SOCKET)) {
        return { ok: false, output: "Visual test failed — app did not start (socket timeout)" };
      }

      // Position camera to a known state
      await agentCommand(VISUAL_TEST_SOCKET, "send_input", { key: "Home" });
      // Let the scene settle
      await new Promise((r) => setTimeout(r, VISUAL_TEST_SETTLE_MS));

      // Capture frame
      const captureResult = await agentCommand(VISUAL_TEST_SOCKET, "capture_frame", { path: VISUAL_TEST_CAPTURE });
      if (!captureResult.ok) {
        return { ok: false, output: `Visual test failed — capture error: ${captureResult.error}` };
      }

      // Stop app before comparing (frees GPU resources)
      await agentCommand(VISUAL_TEST_SOCKET, "quit");
      await new Promise((r) => setTimeout(r, 500));

      // Compare frames using the toolshed's frame_diff logic inline
      // We shell out to a small node script to avoid duplicating the PNG decoder
      const compareScript = `
        const { compareFrames } = require("${join(wt, ".pi/toolshed/src/frame_diff.ts").replace(/\\/g, "\\\\")}");
        const r = compareFrames("${baselinePath.replace(/\\/g, "\\\\")}", "${VISUAL_TEST_CAPTURE.replace(/\\/g, "\\\\")}");
        process.stdout.write(JSON.stringify(r));
      `;
      const { execSync } = require("node:child_process");
      let similarity: number;
      try {
        const tsxBin = join(wt, ".pi/toolshed/node_modules/.bin/tsx");
        const diffOutput = execSync(`${tsxBin} --eval '${compareScript}'`, {
          cwd: wt,
          encoding: "utf-8",
          timeout: 30_000,
        });
        const diffResult = JSON.parse(diffOutput.trim());
        similarity = diffResult.similarity ?? 0;
        ctx.data.visualTestResult = diffResult;
      } catch (e: any) {
        return { ok: false, output: `Visual test failed — frame comparison error: ${e.message}` };
      }

      if (similarity < VISUAL_TEST_SIMILARITY_THRESHOLD) {
        return {
          ok: false,
          output: `Visual regression detected: similarity ${similarity}% < ${VISUAL_TEST_SIMILARITY_THRESHOLD}% threshold`,
        };
      }

      return { ok: true, output: `Visual test PASSED — similarity ${similarity}%` };
    } finally {
      // Ensure app is killed regardless of outcome
      if (!appProc.killed) {
        appProc.kill("SIGTERM");
        await new Promise((r) => setTimeout(r, 300));
        if (!appProc.killed) appProc.kill("SIGKILL");
      }
    }
  },

  diagnose: async (ctx) => {
    const testResult = runTests(ctx.data.worktreePath!);
    const { execSync } = require("node:child_process");
    let recentCommits = "";
    try {
      recentCommits = execSync("git log --oneline -10 2>/dev/null", { encoding: "utf-8", cwd: ctx.data.worktreePath! });
    } catch { /* ignore */ }

    const diagnosis = await askDiagnoser({
      task: ctx.prompt,
      testOutput: testResult.summary,
      recentCommits,
      tools: ["test_run", "test_list", "app_launch", "app_send_input", "app_capture_frame", "app_compare_frames", "app_get_state", "app_stop", "viking_write_memory", "viking_search", "viking_read"],
      cwd: ctx.data.worktreePath!,
      signal: ctx.signal,
    });
    ctx.data.diagnosis = diagnosis;
    writeState("diagnosis.md", diagnosis);

    // Persist diagnosis to Viking persistent memory + session
    if (diagnosis.length > 0 && await isVikingAvailable()) {
      const { vikingWriteMemory } = await import("../tools/viking.js");
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-");
      await vikingWriteMemory(
        `viking://memories/delve/diagnoses/${timestamp}`,
        `# Diagnosis: ${ctx.prompt.slice(0, 80)}\n\n${diagnosis}`,
      );
      if (ctx.data.vikingSessionId) {
        await vikingAddMessage(ctx.data.vikingSessionId, `Diagnosis:\n${diagnosis}`, "assistant");
      }
    }

    return { ok: diagnosis.length > 0, output: "Diagnosis complete" };
  },

  shader_validate: async (ctx) => {
    const result = runShaderValidation(ctx.data.worktreePath!);
    return { ok: result.ok, output: result.ok ? "Shader validation PASSED" : result.summary };
  },

  verify: async (ctx) => {
    const wt = ctx.data.worktreePath!;

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

  commit_wip: async (ctx) => {
    const wt = ctx.data.worktreePath!;
    if (!wt) return { ok: false, output: "No worktree path — cannot save WIP" };

    const add = shell("git add -A 2>&1", wt);
    if (!add.ok) return { ok: false, output: `git add failed: ${add.stderr}` };

    const status = shell("git status --porcelain 2>&1", wt);
    if (status.stdout.trim().length === 0) {
      return { ok: true, output: "No changes to commit (clean worktree)" };
    }

    const failedPhase = ctx.data.lastFailedPhase || "unknown";
    const msg = `wip: checkpoint before ${failedPhase} failure\n\nPartial progress saved by commit_wip handler.`;
    const { writeFileSync, unlinkSync } = require("node:fs");
    const { join } = require("node:path");
    const { tmpdir } = require("node:os");
    const tmpFile = join(tmpdir(), `delve-wip-${Date.now()}.txt`);
    writeFileSync(tmpFile, msg, "utf-8");
    const commit = shell(`git commit -F "${tmpFile}" 2>&1`, wt);
    try { unlinkSync(tmpFile); } catch { /* ignore */ }

    if (!commit.ok) return { ok: false, output: `WIP commit failed: ${commit.stderr}` };

    const push = shell(`git push -u origin HEAD 2>&1`, wt);
    return {
      ok: true,
      output: push.ok
        ? "WIP committed and pushed"
        : `WIP committed locally (push failed: ${push.stderr})`,
    };
  },

  research: async (ctx) => {
    await requireViking();
    const results = await vikingSearch(ctx.prompt, "L2", "viking://resources/delve", 10);
    if (results.length === 0) {
      ctx.data.researchFindings = "";
      return { ok: true, output: "Viking returned 0 results — proceeding without research context" };
    }
    const findings = results.map((r) => `### ${r.uri}\n${r.snippet}`).join("\n\n");
    ctx.data.researchFindings = findings;
    return { ok: true, output: `Found ${results.length} relevant sources` };
  },

  math_verify: async (ctx) => {
    const { spawnSubagent } = await import("../agents/spawn.js");
    let diff = getDiff(ctx.data.worktreePath!);
    if (diff.length > 20000) {
      diff = diff.slice(0, 20000) + "\n\n...[DIFF TRUNCATED — " + diff.length + " chars total]...";
    }
    const result = await spawnSubagent({
      prompt: `Verify mathematical correctness of the following diff. Check for:\n- Off-by-one errors\n- Incorrect formulas\n- Numerical stability issues\n- Unit/coordinate system mismatches\n\nRespond with exactly PASS if correct, or FAIL: <details> if not.\n\n## Diff\n${diff}`,
      systemPrompt: "You are a mathematical verification agent for C++/GLSL game engine code. Analyze diffs for mathematical correctness. Be precise and concise.",
      model: "anthropic/claude-sonnet-4-6",
      agentName: "math-verifier",
      cwd: ctx.data.worktreePath!,
      signal: ctx.signal,
    });
    const passed = /\bPASS\b/i.test(result);
    return { ok: passed, output: passed ? "Math verification PASSED" : `Math verification FAILED: ${result}` };
  },

  worker_fan_out: async (ctx) => {
    const plan = ctx.data.plan;
    if (!plan) return { ok: false, output: "No plan available for worker fan-out" };

    const subtasks = parseSubtasks(plan);
    if (subtasks.length === 0) return { ok: false, output: "No subtasks parsed from plan" };

    const wt = ctx.data.worktreePath!;
    const sharedContextFiles = ctx.data.contextFiles || [];

    // ── Classify into two batches ────────────────────────────────────────
    const isGroundTruth = (task: string) => /\.(h|hpp|glsl|vert|frag)\b/.test(task);
    const batchA = subtasks.filter((st) => isGroundTruth(st.task));  // headers + shaders
    const batchB = subtasks.filter((st) => !isGroundTruth(st.task)); // implementations

    const dispatchWorker = (sub: typeof subtasks[0]) => {
      const agent = getSubsystemAgent(sub.subsystem);
      const enrichedTask = `## Original Request\n${ctx.prompt}\n\n## Plan Subtask\n${sub.task}`;
      const contextFiles = extractFilePaths(sub.task).concat(sharedContextFiles);
      const filename = sub.task.match(/\b[\w/.-]+\.\w+\b/)?.[0] || "unknown";
      ctx.ctx.ui.notify(`Worker starting: ${sub.subsystem} -> ${filename}`, "info");
      return agent({ task: enrichedTask, files: contextFiles, cwd: wt, signal: ctx.signal });
    };

    const allOutputs: string[] = [];

    // ── Batch A: Ground truth (headers/shaders) — parallel, then flush ──
    if (batchA.length > 0) {
      ctx.ctx.ui.notify(`Batch A: ${batchA.length} header/shader worker(s) launching`, "info");
      const batchAResults = await Promise.all(batchA.map(dispatchWorker));
      const emptyIdx = batchAResults.findIndex((r) => !r || r.trim().length === 0);
      if (emptyIdx >= 0) {
        return { ok: false, output: `Batch A worker ${emptyIdx} returned empty output (${batchA[emptyIdx].task.slice(0, 60)})` };
      }
      const batchAOutput = batchAResults.join("\n\n");
      const batchAFiles = (batchAOutput.match(/###\s*FILE:/g) || []).length;
      if (batchAFiles > 0) {
        applyFileBlocks(batchAOutput, wt);
        ctx.ctx.ui.notify(`Batch A flushed: ${batchAFiles} file(s) written to disk`, "info");
      }
      allOutputs.push(batchAOutput);
    }

    // ── Batch B: Implementations — parallel, after Batch A is on disk ───
    if (batchB.length > 0) {
      ctx.ctx.ui.notify(`Batch B: ${batchB.length} implementation worker(s) launching`, "info");
      const batchBResults = await Promise.all(batchB.map(dispatchWorker));
      const emptyIdx = batchBResults.findIndex((r) => !r || r.trim().length === 0);
      if (emptyIdx >= 0) {
        return { ok: false, output: `Batch B worker ${emptyIdx} returned empty output (${batchB[emptyIdx].task.slice(0, 60)})` };
      }
      const batchBOutput = batchBResults.join("\n\n");
      const batchBFiles = (batchBOutput.match(/###\s*FILE:/g) || []).length;
      if (batchBFiles > 0) {
        applyFileBlocks(batchBOutput, wt);
      }
      allOutputs.push(batchBOutput);
    }

    const combined = allOutputs.join("\n\n");
    const totalFiles = (combined.match(/###\s*FILE:/g) || []).length;
    if (totalFiles === 0) {
      return { ok: false, output: "No FILE blocks produced by workers" };
    }

    ctx.data.implementation = combined;
    return { ok: true, output: `Workers produced ${totalFiles} files (A:${batchA.length} B:${batchB.length} subtasks)` };
  },

  replan: async (ctx) => {
    const classification = ctx.data.errorClassification;
    if (!classification) {
      return { ok: false, output: "No error classification available for re-planning" };
    }

    const replanTask = `REPLAN REQUIRED: Previous implementation failed at build phase.

## Error Classification
Severity: ${classification.severity}
Summary: ${classification.summary}
Affected files: ${classification.affected_files.join(", ")}

## Previous Plan (FAILED)
${(ctx.data.plan || "").slice(0, 2000)}

## Original Task
${ctx.prompt}

Generate a NEW plan that addresses the architectural issue. Do NOT repeat the previous approach.`;

    const wt = ctx.data.worktreePath!;
    const codebaseContext = await getCodebaseContext(ctx.prompt, wt);
    const plan = await askMetaPlanner({
      task: replanTask,
      codebaseContext,
      cwd: wt,
      signal: ctx.signal,
      tools: ["code_list_subsystem", "code_find_definition", "code_find_usages", "git_status", "git_diff_from_main", "project_overview", "viking_search", "viking_read"],
    });

    ctx.data.plan = plan;
    ctx.data.errorClassification = undefined;
    return { ok: plan.length > 0, output: plan.length > 0 ? "Re-plan generated" : "Re-plan failed" };
  },

  // ─── B4+: Self-management handlers (merged from handlers-self.ts) ──────────
  ...SELF_PHASE_HANDLERS,
};

