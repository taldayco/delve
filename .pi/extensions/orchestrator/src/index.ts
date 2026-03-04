import { Type } from "@mariozechner/pi-ai";
import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  executeBlueprint,
  loadBlueprint,
  validateBlueprint,
  getAvailablePhases,
  type BlueprintContext,
} from "./blueprints.js";
import {
  askMetaPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askWorker,
  askBuildFixer,
  askTestFixer,
  askTerrainAgent,
  askActorAgent,
  askShaderAgent,
  askEngineAgent,
  getSubsystemAgent,
  generateWorkerPrompt,
  parseSubtasks,
  writeState,
  readState,
  askDiagnoser,
  askBlueprintGenerator,
  agentEvents,
} from "./agents.js";
import {
  slugify,
  elapsed,
  gitBranch,
  runBuild,
  runTests,
  gitCommitAndPr,
  getChangedFiles,
  getDiff,
  getCodebaseContext,
  resolveSubsystems,
  runShaderValidation,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
} from "./tools.js";

// ─── Types ──────────────────────────────────────────────────────────────────

type Phase =
  | "idle"
  | "branch"
  | "plan"
  | "diagnose"
  | "implement"
  | "build"
  | "fix-build"
  | "write-tests"
  | "test"
  | "fix-tests"
  | "review"
  | "shader-validate"
  | "commit-pr"
  | "done"
  | "failed";

interface MinionState {
  prompt: string;
  branch: string;
  phase: Phase;
  startTime: number;
  buildFixRound: number;
  testFixRound: number;
}

// ─── Extension Entry Point ─────────────────────────────────────────────────

export default function (pi: ExtensionAPI) {
  let state: MinionState | null = null;

  // ── Register Agent Tools ────────────────────────────────────────────────
  // These tools are available for the Opus orchestrator (pi session) to call

  pi.registerTool({
    name: "ask_meta_planner",
    label: "Meta Planner",
    description:
      "Decompose a task into ordered subtasks. Returns a structured plan. (Sonnet-tier, stateless)",
    parameters: Type.Object({
      task: Type.String({ description: "The feature/bug description" }),
      codebase_context: Type.String({
        description: "Relevant file listings, types, and structure info",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = await askMetaPlanner({
        task: (params as any).task,
        codebaseContext: (params as any).codebase_context,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  pi.registerTool({
    name: "ask_meta_implementer",
    label: "Meta Implementer",
    description:
      "Given a plan and file list, produce complete implementation changes. (Sonnet-tier, stateless)",
    parameters: Type.Object({
      plan: Type.String({ description: "The structured plan from ask_meta_planner" }),
      task: Type.String({ description: "Original task description" }),
      files: Type.Array(Type.String(), {
        description: "File paths the implementer needs to read",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = await askMetaImplementer({
        plan: (params as any).plan,
        task: (params as any).task,
        files: (params as any).files,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  pi.registerTool({
    name: "ask_meta_tester",
    label: "Meta Tester",
    description:
      "Generate test code for implemented changes. (Sonnet-tier, stateless)",
    parameters: Type.Object({
      task: Type.String({ description: "Original task description" }),
      changed_files: Type.Array(Type.String(), {
        description: "Paths of files that were changed",
      }),
      implementation_summary: Type.String({
        description: "Summary of what was implemented",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = await askMetaTester({
        task: (params as any).task,
        changedFiles: (params as any).changed_files,
        implementationSummary: (params as any).implementation_summary,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  pi.registerTool({
    name: "ask_reviewer",
    label: "Code Reviewer",
    description:
      "Review a diff for correctness, safety, and consistency. Returns APPROVE or REQUEST_CHANGES. (Sonnet-tier, stateless)",
    parameters: Type.Object({
      task: Type.String({ description: "Original task description" }),
      diff: Type.String({ description: "Git diff of changes" }),
      test_results: Type.String({ description: "Test output summary" }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = await askReviewer({
        task: (params as any).task,
        diff: (params as any).diff,
        testResults: (params as any).test_results,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  pi.registerTool({
    name: "ask_worker",
    label: "Worker Agent",
    description:
      "Execute a single, well-scoped task. The leaf agent — cheap and fast. (Haiku-tier, stateless)",
    parameters: Type.Object({
      system_prompt: Type.String({ description: "System prompt for the worker" }),
      task: Type.String({ description: "The specific task to perform" }),
      file_contents: Type.String({
        description: "JSON-encoded map of file path → content the worker needs (optional, empty string if none)",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      let fileContents: Record<string, string> | undefined;
      const raw = (params as any).file_contents;
      if (raw && raw.trim()) {
        try { fileContents = JSON.parse(raw); } catch { /* ignore */ }
      }
      const result = await askWorker({
        systemPrompt: (params as any).system_prompt,
        task: (params as any).task,
        fileContents,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  pi.registerTool({
    name: "generate_worker_prompt",
    label: "Prompt Generator",
    description:
      "Create a specialized system+user prompt for a Haiku worker. Meta-tool: prompts that create prompts. (Sonnet-tier, stateless)",
    parameters: Type.Object({
      task_type: Type.String({ description: "Type of task (e.g., fix_build, add_palette)" }),
      context: Type.String({ description: "Context for prompt generation" }),
      constraints: Type.Array(Type.String(), {
        description: "Non-negotiable constraints for the worker",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = await generateWorkerPrompt({
        taskType: (params as any).task_type,
        context: (params as any).context,
        constraints: (params as any).constraints,
      });
      return { content: [{ type: "text", text: JSON.stringify(result) }] };
    },
  });

  // ── Register Subsystem Agent Tools ─────────────────────────────────────

  const subsystemToolDefs: Array<{
    name: string;
    label: string;
    description: string;
    handler: (opts: { task: string; files: string[] }) => Promise<string>;
  }> = [
    { name: "ask_terrain_agent", label: "Terrain Agent", description: "Terrain specialist: noise, hex, contour, lava, mesh. (Sonnet-tier, stateless)", handler: askTerrainAgent },
    { name: "ask_actor_agent", label: "Actor Agent", description: "Actor specialist: skeleton, IK, gait, proportions, animation. (Sonnet-tier, stateless)", handler: askActorAgent },
    { name: "ask_shader_agent", label: "Shader Agent", description: "Shader specialist: GLSL, SPIR-V, vertex layouts, compute. (Sonnet-tier, stateless)", handler: askShaderAgent },
    { name: "ask_engine_agent", label: "Engine Agent", description: "Engine specialist: app lifecycle, GPU, camera, input, ECS, UI. (Sonnet-tier, stateless)", handler: askEngineAgent },
  ];

  for (const def of subsystemToolDefs) {
    pi.registerTool({
      name: def.name,
      label: def.label,
      description: def.description,
      parameters: Type.Object({
        task: Type.String({ description: "The task for this subsystem" }),
        files: Type.Array(Type.String(), {
          description: "Source file paths to read as context",
        }),
      }),
      async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
        const result = await def.handler({
          task: (params as any).task,
          files: (params as any).files,
        });
        return { content: [{ type: "text", text: result }] };
      },
    });
  }

  // ── Register Deterministic Tools ────────────────────────────────────────

  pi.registerTool({
    name: "run_build",
    label: "Run Build",
    description:
      "Run cmake configure + build. Returns exit status and last 50 lines of output. Full log written to .pi/state/build_log.txt.",
    parameters: Type.Object({}),
    async execute(_toolCallId, _params, _signal, _onUpdate, _ctx) {
      const result = runBuild();
      return {
        content: [{ type: "text", text: JSON.stringify({ ok: result.ok, summary: result.summary }) }],
      };
    },
  });

  pi.registerTool({
    name: "run_tests",
    label: "Run Tests",
    description:
      "Build and run delve_tests. Returns build/test status and output summary. Full results written to .pi/state/test_results.json.",
    parameters: Type.Object({}),
    async execute(_toolCallId, _params, _signal, _onUpdate, _ctx) {
      const result = runTests();
      return {
        content: [{
          type: "text",
          text: JSON.stringify({
            buildOk: result.buildOk,
            testsOk: result.testsOk,
            summary: result.summary,
          }),
        }],
      };
    },
  });

  pi.registerTool({
    name: "git_branch",
    label: "Git Branch",
    description: "Create a new git branch from main.",
    parameters: Type.Object({
      branch_name: Type.String({ description: "Branch name to create" }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = gitBranch((params as any).branch_name);
      return { content: [{ type: "text", text: JSON.stringify(result) }] };
    },
  });

  pi.registerTool({
    name: "git_commit_and_pr",
    label: "Git Commit & PR",
    description:
      "Stage all changes, commit, push, and create a PR. Returns the PR URL.",
    parameters: Type.Object({
      prompt: Type.String({ description: "Feature description for commit message" }),
      branch: Type.String({ description: "Branch name" }),
      build_ok: Type.Boolean({ description: "Did the build pass?" }),
      tests_ok: Type.Boolean({ description: "Did the tests pass?" }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const result = gitCommitAndPr({
        prompt: (params as any).prompt,
        branch: (params as any).branch,
        buildOk: (params as any).build_ok,
        testsOk: (params as any).tests_ok,
      });
      return { content: [{ type: "text", text: JSON.stringify(result) }] };
    },
  });

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

      const prompt = args.trim();
      const timestamp = new Date()
        .toISOString()
        .replace(/[:.]/g, "-")
        .slice(0, 19);
      const branch = `minion/${timestamp}-${slugify(prompt)}`;

      state = {
        prompt,
        branch,
        phase: "branch",
        startTime: Date.now(),
        buildFixRound: 0,
        testFixRound: 0,
      };
      attachAgentListeners(ctx);

      ctx.ui.notify(`Minion started: ${prompt}`, "info");

      try {
        // ── PHASE: Branch (deterministic) ─────────────────────────────
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) {
          ctx.ui.notify(branchResult.summary, "error");
          state.phase = "failed";
          return;
        }
        ctx.ui.notify(branchResult.summary, "success");

        // ── DECISION 1: Route by subsystem count ─────────────────────
        setPhase("plan", ctx);
        const subsystems = resolveSubsystems(prompt);
        const codebaseContext = getCodebaseContext(prompt);
        const contextFiles = extractFilePaths(codebaseContext);
        let implementation: string;

        if (subsystems.length <= 1) {
          // SINGLE SUBSYSTEM: call its agent directly (skip planner)
          const targetSubsystem = subsystems[0] || "engine";
          ctx.ui.notify(`Single subsystem: ${targetSubsystem} — calling specialist directly`, "info");
          setPhase("implement", ctx);
          const agent = getSubsystemAgent(targetSubsystem);
          implementation = await agent({ task: prompt, files: contextFiles });
        } else {
          // MULTIPLE SUBSYSTEMS: decompose via planner, then call each agent
          ctx.ui.notify(`Multiple subsystems: [${subsystems.join(", ")}] — decomposing via planner`, "info");
          const plan = await askMetaPlanner({ task: prompt, codebaseContext });
          ctx.ui.notify(`Plan complete (${plan.length} chars)`, "success");

          // Parse per-subsystem subtasks from plan
          const subtasks = parseSubtasks(plan);
          const implementations: string[] = [];

          setPhase("implement", ctx);
          if (subtasks.length > 0) {
            for (const sub of subtasks) {
              ctx.ui.notify(`Implementing subtask [${sub.subsystem}]`, "info");
              const agent = getSubsystemAgent(sub.subsystem);
              const subFiles = extractFilePaths(sub.task).concat(contextFiles);
              const subImpl = await agent({ task: sub.task, files: subFiles });
              implementations.push(subImpl);
            }
          } else {
            ctx.ui.notify("Planner output has no tagged subtasks — using generic implementer", "warning");
            const planFiles = extractFilePaths(plan);
            const genImpl = await askMetaImplementer({ plan, task: prompt, files: planFiles });
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
          });
          const retryCount = (finalImplementation.match(/###\s*FILE:/g) || []).length;
          if (retryCount === 0) {
            state.phase = "failed";
            ctx.ui.notify("No file changes after retry — FAIL", "error");
            return;
          }
        }

        // Apply implementation changes directly by parsing FILE blocks
        const appliedCount = applyFileBlocks(finalImplementation);
        ctx.ui.notify(`Implementation applied (${appliedCount} files)`, "success");

        // ── DECISION 3: Build pass? ──────────────────────────────────
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);

          const build = runBuild();

          if (build.ok) {
            ctx.ui.notify(`Build PASSED (round ${round + 1})`, "success");
            buildOk = true;
            break;
          }

          ctx.ui.notify(`Build FAILED (round ${round + 1}/${MAX_BUILD_FIX_ROUNDS})`, "warning");
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;

          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({
            buildOutput: build.summary,
            round: round + 1,
            maxRounds: MAX_BUILD_FIX_ROUNDS,
          });

          applyFileBlocks(fix);
        }

        if (!buildOk) {
          state.phase = "failed";
          ctx.ui.notify("Build FAILED after max attempts — FAIL", "error");
          return;
        }

        // ── PHASE: Write Tests ───────────────────────────────────────
        setPhase("write-tests", ctx);
        const changedFiles = getChangedFiles();
        const testCode = await askMetaTester({
          task: prompt,
          changedFiles,
          implementationSummary: implementation.slice(0, 2000),
        });

        applyFileBlocks(testCode);
        ctx.ui.notify("Tests written", "success");

        // ── DECISION 4: Tests pass? ──────────────────────────────────
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state.testFixRound = round + 1;
          setPhase("test", ctx);

          const testResult = runTests();

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

          setPhase("fix-tests", ctx);
          const fix = await askTestFixer({
            testOutput: testResult.summary,
            round: round + 1,
            maxRounds: MAX_TEST_FIX_ROUNDS,
            isBuildFailure,
          });

          applyFileBlocks(fix);
        }

        // ── DECISION 5: Review approve? (max 2 rounds) ──────────────
        let reviewDecision: "APPROVE" | "REQUEST_CHANGES" = "REQUEST_CHANGES";

        for (let reviewRound = 0; reviewRound < MAX_REVIEW_ROUNDS; reviewRound++) {
          setPhase("review", ctx);
          const diff = getDiff();
          const testResults = readState("test_results.json");
          const review = await askReviewer({
            task: prompt,
            diff,
            testResults: testsOk ? "All tests PASSED." : `Tests FAILED.\n${testResults}`,
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
            files: getChangedFiles(),
          });
          applyFileBlocks(reviewFix);

          // Re-build after review fixes
          setPhase("build", ctx);
          let fixBuild = runBuild();
          if (!fixBuild.ok) {
            const buildFix = await askBuildFixer({
              buildOutput: fixBuild.summary,
              round: 1,
              maxRounds: 1,
            });
            applyFileBlocks(buildFix);
            fixBuild = runBuild();
            if (!fixBuild.ok) {
              ctx.ui.notify("Build failed during review loop", "warning");
            }
          }

          // Re-test after review fixes
          setPhase("test", ctx);
          const retest = runTests();
          testsOk = retest.buildOk && retest.testsOk;
        }

        // ── PHASE: Commit + PR (deterministic) ───────────────────────
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({
          prompt,
          branch,
          buildOk,
          testsOk,
        });

        if (prResult.ok) {
          ctx.ui.notify(prResult.summary, "success");
        } else {
          ctx.ui.notify(prResult.summary, "warning");
        }

        // ── Done ──────────────────────────────────────────────────────
        state.phase = "done";
        ctx.ui.notify(
          `Minion complete in ${elapsed(state.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"} | Review: ${reviewDecision}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      }
    },
  });

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

      const prompt = args.trim();
      const timestamp = new Date()
        .toISOString()
        .replace(/[:.]/g, "-")
        .slice(0, 19);
      const branch = `minion-quick/${timestamp}-${slugify(prompt)}`;

      state = {
        prompt,
        branch,
        phase: "branch",
        startTime: Date.now(),
        buildFixRound: 0,
        testFixRound: 0,
      };
      attachAgentListeners(ctx);

      ctx.ui.notify(`Minion-quick started: ${prompt}`, "info");

      try {
        // ── PHASE: Branch ────────────────────────────────────────────
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) {
          ctx.ui.notify(branchResult.summary, "error");
          state.phase = "failed";
          return;
        }

        // ── PHASE: Implement (single agent, no planner) ──────────────
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt);
        const contextFiles = extractFilePaths(codebaseContext);

        ctx.ui.notify(`Subsystem: ${targetSubsystem}`, "info");
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) {
          state.phase = "failed";
          ctx.ui.notify("No file changes produced — FAIL", "error");
          return;
        }

        const appliedCount = applyFileBlocks(implementation);
        ctx.ui.notify(`Applied ${appliedCount} files`, "success");

        // ── PHASE: Build + fix (max 3 rounds) ───────────────────────
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild();

          if (build.ok) {
            ctx.ui.notify(`Build PASSED (round ${round + 1})`, "success");
            buildOk = true;
            break;
          }

          ctx.ui.notify(`Build FAILED (round ${round + 1}/${MAX_BUILD_FIX_ROUNDS})`, "warning");
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;

          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({
            buildOutput: build.summary,
            round: round + 1,
            maxRounds: MAX_BUILD_FIX_ROUNDS,
          });
          applyFileBlocks(fix);
        }

        if (!buildOk) {
          state.phase = "failed";
          ctx.ui.notify("Build FAILED after max attempts — FAIL", "error");
          return;
        }

        // ── PHASE: Commit + PR ───────────────────────────────────────
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({
          prompt,
          branch,
          buildOk: true,
          testsOk: false,
        });

        if (prResult.ok) {
          ctx.ui.notify(prResult.summary, "success");
        } else {
          ctx.ui.notify(prResult.summary, "warning");
        }

        state.phase = "done";
        ctx.ui.notify(
          `Minion-quick complete in ${elapsed(state.startTime)}. Build: PASS`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion-quick error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-refactor/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-refactor started: ${prompt}`, "info");

      try {
        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; ctx.ui.notify(branchResult.summary, "error"); return; }

        // Implement
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild();
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS });
          applyFileBlocks(fix);
        }
        if (!buildOk) { state.phase = "failed"; ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Review (with extra constraint: reject if behavior changes)
        setPhase("review", ctx);
        const diff = getDiff();
        const review = await askReviewer({
          task: prompt + "\n\nCRITICAL REVIEW CONSTRAINT: Reject if observable behavior changes. This is a refactoring — behavior must be preserved.",
          diff,
          testResults: "No tests run (refactoring pipeline).",
        });
        const reviewDecision = parseReviewDecision(review);
        ctx.ui.notify(`Review: ${reviewDecision}`, reviewDecision === "APPROVE" ? "success" : "warning");

        // Commit + PR
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: false });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(`Minion-refactor complete in ${elapsed(state.startTime)}`, "success");
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion-refactor error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-bugfix/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-bugfix started: ${prompt}`, "info");

      try {
        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; ctx.ui.notify(branchResult.summary, "error"); return; }

        // Diagnose: run tests first to collect failure output
        setPhase("diagnose", ctx);
        const initialTests = runTests();
        const { execSync } = require("node:child_process");
        let recentCommits = "";
        try {
          recentCommits = execSync("git log --oneline -10 2>/dev/null", { encoding: "utf-8" });
        } catch { /* ignore */ }

        const diagnosis = await askDiagnoser({
          task: prompt,
          testOutput: initialTests.summary,
          recentCommits,
        });
        ctx.ui.notify("Diagnosis complete", "success");
        writeState("diagnosis.md", diagnosis);

        // Implement with diagnosis context
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({
          task: `${prompt}\n\n## Diagnosis\n${diagnosis}`,
          files: contextFiles,
        });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild();
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS });
          applyFileBlocks(fix);
        }
        if (!buildOk) { state.phase = "failed"; ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Test (no test-writing — bugfixes should make existing tests pass)
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state.testFixRound = round + 1;
          setPhase("test", ctx);
          const testResult = runTests();
          if (testResult.buildOk && testResult.testsOk) { testsOk = true; break; }
          if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;
          setPhase("fix-tests", ctx);
          const fix = await askTestFixer({
            testOutput: testResult.summary,
            round: round + 1,
            maxRounds: MAX_TEST_FIX_ROUNDS,
            isBuildFailure: !testResult.buildOk,
          });
          applyFileBlocks(fix);
        }

        // Commit + PR (skip review for bugfixes)
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(
          `Minion-bugfix complete in ${elapsed(state.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion-bugfix error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-shader/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-shader started: ${prompt}`, "info");

      try {
        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; ctx.ui.notify(branchResult.summary, "error"); return; }

        // Implement (force shader subsystem)
        setPhase("implement", ctx);
        const codebaseContext = getCodebaseContext("shader " + prompt);
        const contextFiles = extractFilePaths(codebaseContext);
        const implementation = await askShaderAgent({ task: prompt, files: contextFiles });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild();
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS });
          applyFileBlocks(fix);
        }
        if (!buildOk) { state.phase = "failed"; ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Shader validation via glslc
        setPhase("shader-validate", ctx);
        const shaderResult = runShaderValidation();
        ctx.ui.notify(
          shaderResult.ok ? "Shader validation PASSED" : `Shader validation FAILED:\n${shaderResult.summary}`,
          shaderResult.ok ? "success" : "warning"
        );

        // Commit + PR
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: shaderResult.ok });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(
          `Minion-shader complete in ${elapsed(state.startTime)}. Build: PASS | Shaders: ${shaderResult.ok ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion-shader error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      }
    },
  });

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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-meta/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "plan", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-meta started: ${prompt}`, "info");

      try {
        // Generate blueprint via AI
        setPhase("plan", ctx);
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
        const context: BlueprintContext = {
          prompt,
          branch,
          startTime: Date.now(),
          ctx,
          data: {},
        };

        await executeBlueprint(blueprint, context);

        state.phase = "done";
        ctx.ui.notify(
          `Minion-meta complete in ${elapsed(state.startTime)}. Blueprint: ${blueprint.name}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion-meta error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      }
    },
  });

  // ── /minion-status command ──────────────────────────────────────────────

  pi.registerCommand("minion-status", {
    description: "Show current minion run status",
    handler: async (_args, ctx) => {
      if (!state) {
        ctx.ui.notify("No minion run in progress", "info");
        return;
      }
      ctx.ui.notify(
        `Phase: ${state.phase} | Build rounds: ${state.buildFixRound}/${MAX_BUILD_FIX_ROUNDS} | Test rounds: ${state.testFixRound}/${MAX_TEST_FIX_ROUNDS} | Elapsed: ${elapsed(state.startTime)}`,
        "info"
      );
    },
  });

  // ── Safety: block dangerous operations ──────────────────────────────────

  pi.on("tool_call", async (event, _ctx) => {
    if (event.toolName === "bash") {
      const cmd = (event.input as any)?.command || "";
      const dangerous =
        cmd.includes("rm -rf /") ||
        cmd.includes("git push --force") ||
        cmd.includes("git push -f") ||
        (cmd.includes("git reset --hard") && cmd.includes("main"));
      if (dangerous) {
        return { block: true, reason: "Blocked dangerous operation: " + cmd };
      }
    }
    return undefined;
  });

  // ── Status line tracking ────────────────────────────────────────────────

  let _agentStartListener: ((...args: any[]) => void) | null = null;
  let _agentEndListener: ((...args: any[]) => void) | null = null;

  function attachAgentListeners(ctx: any) {
    detachAgentListeners();
    _agentStartListener = ({ name }: { name: string }) => {
      if (state) {
        ctx.ui.setStatus("minion", `${state.phase}: ${name} [${elapsed(state.startTime)}]`);
      }
    };
    _agentEndListener = ({ name }: { name: string }) => {
      if (state) {
        ctx.ui.setStatus("minion", `${state.phase}: ${name} done [${elapsed(state.startTime)}]`);
      }
    };
    agentEvents.on("agent:start", _agentStartListener);
    agentEvents.on("agent:end", _agentEndListener);
  }

  function detachAgentListeners() {
    if (_agentStartListener) {
      agentEvents.removeListener("agent:start", _agentStartListener);
      _agentStartListener = null;
    }
    if (_agentEndListener) {
      agentEvents.removeListener("agent:end", _agentEndListener);
      _agentEndListener = null;
    }
  }

  function setPhase(phase: Phase, ctx: any) {
    if (state) {
      state.phase = phase;
      ctx.ui.setStatus("minion", `${phase} [${elapsed(state.startTime)}]`);
    }
  }
}

// ─── Constants ───────────────────────────────────────────────────────────────

const MAX_REVIEW_ROUNDS = 2;

// ─── Helpers ────────────────────────────────────────────────────────────────

/**
 * Parse ### FILE: blocks from agent output and write files directly.
 * Returns the number of files written.
 */
function applyFileBlocks(text: string): number {
  const { writeFileSync, mkdirSync } = require("node:fs");
  const { dirname, join } = require("node:path");
  const cwd = process.cwd();

  // Match ### FILE: <path> followed by optional #### ACTION: line and code block.
  // IMPORTANT: Only accepts ACTION (not CHANGE) — partial-patch CHANGE blocks
  // would destroy files since we write the code block as the entire file content.
  const regex = /###\s*FILE:\s*(\S+)\s*\n(?:####\s*ACTION:[^\n]*\n)?```[\w]*\n([\s\S]*?)```/g;
  let match;
  let count = 0;

  while ((match = regex.exec(text)) !== null) {
    const filePath = match[1];
    const content = match[2];
    const fullPath = filePath.startsWith("/") ? filePath : join(cwd, filePath);

    try {
      mkdirSync(dirname(fullPath), { recursive: true });
      writeFileSync(fullPath, content, "utf-8");
      count++;
    } catch (e: any) {
      // Log but don't fail — some files may be in read-only locations
      console.error(`Failed to write ${fullPath}: ${e.message}`);
    }
  }

  return count;
}

/**
 * Extract file paths from markdown text.
 * Matches any directory/file.ext pattern.
 */
function extractFilePaths(text: string): string[] {
  const regex = /\b([a-zA-Z0-9_.][a-zA-Z0-9_./+-]*\/[a-zA-Z0-9_./+-]*\.[a-zA-Z]{1,10})\b/g;
  const paths = new Set<string>();
  let match;
  while ((match = regex.exec(text)) !== null) {
    const p = match[1];
    if (p.includes("://") || p.includes("..")) continue;
    paths.add(p);
  }
  if (text.includes("CMakeLists.txt")) {
    paths.add("CMakeLists.txt");
  }
  return Array.from(paths);
}

/**
 * Parse a review response into APPROVE or REQUEST_CHANGES.
 * Binary — no middle ground.
 */
function parseReviewDecision(review: string): "APPROVE" | "REQUEST_CHANGES" {
  // 1. Structured header: "## Review: APPROVE"
  const headerMatch = review.match(/##\s*Review:\s*(APPROVE|REQUEST_CHANGES)/i);
  if (headerMatch) {
    return headerMatch[1].toUpperCase() as "APPROVE" | "REQUEST_CHANGES";
  }

  // 2. Negation patterns → NOT approved
  if (/\b(cannot|can't|do not|don't|unable to|not)\s+approve\b/i.test(review)) {
    return "REQUEST_CHANGES";
  }

  // 3. Explicit REQUEST_CHANGES
  if (/\bREQUEST_CHANGES\b/.test(review)) {
    return "REQUEST_CHANGES";
  }

  // 4. Word-boundary APPROVE
  if (/\bAPPROVE\b/.test(review)) {
    return "APPROVE";
  }

  // Default: changes requested
  return "REQUEST_CHANGES";
}
