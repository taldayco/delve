import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  askMetaPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askWorker,
  askBuildFixer,
  askTestFixer,
  generateWorkerPrompt,
  writeState,
  readState,
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
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
} from "./tools.js";

// ─── Types ──────────────────────────────────────────────────────────────────

type Phase =
  | "idle"
  | "branch"
  | "plan"
  | "implement"
  | "build"
  | "fix-build"
  | "write-tests"
  | "test"
  | "fix-tests"
  | "review"
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

  pi.registerTool("ask_meta_planner", {
    description:
      "Decompose a task into ordered subtasks. Returns a structured plan. (Sonnet-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        task: { type: "string" as const, description: "The feature/bug description" },
        codebase_context: {
          type: "string" as const,
          description: "Relevant file listings, types, and structure info",
        },
      },
      required: ["task", "codebase_context"],
    },
    handler: async (args: any) => {
      return askMetaPlanner({
        task: args.task,
        codebaseContext: args.codebase_context,
      });
    },
  });

  pi.registerTool("ask_meta_implementer", {
    description:
      "Given a plan and file list, produce complete implementation changes. (Sonnet-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        plan: { type: "string" as const, description: "The structured plan from ask_meta_planner" },
        task: { type: "string" as const, description: "Original task description" },
        files: {
          type: "array" as const,
          items: { type: "string" as const },
          description: "File paths the implementer needs to read",
        },
      },
      required: ["plan", "task", "files"],
    },
    handler: async (args: any) => {
      return askMetaImplementer({
        plan: args.plan,
        task: args.task,
        files: args.files,
      });
    },
  });

  pi.registerTool("ask_meta_tester", {
    description:
      "Generate test code for implemented changes. (Sonnet-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        task: { type: "string" as const, description: "Original task description" },
        changed_files: {
          type: "array" as const,
          items: { type: "string" as const },
          description: "Paths of files that were changed",
        },
        implementation_summary: {
          type: "string" as const,
          description: "Summary of what was implemented",
        },
      },
      required: ["task", "changed_files", "implementation_summary"],
    },
    handler: async (args: any) => {
      return askMetaTester({
        task: args.task,
        changedFiles: args.changed_files,
        implementationSummary: args.implementation_summary,
      });
    },
  });

  pi.registerTool("ask_reviewer", {
    description:
      "Review a diff for correctness, safety, and consistency. Returns APPROVE or REQUEST_CHANGES. (Sonnet-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        task: { type: "string" as const, description: "Original task description" },
        diff: { type: "string" as const, description: "Git diff of changes" },
        test_results: { type: "string" as const, description: "Test output summary" },
      },
      required: ["task", "diff", "test_results"],
    },
    handler: async (args: any) => {
      return askReviewer({
        task: args.task,
        diff: args.diff,
        testResults: args.test_results,
      });
    },
  });

  pi.registerTool("ask_worker", {
    description:
      "Execute a single, well-scoped task. The leaf agent — cheap and fast. (Haiku-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        system_prompt: { type: "string" as const, description: "System prompt for the worker" },
        task: { type: "string" as const, description: "The specific task to perform" },
        file_contents: {
          type: "object" as const,
          description: "Map of file path → content the worker needs",
        },
      },
      required: ["system_prompt", "task"],
    },
    handler: async (args: any) => {
      return askWorker({
        systemPrompt: args.system_prompt,
        task: args.task,
        fileContents: args.file_contents,
      });
    },
  });

  pi.registerTool("generate_worker_prompt", {
    description:
      "Create a specialized system+user prompt for a Haiku worker. Meta-tool: prompts that create prompts. (Sonnet-tier, stateless)",
    parameters: {
      type: "object" as const,
      properties: {
        task_type: { type: "string" as const, description: "Type of task (e.g., fix_build, add_palette)" },
        context: { type: "string" as const, description: "Context for prompt generation" },
        constraints: {
          type: "array" as const,
          items: { type: "string" as const },
          description: "Non-negotiable constraints for the worker",
        },
      },
      required: ["task_type", "context", "constraints"],
    },
    handler: async (args: any) => {
      const result = await generateWorkerPrompt({
        taskType: args.task_type,
        context: args.context,
        constraints: args.constraints,
      });
      return JSON.stringify(result);
    },
  });

  // ── Register Deterministic Tools ────────────────────────────────────────

  pi.registerTool("run_build", {
    description:
      "Run cmake configure + build. Returns exit status and last 50 lines of output. Full log written to .pi/state/build_log.txt.",
    parameters: { type: "object" as const, properties: {} },
    handler: async () => {
      const result = runBuild();
      return JSON.stringify({ ok: result.ok, summary: result.summary });
    },
  });

  pi.registerTool("run_tests", {
    description:
      "Build and run delve_tests. Returns build/test status and output summary. Full results written to .pi/state/test_results.json.",
    parameters: { type: "object" as const, properties: {} },
    handler: async () => {
      const result = runTests();
      return JSON.stringify({
        buildOk: result.buildOk,
        testsOk: result.testsOk,
        summary: result.summary,
      });
    },
  });

  pi.registerTool("git_branch", {
    description: "Create a new git branch from main.",
    parameters: {
      type: "object" as const,
      properties: {
        branch_name: { type: "string" as const, description: "Branch name to create" },
      },
      required: ["branch_name"],
    },
    handler: async (args: any) => {
      const result = gitBranch(args.branch_name);
      return JSON.stringify(result);
    },
  });

  pi.registerTool("git_commit_and_pr", {
    description:
      "Stage all changes, commit, push, and create a PR. Returns the PR URL.",
    parameters: {
      type: "object" as const,
      properties: {
        prompt: { type: "string" as const, description: "Feature description for commit message" },
        branch: { type: "string" as const, description: "Branch name" },
        build_ok: { type: "boolean" as const, description: "Did the build pass?" },
        tests_ok: { type: "boolean" as const, description: "Did the tests pass?" },
      },
      required: ["prompt", "branch", "build_ok", "tests_ok"],
    },
    handler: async (args: any) => {
      const result = gitCommitAndPr({
        prompt: args.prompt,
        branch: args.branch,
        buildOk: args.build_ok,
        testsOk: args.tests_ok,
      });
      return JSON.stringify(result);
    },
  });

  // ── /minion command ─────────────────────────────────────────────────────
  // Orchestrates the full pipeline by calling registered tools sequentially.
  // The Opus main session only sees tool call summaries — each sub-agent
  // is a stateless ask() call that starts fresh.

  pi.registerCommand("minion", {
    description:
      "Run the meta-agentic development pipeline: branch → plan → implement → build → test → review → PR",
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

        // ── PHASE: Plan (Sonnet meta-agent) ───────────────────────────
        setPhase("plan", ctx);
        const codebaseContext = getCodebaseContext(prompt);
        const plan = await askMetaPlanner({
          task: prompt,
          codebaseContext,
        });
        ctx.ui.notify(`Plan complete (${plan.length} chars)`, "success");

        // Extract file paths from the plan for the implementer
        const planFiles = extractFilePaths(plan);

        // ── PHASE: Implement (Sonnet meta-agent → Haiku workers) ──────
        setPhase("implement", ctx);
        const implementation = await askMetaImplementer({
          plan,
          task: prompt,
          files: planFiles,
        });

        // Apply the implementation changes via the pi session
        // The meta-implementer returns file changes — send them to the main agent to apply
        pi.sendUserMessage(
          `Apply the following implementation changes. For each FILE section, create or modify the file with the provided content. Only change the files listed — nothing else.\n\n${implementation}`
        );
        await ctx.waitForIdle();
        ctx.ui.notify("Implementation applied", "success");

        // ── PHASE: Build + Fix Loop ───────────────────────────────────
        let buildOk = false;
        for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
          state.buildFixRound = round + 1;
          setPhase("build", ctx);

          const build = runBuild();

          if (build.ok) {
            ctx.ui.notify(`Build passed (round ${round + 1})`, "success");
            buildOk = true;
            break;
          }

          ctx.ui.notify(
            `Build failed (round ${round + 1}/${MAX_BUILD_FIX_ROUNDS})`,
            "warn"
          );

          if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;

          // Ask Haiku worker to diagnose the fix, then have pi apply it
          setPhase("fix-build", ctx);
          const fix = await askBuildFixer({
            buildOutput: build.summary,
            round: round + 1,
            maxRounds: MAX_BUILD_FIX_ROUNDS,
          });

          pi.sendUserMessage(
            `The build failed. Apply these fixes:\n\n${fix}\n\nOnly fix the compilation errors — don't refactor.`
          );
          await ctx.waitForIdle();
        }

        if (!buildOk) {
          state.phase = "failed";
          ctx.ui.notify("Build failed after max attempts", "error");
          pi.sendUserMessage(
            "The build failed after all attempts. Summarize what went wrong."
          );
          return;
        }

        // ── PHASE: Write Tests (Sonnet meta-agent) ────────────────────
        setPhase("write-tests", ctx);
        const changedFiles = getChangedFiles();
        const testCode = await askMetaTester({
          task: prompt,
          changedFiles,
          implementationSummary: implementation.slice(0, 2000),
        });

        // Apply test code via pi session
        pi.sendUserMessage(
          `Apply the following test code changes. Create or modify files as specified.\n\n${testCode}`
        );
        await ctx.waitForIdle();
        ctx.ui.notify("Tests written", "success");

        // ── PHASE: Test + Fix Loop ────────────────────────────────────
        let testsOk = false;
        for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
          state.testFixRound = round + 1;
          setPhase("test", ctx);

          const testResult = runTests();

          if (testResult.buildOk && testResult.testsOk) {
            ctx.ui.notify(`Tests passed (round ${round + 1})`, "success");
            testsOk = true;
            break;
          }

          const isBuildFailure = !testResult.buildOk;
          ctx.ui.notify(
            `${isBuildFailure ? "Test build" : "Tests"} failed (round ${round + 1}/${MAX_TEST_FIX_ROUNDS})`,
            "warn"
          );

          if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;

          // Ask Haiku worker to fix, then have pi apply
          setPhase("fix-tests", ctx);
          const fix = await askTestFixer({
            testOutput: testResult.summary,
            round: round + 1,
            maxRounds: MAX_TEST_FIX_ROUNDS,
            isBuildFailure,
          });

          pi.sendUserMessage(
            `${isBuildFailure ? "Test build" : "Tests"} failed. Apply these fixes:\n\n${fix}\n\nOnly fix the errors — don't refactor.`
          );
          await ctx.waitForIdle();
        }

        // ── PHASE: Review (Sonnet meta-agent) ─────────────────────────
        setPhase("review", ctx);
        const diff = getDiff();
        const testResults = readState("test_results.json");
        const review = await askReviewer({
          task: prompt,
          diff,
          testResults: testsOk ? "All tests PASSED." : `Tests FAILED.\n${testResults}`,
        });
        ctx.ui.notify(
          `Review: ${review.includes("APPROVE") ? "APPROVED" : "CHANGES REQUESTED"}`,
          review.includes("APPROVE") ? "success" : "warn"
        );

        // If review requests changes, have pi apply them
        if (review.includes("REQUEST_CHANGES")) {
          pi.sendUserMessage(
            `The code review found issues. Fix these:\n\n${review}\n\nOnly fix the specific issues listed.`
          );
          await ctx.waitForIdle();
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
          ctx.ui.notify(prResult.summary, "warn");
        }

        // ── Done ──────────────────────────────────────────────────────
        state.phase = "done";
        ctx.ui.notify(
          `Minion complete in ${elapsed(state.startTime)}. Branch: ${branch}`,
          "success"
        );
        state = null;
      } catch (error: any) {
        state!.phase = "failed";
        ctx.ui.notify(`Minion error: ${error.message}`, "error");
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
      const cmd = event.input?.command || "";
      const dangerous =
        cmd.includes("rm -rf /") ||
        cmd.includes("git push --force") ||
        cmd.includes("git push -f") ||
        (cmd.includes("git reset --hard") && cmd.includes("main"));
      if (dangerous) {
        return { block: true, reason: "Blocked dangerous operation: " + cmd };
      }
    }
  });

  // ── Status line tracking ────────────────────────────────────────────────

  function setPhase(phase: Phase, ctx: any) {
    if (state) {
      state.phase = phase;
      ctx.ui.setStatus("minion", `Minion: ${phase} [${elapsed(state.startTime)}]`);
    }
  }
}

// ─── Helpers ────────────────────────────────────────────────────────────────

/**
 * Extract file paths from a plan markdown.
 * Looks for patterns like `src/game/terrain/foo.cpp` in the text.
 */
function extractFilePaths(plan: string): string[] {
  const regex = /\b(src\/[a-zA-Z0-9_/]+\.[a-zA-Z]+)\b/g;
  const paths = new Set<string>();
  let match;
  while ((match = regex.exec(plan)) !== null) {
    paths.add(match[1]);
  }
  // Also look for CMakeLists.txt references
  if (plan.includes("CMakeLists.txt")) {
    paths.add("CMakeLists.txt");
  }
  return Array.from(paths);
}
