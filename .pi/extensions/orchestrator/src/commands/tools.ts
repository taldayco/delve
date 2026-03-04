import { Type } from "@mariozechner/pi-ai";
import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  askMetaPlanner,
  askParallelPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askWorker,
  askTerrainAgent,
  askActorAgent,
  askShaderAgent,
  askEngineAgent,
  generateWorkerPrompt,
  askVerifier,
  readRunMetrics,
} from "../agents.js";
import {
  runBuild,
  runTests,
  runMetrics,
  gitBranch,
  gitCommitAndPr,
  getSubsystemCodebaseContext,
  measureDomainComplexity,
} from "../tools.js";

export function registerTools(pi: ExtensionAPI) {
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
}
