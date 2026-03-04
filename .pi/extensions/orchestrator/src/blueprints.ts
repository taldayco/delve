import { readFileSync, existsSync } from "node:fs";
import { join } from "node:path";
import {
  askMetaPlanner,
  askMetaImplementer,
  askMetaTester,
  askReviewer,
  askBuildFixer,
  askTestFixer,
  askDiagnoser,
  getSubsystemAgent,
  parseSubtasks,
  writeState,
  readState,
} from "./agents.js";
import {
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

export interface Blueprint {
  name: string;
  description: string;
  phases: BlueprintPhase[];
}

export interface BlueprintPhase {
  name: string;
  type: "deterministic" | "agentic";
  handler: string;
  config?: Record<string, any>;
  optional?: boolean;
}

export interface BlueprintContext {
  prompt: string;
  branch: string;
  startTime: number;
  ctx: any;
  data: {
    subsystems?: string[];
    contextFiles?: string[];
    implementation?: string;
    plan?: string;
    diagnosis?: string;
    buildOk?: boolean;
    testsOk?: boolean;
    reviewDecision?: string;
  };
}

// ─── File Block Applier (shared with index.ts) ───────────────────────────────

function applyFileBlocks(text: string): number {
  const { writeFileSync, mkdirSync } = require("node:fs");
  const { dirname, join } = require("node:path");
  const cwd = process.cwd();

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
      console.error(`Failed to write ${fullPath}: ${e.message}`);
    }
  }
  return count;
}

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

// ─── Phase Handlers ─────────────────────────────────────────────────────────

type PhaseResult = { ok: boolean; output: string };
type PhaseHandler = (ctx: BlueprintContext) => Promise<PhaseResult>;

const PHASE_HANDLERS: Record<string, PhaseHandler> = {
  branch: async (ctx) => {
    const result = gitBranch(ctx.branch);
    return { ok: result.ok, output: result.summary };
  },

  resolve_subsystem: async (ctx) => {
    const subsystems = resolveSubsystems(ctx.prompt);
    const codebaseContext = getCodebaseContext(ctx.prompt);
    const contextFiles = extractFilePaths(codebaseContext);
    ctx.data.subsystems = subsystems;
    ctx.data.contextFiles = contextFiles;
    return { ok: true, output: `Subsystems: [${subsystems.join(", ")}]` };
  },

  plan: async (ctx) => {
    const codebaseContext = getCodebaseContext(ctx.prompt);
    const plan = await askMetaPlanner({ task: ctx.prompt, codebaseContext });
    ctx.data.plan = plan;
    return { ok: plan.length > 0, output: plan };
  },

  implement: async (ctx) => {
    const subsystems = ctx.data.subsystems || resolveSubsystems(ctx.prompt);
    const contextFiles = ctx.data.contextFiles || [];
    const taskWithDiagnosis = ctx.data.diagnosis
      ? `${ctx.prompt}\n\n## Diagnosis\n${ctx.data.diagnosis}`
      : ctx.prompt;

    let implementation: string;

    if (ctx.data.plan) {
      const subtasks = parseSubtasks(ctx.data.plan);
      if (subtasks.length > 0) {
        const implementations: string[] = [];
        for (const sub of subtasks) {
          const agent = getSubsystemAgent(sub.subsystem);
          const subFiles = extractFilePaths(sub.task).concat(contextFiles);
          implementations.push(await agent({ task: sub.task, files: subFiles }));
        }
        implementation = implementations.join("\n\n");
      } else {
        implementation = await askMetaImplementer({
          plan: ctx.data.plan,
          task: taskWithDiagnosis,
          files: contextFiles,
        });
      }
    } else {
      const targetSubsystem = subsystems[0] || "engine";
      const agent = getSubsystemAgent(targetSubsystem);
      implementation = await agent({ task: taskWithDiagnosis, files: contextFiles });
    }

    const fileBlockCount = (implementation.match(/###\s*FILE:/g) || []).length;
    if (fileBlockCount === 0) {
      return { ok: false, output: "No FILE blocks produced" };
    }

    applyFileBlocks(implementation);
    ctx.data.implementation = implementation;
    return { ok: true, output: `Applied ${fileBlockCount} files` };
  },

  build: async (ctx) => {
    let buildOk = false;
    for (let round = 0; round < MAX_BUILD_FIX_ROUNDS; round++) {
      const build = runBuild();
      if (build.ok) { buildOk = true; break; }
      if (round + 1 >= MAX_BUILD_FIX_ROUNDS) break;
      const fix = await askBuildFixer({
        buildOutput: build.summary,
        round: round + 1,
        maxRounds: MAX_BUILD_FIX_ROUNDS,
      });
      applyFileBlocks(fix);
    }
    ctx.data.buildOk = buildOk;
    return { ok: buildOk, output: buildOk ? "Build PASSED" : "Build FAILED after max attempts" };
  },

  write_tests: async (ctx) => {
    const changedFiles = getChangedFiles();
    const testCode = await askMetaTester({
      task: ctx.prompt,
      changedFiles,
      implementationSummary: (ctx.data.implementation || "").slice(0, 2000),
    });
    applyFileBlocks(testCode);
    return { ok: true, output: "Tests written" };
  },

  test: async (ctx) => {
    let testsOk = false;
    for (let round = 0; round < MAX_TEST_FIX_ROUNDS; round++) {
      const testResult = runTests();
      if (testResult.buildOk && testResult.testsOk) { testsOk = true; break; }
      if (round + 1 >= MAX_TEST_FIX_ROUNDS) break;
      const fix = await askTestFixer({
        testOutput: testResult.summary,
        round: round + 1,
        maxRounds: MAX_TEST_FIX_ROUNDS,
        isBuildFailure: !testResult.buildOk,
      });
      applyFileBlocks(fix);
    }
    ctx.data.testsOk = testsOk;
    return { ok: testsOk, output: testsOk ? "Tests PASSED" : "Tests FAILED" };
  },

  review: async (ctx) => {
    const diff = getDiff();
    const testResults = readState("test_results.json");
    const review = await askReviewer({
      task: ctx.prompt,
      diff,
      testResults: ctx.data.testsOk ? "All tests PASSED." : `Tests FAILED.\n${testResults}`,
    });

    const decision = /\bAPPROVE\b/.test(review) && !/\bREQUEST_CHANGES\b/.test(review)
      ? "APPROVE" : "REQUEST_CHANGES";
    ctx.data.reviewDecision = decision;
    return { ok: decision === "APPROVE", output: `Review: ${decision}` };
  },

  commit_pr: async (ctx) => {
    const result = gitCommitAndPr({
      prompt: ctx.prompt,
      branch: ctx.branch,
      buildOk: ctx.data.buildOk ?? false,
      testsOk: ctx.data.testsOk ?? false,
    });
    return { ok: result.ok, output: result.summary };
  },

  diagnose: async (ctx) => {
    const testResult = runTests();
    const { execSync } = require("node:child_process");
    let recentCommits = "";
    try {
      recentCommits = execSync("git log --oneline -10 2>/dev/null", { encoding: "utf-8" });
    } catch { /* ignore */ }

    const diagnosis = await askDiagnoser({
      task: ctx.prompt,
      testOutput: testResult.summary,
      recentCommits,
    });
    ctx.data.diagnosis = diagnosis;
    writeState("diagnosis.md", diagnosis);
    return { ok: diagnosis.length > 0, output: "Diagnosis complete" };
  },

  shader_validate: async (ctx) => {
    const result = runShaderValidation();
    return { ok: result.ok, output: result.ok ? "Shader validation PASSED" : result.summary };
  },
};

// ─── Available Phase Names ──────────────────────────────────────────────────

export function getAvailablePhases(): string[] {
  return Object.keys(PHASE_HANDLERS);
}

// ─── Builtin Blueprints ─────────────────────────────────────────────────────

const BUILTIN_BLUEPRINTS: Record<string, Blueprint> = {
  full: {
    name: "full",
    description: "Full pipeline: branch → resolve → implement → build → write tests → test → review → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Plan", type: "agentic", handler: "plan" },
      { name: "Implement", type: "agentic", handler: "implement" },
      { name: "Build", type: "deterministic", handler: "build" },
      { name: "Write tests", type: "agentic", handler: "write_tests" },
      { name: "Run tests", type: "deterministic", handler: "test" },
      { name: "Review", type: "agentic", handler: "review", optional: true },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
    ],
  },

  quick: {
    name: "quick",
    description: "Quick pipeline: branch → resolve → implement → build → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement" },
      { name: "Build", type: "deterministic", handler: "build" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
    ],
  },

  refactor: {
    name: "refactor",
    description: "Refactoring pipeline: branch → resolve → implement → build → review → PR (no tests)",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement" },
      { name: "Build", type: "deterministic", handler: "build" },
      { name: "Review", type: "agentic", handler: "review", optional: true },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
    ],
  },

  bugfix: {
    name: "bugfix",
    description: "Bugfix pipeline: branch → resolve → diagnose → implement → build → test → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Diagnose", type: "agentic", handler: "diagnose" },
      { name: "Implement", type: "agentic", handler: "implement" },
      { name: "Build", type: "deterministic", handler: "build" },
      { name: "Run tests", type: "deterministic", handler: "test" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
    ],
  },

  shader: {
    name: "shader",
    description: "Shader pipeline: branch → implement → build → shader-validate → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement" },
      { name: "Build", type: "deterministic", handler: "build" },
      { name: "Validate shaders", type: "deterministic", handler: "shader_validate" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
    ],
  },
};

// ─── Blueprint Loader ───────────────────────────────────────────────────────

export function loadBlueprint(name: string): Blueprint | null {
  // Check for custom blueprint file first
  const customPath = join(process.cwd(), `.pi/blueprints/${name}.json`);
  if (existsSync(customPath)) {
    try {
      const content = readFileSync(customPath, "utf-8");
      return JSON.parse(content) as Blueprint;
    } catch {
      // Fall through to builtins
    }
  }

  return BUILTIN_BLUEPRINTS[name] || null;
}

// ─── Blueprint Execution Engine ─────────────────────────────────────────────

export async function executeBlueprint(
  blueprint: Blueprint,
  context: BlueprintContext,
): Promise<void> {
  // Agent listeners are managed by MinionDisplay in index.ts — no need to
  // duplicate them here. We just notify on phase transitions.
  for (const phase of blueprint.phases) {
    const handler = PHASE_HANDLERS[phase.handler];
    if (!handler) {
      throw new Error(`Unknown phase handler: ${phase.handler}`);
    }

    context.ctx.ui.notify(`Phase: ${phase.name}`, "info");

    const result = await handler(context);

    if (!result.ok && !phase.optional) {
      context.ctx.ui.notify(`Phase ${phase.name} FAILED: ${result.output}`, "error");
      return;
    }

    if (result.ok) {
      context.ctx.ui.notify(`${phase.name}: ${result.output}`, "success");
    } else {
      context.ctx.ui.notify(`${phase.name} (optional): ${result.output}`, "warning");
    }
  }
}

// ─── Blueprint Validator ────────────────────────────────────────────────────

export function validateBlueprint(blueprint: any): blueprint is Blueprint {
  if (!blueprint || !blueprint.name || !Array.isArray(blueprint.phases)) return false;

  for (const phase of blueprint.phases) {
    if (!phase.handler || !PHASE_HANDLERS[phase.handler]) return false;
  }

  return true;
}
