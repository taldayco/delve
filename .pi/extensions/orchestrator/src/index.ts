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
  askParallelPlanner,
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
  askVerifier,
  agentEvents,
  readRunMetrics,
  pruneMetricsLog,
  askDecoupleAnalyst,
  askMapUpdater,
} from "./agents.js";
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
  runShaderValidation,
  runMetrics,
  cleanupMergedBranches,
  applyFileBlocks,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  measureDomainComplexity,
  runSystemAudit,
  detectMapCoverage,
  cleanupStaleState,
  getSubsystemCodebaseContext,
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
  | "verify"
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
  worktreePath?: string;
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
    name: "ask_parallel_planner",
    label: "Parallel Planner",
    description:
      "Decompose a multi-subsystem task by spawning parallel per-subsystem planners. Faster and more reliable than ask_meta_planner for tasks touching 2+ subsystems. (Sonnet-tier, parallel)",
    parameters: Type.Object({
      task: Type.String({ description: "The feature/bug description" }),
      subsystems: Type.Array(Type.String(), {
        description: "Canonical subsystem names to plan for (e.g. ['terrain', 'shader', 'engine'])",
      }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const subsystems = (params as any).subsystems as string[];
      const subsystemContexts: Record<string, string> = {};
      for (const sub of subsystems) {
        subsystemContexts[sub] = getSubsystemCodebaseContext(sub);
      }
      const result = await askParallelPlanner({
        task: (params as any).task,
        subsystemContexts,
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
    name: "run_metrics",
    label: "Run Metrics",
    description:
      "Build and run delve_metrics with per-domain JSON output. Emits terrain.json, mesh.json, performance.json into .pi/state/metrics/.",
    parameters: Type.Object({}),
    async execute(_toolCallId, _params, _signal, _onUpdate, _ctx) {
      const result = runMetrics();
      return {
        content: [{
          type: "text",
          text: JSON.stringify({
            ok: result.ok,
            domains: result.domains,
            outputDir: result.outputDir,
            summary: result.summary,
          }),
        }],
      };
    },
  });

  pi.registerTool({
    name: "ask_verifier",
    label: "Verifier",
    description:
      "Run headless metrics and verify quantitative results. Returns verification summary with per-domain pass/fail. (Sonnet coordinator + Haiku analyzers)",
    parameters: Type.Object({
      task: Type.String({ description: "Task description for context" }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const metrics = runMetrics();
      if (!metrics.ok) {
        return { content: [{ type: "text", text: JSON.stringify({ ok: false, error: metrics.summary }) }] };
      }
      const result = await askVerifier({
        task: (params as any).task,
        metricsDir: metrics.outputDir,
        domains: metrics.domains,
      });
      return { content: [{ type: "text", text: result }] };
    },
  });

  // ── Register Self-Scaling Tools ──────────────────────────────────────────

  pi.registerTool({
    name: "read_run_metrics",
    label: "Read Run Metrics",
    description:
      "Read recent per-agent-invocation metrics (context usage, duration, escalation). Returns last N records from .pi/state/metrics.jsonl.",
    parameters: Type.Object({
      max_records: Type.Number({ description: "Maximum number of records to return (default 50)" }),
    }),
    async execute(_toolCallId, params, _signal, _onUpdate, _ctx) {
      const records = readRunMetrics((params as any).max_records || 50);
      return { content: [{ type: "text", text: JSON.stringify(records, null, 2) }] };
    },
  });

  pi.registerTool({
    name: "measure_domain_complexity",
    label: "Measure Domain Complexity",
    description:
      "Measure file count, line count, and complexity score for each domain (terrain, actor, shader, engine). Flags domains exceeding the threshold.",
    parameters: Type.Object({}),
    async execute(_toolCallId, _params, _signal, _onUpdate, _ctx) {
      const reports = measureDomainComplexity();
      return { content: [{ type: "text", text: JSON.stringify(reports, null, 2) }] };
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

      if (state) {
        ctx.ui.notify(`A minion is already running (phase: ${state.phase}). Wait or restart.`, "error");
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

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // ── PHASE: Branch (deterministic) ─────────────────────────────
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) {
          ctx.ui.notify(branchResult.summary, "error");
          state.phase = "failed";
          recordFailure("branch", branchResult.summary);
          return;
        }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;
        ctx.ui.notify(branchResult.summary, "success");

        // ── DECISION 1: Route by subsystem count ─────────────────────
        setPhase("plan", ctx);
        const subsystems = resolveSubsystems(prompt);
        const codebaseContext = getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        let implementation: string;

        if (subsystems.length <= 1) {
          // SINGLE SUBSYSTEM: call its agent directly (skip planner)
          const targetSubsystem = subsystems[0] || "engine";
          ctx.ui.notify(`Single subsystem: ${targetSubsystem} — calling specialist directly`, "info");
          setPhase("implement", ctx);
          const agent = getSubsystemAgent(targetSubsystem);
          implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });
        } else {
          // MULTIPLE SUBSYSTEMS: parallel planning — one planner per subsystem
          ctx.ui.notify(`Multiple subsystems: [${subsystems.join(", ")}] — parallel planning`, "info");

          // Build per-subsystem scoped contexts
          const subsystemContexts: Record<string, string> = {};
          for (const sub of subsystems) {
            subsystemContexts[sub] = getSubsystemCodebaseContext(sub, wt);
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

          setPhase("implement", ctx);
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
            state.phase = "failed";
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
          state.buildFixRound = round + 1;
          setPhase("build", ctx);

          const build = runBuild(wt);

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
            cwd: wt,
          });

          applyFileBlocks(fix, wt);
        }

        if (!buildOk) {
          state.phase = "failed";
          recordFailure("build", "Build failed after max fix attempts");
          ctx.ui.notify("Build FAILED after max attempts — FAIL", "error");
          return;
        }

        // ── PHASE: Write Tests ───────────────────────────────────────
        setPhase("write-tests", ctx);
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
          state.testFixRound = round + 1;
          setPhase("test", ctx);

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

          setPhase("fix-tests", ctx);
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
          setPhase("review", ctx);
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
          setPhase("build", ctx);
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
          setPhase("test", ctx);
          const retest = runTests(wt);
          testsOk = retest.buildOk && retest.testsOk;
        }

        // ── GATE: Final build+test verification after review ─────────
        setPhase("build", ctx);
        const finalBuild = runBuild(wt);
        if (!finalBuild.ok) {
          state.phase = "failed";
          recordFailure("build", "Final build check failed after review");
          ctx.ui.notify("Final build check FAILED after review — aborting pipeline", "error");
          return;
        }
        ctx.ui.notify("Final build check PASSED", "success");

        setPhase("test", ctx);
        const finalTests = runTests(wt);
        if (!finalTests.buildOk || !finalTests.testsOk) {
          state.phase = "failed";
          recordFailure("test", "Final test check failed after review");
          ctx.ui.notify("Final test check FAILED after review — aborting pipeline", "error");
          return;
        }
        testsOk = true;
        buildOk = true;
        ctx.ui.notify("Final test check PASSED", "success");

        // ── PHASE: Commit + PR (deterministic) ───────────────────────
        setPhase("commit-pr", ctx);
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
        state.phase = "done";
        ctx.ui.notify(
          `Minion complete in ${elapsed(state.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"} | Review: ${reviewDecision}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
        if (worktreePath) cleanupWorktree(worktreePath);
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

      if (state) {
        ctx.ui.notify(`A minion is already running (phase: ${state.phase}). Wait or restart.`, "error");
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

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // ── PHASE: Branch ────────────────────────────────────────────
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) {
          ctx.ui.notify(branchResult.summary, "error");
          state.phase = "failed";
          recordFailure("branch", branchResult.summary);
          return;
        }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;

        // ── PHASE: Implement (single agent, no planner) ──────────────
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);

        ctx.ui.notify(`Subsystem: ${targetSubsystem}`, "info");
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) {
          state.phase = "failed";
          recordFailure("implement", "No file changes produced");
          ctx.ui.notify("No file changes produced — FAIL", "error");
          return;
        }

        const appliedCount = applyFileBlocks(implementation, wt);
        ctx.ui.notify(`Applied ${appliedCount} files`, "success");

        // ── PHASE: Build + fix (max 3 rounds) ───────────────────────
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild(wt);

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
            cwd: wt,
          });
          applyFileBlocks(fix, wt);
        }

        if (!buildOk) {
          state.phase = "failed";
          recordFailure("build", "Build failed after max fix attempts");
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
          cwd: wt,
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
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-quick error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-refactor/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-refactor started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;

        // Implement
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Review (with extra constraint: reject if behavior changes)
        setPhase("review", ctx);
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
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: false, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(`Minion-refactor complete in ${elapsed(state.startTime)}`, "success");
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-refactor error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-bugfix/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-bugfix started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;

        // Diagnose: run tests first to collect failure output
        setPhase("diagnose", ctx);
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
        setPhase("implement", ctx);
        const subsystems = resolveSubsystems(prompt);
        const targetSubsystem = subsystems[0] || "engine";
        const codebaseContext = getCodebaseContext(prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const agent = getSubsystemAgent(targetSubsystem);
        const implementation = await agent({
          task: `${prompt}\n\n## Diagnosis\n${diagnosis}`,
          files: contextFiles,
          cwd: wt,
        });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Test (no test-writing — bugfixes should make existing tests pass)
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state.testFixRound = round + 1;
          setPhase("test", ctx);
          const testResult = runTests(wt);
          if (testResult.buildOk && testResult.testsOk) { testsOk = true; break; }
          if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;
          setPhase("fix-tests", ctx);
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
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(
          `Minion-bugfix complete in ${elapsed(state.startTime)}. Build: ${buildOk ? "PASS" : "FAIL"} | Tests: ${testsOk ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-bugfix error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
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

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-shader/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-shader started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;

        // Implement (force shader subsystem)
        setPhase("implement", ctx);
        const codebaseContext = getCodebaseContext("shader " + prompt, wt);
        const contextFiles = extractFilePaths(codebaseContext);
        const implementation = await askShaderAgent({ task: prompt, files: contextFiles, cwd: wt });

        const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
        if (fileBlockCount === 0) { state.phase = "failed"; recordFailure("implement", "No file changes produced"); ctx.ui.notify("No file changes produced — FAIL", "error"); return; }
        applyFileBlocks(implementation, wt);

        // Build + fix
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }
        if (!buildOk) { state.phase = "failed"; recordFailure("build", "Build failed after max fix attempts"); ctx.ui.notify("Build FAILED after max attempts", "error"); return; }

        // Shader validation via glslc
        setPhase("shader-validate", ctx);
        const shaderResult = runShaderValidation(wt);
        ctx.ui.notify(
          shaderResult.ok ? "Shader validation PASSED" : `Shader validation FAILED:\n${shaderResult.summary}`,
          shaderResult.ok ? "success" : "warning"
        );

        // Commit + PR
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt, branch, buildOk, testsOk: shaderResult.ok, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(
          `Minion-shader complete in ${elapsed(state.startTime)}. Build: PASS | Shaders: ${shaderResult.ok ? "PASS" : "FAIL"}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Minion-shader error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
        if (worktreePath) cleanupWorktree(worktreePath);
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

      if (state) {
        ctx.ui.notify(`A minion is already running (phase: ${state.phase}). Wait or restart.`, "error");
        return;
      }

      const prompt = args.trim();
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-meta/${timestamp}-${slugify(prompt)}`;

      state = { prompt, branch, phase: "plan", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify(`Minion-meta started: ${prompt}`, "info");

      let worktreePath: string | undefined;
      try {
        // Silently clean up merged minion branches
        cleanupMergedBranches();

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

        // Track worktree path from state in case executeBlueprint set it
        worktreePath = state?.worktreePath;

        state.phase = "done";
        ctx.ui.notify(
          `Minion-meta complete in ${elapsed(state.startTime)}. Blueprint: ${blueprint.name}`,
          "success"
        );
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        worktreePath = worktreePath || state?.worktreePath;
        if (state) state.phase = "failed";
        recordFailure("blueprint", error.message);
        ctx.ui.notify(`Minion-meta error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });

  // ── /minion-cleanup command ─────────────────────────────────────────────

  pi.registerCommand("minion-cleanup", {
    description: "Clean up merged minion branches and stale state files",
    handler: async (_args, ctx) => {
      const branchResult = cleanupMergedBranches();
      ctx.ui.notify(branchResult.summary, branchResult.deleted.length > 0 ? "success" : "info");

      const stateResult = cleanupStaleState();
      if (stateResult.deleted.length > 0) {
        ctx.ui.notify(stateResult.summary, "success");
      }

      pruneMetricsLog();
    },
  });

  // ── /minion-audit command ───────────────────────────────────────────────

  pi.registerCommand("minion-audit", {
    description: "Run a system audit: check for stale agents, broken rules, unmapped directories, domain overload, and orphaned state",
    handler: async (_args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Running system audit...", "info");
      const report = runSystemAudit();

      // Format findings as markdown
      const lines: string[] = [
        "# System Audit Report",
        "",
        `**Summary:** ${report.summary}`,
        "",
      ];

      if (report.findings.length > 0) {
        lines.push("## Findings", "");
        for (const finding of report.findings) {
          const icon = finding.severity === "error" ? "ERROR" : "WARNING";
          lines.push(`- **[${icon}]** (${finding.category}) ${finding.message}`);
        }
        lines.push("");
      }

      lines.push("## Domain Complexity", "");
      for (const domain of report.domainReports) {
        const flag = domain.exceedsThreshold ? " **OVER THRESHOLD**" : "";
        lines.push(`- **${domain.domain}** (${domain.directory}): score=${domain.complexityScore}, files=${domain.fileCount}, lines=${domain.totalLines}${flag}`);
      }

      const markdown = lines.join("\n");
      writeState("audit_report.md", markdown);
      ctx.ui.notify(report.summary, report.findings.length > 0 ? "warning" : "success");
      ctx.ui.notify("Full report: .pi/state/audit_report.md", "info");
    },
  });

  // ── /minion-decouple command ────────────────────────────────────────────

  pi.registerCommand("minion-decouple", {
    description: "Propose a domain split for an over-complex domain. Usage: /minion-decouple [domain]",
    handler: async (args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Measuring domain complexity...", "info");
      const reports = measureDomainComplexity();
      const overThreshold = reports.filter((r) => r.exceedsThreshold);

      if (overThreshold.length === 0) {
        ctx.ui.notify("No domains exceed the complexity threshold — no decoupling needed", "info");
        return;
      }

      // Pick specified domain or highest-scoring one
      const targetDomain = args?.trim() || "";
      let target = overThreshold.find((r) => r.domain === targetDomain);
      if (!target) {
        target = overThreshold.sort((a, b) => b.complexityScore - a.complexityScore)[0];
        if (targetDomain) {
          ctx.ui.notify(`Domain "${targetDomain}" not over threshold — using highest: ${target.domain}`, "warning");
        }
      }

      ctx.ui.notify(`Analyzing domain "${target.domain}" (score=${target.complexityScore})...`, "info");

      const recentMetrics = readRunMetrics(20);
      const proposal = await askDecoupleAnalyst({
        domainReport: target,
        recentMetrics,
        files: target.files,
      });

      ctx.ui.notify(`Decouple proposal written to .pi/state/decouple_proposal.md`, "success");
      ctx.ui.notify("Review the proposal, then run /minion-decouple-execute to apply it", "info");
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

      const prompt = `Apply the following domain decoupling proposal:\n\n${proposal}`;
      const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
      const branch = `minion-decouple/${timestamp}`;

      state = { prompt, branch, phase: "branch", startTime: Date.now(), buildFixRound: 0, testFixRound: 0 };
      attachAgentListeners(ctx);
      ctx.ui.notify("Executing decouple proposal...", "info");

      let worktreePath: string | undefined;
      try {
        cleanupMergedBranches();

        // Branch
        setPhase("branch", ctx);
        const branchResult = gitBranch(branch);
        if (!branchResult.ok) { state.phase = "failed"; recordFailure("branch", branchResult.summary); ctx.ui.notify(branchResult.summary, "error"); return; }
        const wt = branchResult.worktreePath;
        worktreePath = wt;
        state.worktreePath = wt;

        // Apply proposal file blocks
        setPhase("implement", ctx);
        const appliedCount = applyFileBlocks(proposal, wt);
        ctx.ui.notify(`Applied ${appliedCount} files from proposal`, "success");

        // Build
        setPhase("build", ctx);
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          const build = runBuild(wt);
          if (build.ok) { buildOk = true; break; }
          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({ buildOutput: build.summary, round: round + 1, maxRounds: MAX_BUILD_FIX_ROUNDS, cwd: wt });
          applyFileBlocks(fix, wt);
        }

        if (!buildOk) {
          state.phase = "failed";
          recordFailure("build", "Build failed after max fix attempts");
          ctx.ui.notify("Build FAILED after max attempts — decouple aborted", "error");
          return;
        }

        // Review
        setPhase("review", ctx);
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
        setPhase("commit-pr", ctx);
        const prResult = gitCommitAndPr({ prompt: "Domain decoupling", branch, buildOk, testsOk: false, cwd: wt });
        ctx.ui.notify(prResult.ok ? prResult.summary : prResult.summary, prResult.ok ? "success" : "warning");

        state.phase = "done";
        ctx.ui.notify(`Decouple-execute complete in ${elapsed(state.startTime)}`, "success");
        detachAgentListeners();
        state = null;
      } catch (error: any) {
        if (state) state.phase = "failed";
        recordFailure(state?.phase || "unknown", error.message);
        ctx.ui.notify(`Decouple-execute error: ${error.message}`, "error");
        detachAgentListeners();
        state = null;
      } finally {
        if (worktreePath) cleanupWorktree(worktreePath);
      }
    },
  });

  // ── /minion-update-maps command ─────────────────────────────────────────

  pi.registerCommand("minion-update-maps", {
    description: "Detect unmapped directories and update KEYWORD_SYNONYMS/SUBSYSTEM_DIRS maps",
    handler: async (_args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Detecting map coverage gaps...", "info");
      const coverage = detectMapCoverage();

      if (coverage.unmappedDirs.length === 0) {
        ctx.ui.notify("All directories are mapped — no updates needed", "success");
        return;
      }

      ctx.ui.notify(`Found ${coverage.unmappedDirs.length} unmapped directory(s): ${coverage.unmappedDirs.join(", ")}`, "warning");

      // Read current tools.ts for the maps section
      const { readFileSync } = require("node:fs");
      const { join } = require("node:path");
      const toolsPath = join(process.cwd(), ".pi/extensions/orchestrator/src/tools.ts");
      let toolsContent = "";
      try {
        const full = readFileSync(toolsPath, "utf-8");
        // Extract just the maps section
        const start = full.indexOf("const KEYWORD_SYNONYMS");
        const end = full.indexOf("};", full.indexOf("const SUBSYSTEM_CANONICAL")) + 2;
        if (start >= 0 && end > start) {
          toolsContent = full.slice(start, end);
        }
      } catch { /* ignore */ }

      ctx.ui.notify("Spawning map updater agent...", "info");
      const result = await askMapUpdater({
        coverageReport: coverage,
        currentToolsContent: toolsContent,
      });

      const appliedCount = applyFileBlocks(result);
      if (appliedCount > 0) {
        ctx.ui.notify(`Updated ${appliedCount} file(s). Rebuild the extension: cd .pi/extensions/orchestrator && npm run build`, "success");
      } else {
        writeState("map_update_suggestion.md", result);
        ctx.ui.notify("No file blocks produced. Suggestion written to .pi/state/map_update_suggestion.md", "warning");
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

  // ── Live Status Display ─────────────────────────────────────────────────

  const SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

  const PHASE_ORDER: Phase[] = [
    "branch", "plan", "diagnose", "implement", "build", "fix-build",
    "write-tests", "test", "fix-tests", "review", "shader-validate", "commit-pr",
  ];

  // Display labels: group fix-* phases with their parent
  const PHASE_LABELS: Record<string, string> = {
    "branch": "branch",
    "plan": "plan",
    "diagnose": "diagnose",
    "implement": "implement",
    "build": "build",
    "fix-build": "build",
    "write-tests": "tests",
    "test": "tests",
    "fix-tests": "tests",
    "review": "review",
    "shader-validate": "shaders",
    "commit-pr": "pr",
  };

  // Deduplicated display phases in order
  function getDisplayPhases(pipelinePhases: Phase[]): string[] {
    const seen = new Set<string>();
    const result: string[] = [];
    for (const p of pipelinePhases) {
      const label = PHASE_LABELS[p] || p;
      if (!seen.has(label)) {
        seen.add(label);
        result.push(label);
      }
    }
    return result;
  }

  // Phase icons for breadcrumbs
  const PHASE_ICONS = { done: "✓", current: "▸", pending: "○" };

  class MinionDisplay {
    private interval: ReturnType<typeof setInterval> | null = null;
    private spinnerIdx = 0;
    private agentName = "";
    private agentModel = "";
    private currentPhase: Phase = "idle";
    private lastProgress = 0;
    private pipelinePhases: Phase[] = [];
    private completedPhases = new Set<string>();
    private ctx: any = null;
    private _onStart: ((...args: any[]) => void) | null = null;
    private _onEnd: ((...args: any[]) => void) | null = null;
    private _onProgress: ((...args: any[]) => void) | null = null;

    start(ctx: any, minionState: MinionState, pipelinePhases?: Phase[]) {
      this.stop();
      this.ctx = ctx;
      this.currentPhase = minionState.phase;
      this.completedPhases.clear();
      this.agentName = "";
      this.agentModel = "";
      this.lastProgress = Date.now();
      this.pipelinePhases = pipelinePhases || PHASE_ORDER;

      // Event listeners
      this._onStart = ({ name, model }: { name: string; model?: string }) => {
        this.agentName = name;
        this.agentModel = model || "";
        this.lastProgress = Date.now();
      };
      this._onEnd = () => {
        this.agentName = "";
        this.agentModel = "";
      };
      this._onProgress = () => {
        this.lastProgress = Date.now();
      };

      agentEvents.on("agent:start", this._onStart);
      agentEvents.on("agent:end", this._onEnd);
      agentEvents.on("agent:progress", this._onProgress);

      // Render loop at 500ms
      this.interval = setInterval(() => this.render(minionState), 500);
      this.render(minionState);
    }

    updatePhase(phase: Phase, minionState: MinionState) {
      // Mark previous display label as completed
      if (this.currentPhase !== "idle") {
        this.completedPhases.add(PHASE_LABELS[this.currentPhase] || this.currentPhase);
      }
      this.currentPhase = phase;
      minionState.phase = phase;
      this.render(minionState);
    }

    stop() {
      if (this.interval) {
        clearInterval(this.interval);
        this.interval = null;
      }
      if (this._onStart) {
        agentEvents.removeListener("agent:start", this._onStart);
        this._onStart = null;
      }
      if (this._onEnd) {
        agentEvents.removeListener("agent:end", this._onEnd);
        this._onEnd = null;
      }
      if (this._onProgress) {
        agentEvents.removeListener("agent:progress", this._onProgress);
        this._onProgress = null;
      }
      if (this.ctx) {
        try {
          this.ctx.ui.setWidget("minion", []);
          this.ctx.ui.setStatus("minion", "");
        } catch { /* ignore */ }
        this.ctx = null;
      }
    }

    private render(minionState: MinionState) {
      if (!this.ctx || !minionState) return;

      const spinner = SPINNER_FRAMES[this.spinnerIdx % SPINNER_FRAMES.length];
      this.spinnerIdx++;

      const elapsedStr = elapsed(minionState.startTime);
      const phaseLabel = PHASE_LABELS[this.currentPhase] || this.currentPhase;

      // Line 1: spinner + phase + agent + elapsed
      let line1 = ` ${spinner} minion — ${phaseLabel}`;
      if (this.agentName) {
        line1 += `: ${this.agentName}`;
        if (this.agentModel) {
          line1 += ` (${this.agentModel})`;
        }
      }
      // Activity indicator
      const silentMs = Date.now() - this.lastProgress;
      if (this.agentName && silentMs > 10_000) {
        line1 += "  waiting...";
      }
      line1 += `        ${elapsedStr}`;

      // Line 2: horizontal rule
      const line2 = " " + "━".repeat(60);

      // Line 3: phase breadcrumbs
      const displayPhases = getDisplayPhases(this.pipelinePhases);
      const breadcrumbs = displayPhases.map((label) => {
        if (this.completedPhases.has(label)) {
          return `${PHASE_ICONS.done} ${label}`;
        } else if (label === phaseLabel) {
          return `${PHASE_ICONS.current} ${label}`;
        } else {
          return `${PHASE_ICONS.pending} ${label}`;
        }
      });
      const line3 = " " + breadcrumbs.join("  ");

      try {
        this.ctx.ui.setWidget("minion", [line1, line2, line3]);
        // Footer status: compact one-liner
        let footer = `${spinner} ${phaseLabel}`;
        if (this.agentName) footer += `: ${this.agentName}`;
        footer += ` — ${elapsedStr}`;
        this.ctx.ui.setStatus("minion", footer);
      } catch { /* ignore if API not available */ }
    }
  }

  const display = new MinionDisplay();

  function attachAgentListeners(ctx: any) {
    if (state) {
      display.start(ctx, state);
    }
  }

  function detachAgentListeners() {
    display.stop();
  }

  function setPhase(phase: Phase, ctx: any) {
    if (state) {
      display.updatePhase(phase, state);
    }
  }

  function recordFailure(phase: string, reason: string): void {
    const timestamp = new Date().toISOString();
    const report = `# Pipeline Failure\n\n- **Phase:** ${phase}\n- **Reason:** ${reason}\n- **Time:** ${timestamp}\n`;
    // Write individual report (writeState archives previous version)
    writeState("failure_report.md", report);
    // Also append to running failure log for quick debugging
    const logEntry = `- [${timestamp}] **${phase}**: ${reason}\n`;
    try {
      const { appendFileSync, mkdirSync } = require("node:fs");
      const { join } = require("node:path");
      const logPath = join(process.cwd(), ".pi/state/failure_log.md");
      mkdirSync(join(process.cwd(), ".pi/state"), { recursive: true });
      appendFileSync(logPath, logEntry, "utf-8");
    } catch { /* ignore append failures */ }
  }
}

// ─── Constants ───────────────────────────────────────────────────────────────

const MAX_REVIEW_ROUNDS = 2;

// ─── Helpers ────────────────────────────────────────────────────────────────

// applyFileBlocks is imported from tools.ts (shared line-by-line parser)

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
