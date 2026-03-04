import { readFileSync, existsSync } from "node:fs";
import { join } from "node:path";
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
} from "./agents.js";
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
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  getSubsystemCodebaseContext,
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
    worktreePath?: string;
  };
}

// applyFileBlocks is imported from tools.ts (shared line-by-line parser)

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
    if (result.ok && result.worktreePath) {
      ctx.data.worktreePath = result.worktreePath;
    }
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
    const subsystems = ctx.data.subsystems || resolveSubsystems(ctx.prompt);
    let plan: string;

    if (subsystems.length > 1) {
      // Parallel planning for multi-subsystem tasks
      const subsystemContexts: Record<string, string> = {};
      for (const sub of subsystems) {
        subsystemContexts[sub] = getSubsystemCodebaseContext(sub);
      }
      plan = await askParallelPlanner({ task: ctx.prompt, subsystemContexts });
    } else {
      // Single-agent planning for single-subsystem tasks
      const codebaseContext = getCodebaseContext(ctx.prompt);
      plan = await askMetaPlanner({ task: ctx.prompt, codebaseContext });
    }

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
      let subtasks = parseSubtasks(ctx.data.plan);

      // Retry planner with format correction if no subtasks parsed
      if (subtasks.length === 0) {
        const codebaseContext = getCodebaseContext(ctx.prompt);
        const correctedPlan = await askMetaPlanner({
          task: `Your previous output was:\n\n${ctx.data.plan}\n\nReformat this into the EXACT required format. Each subtask MUST use this header format:\n## Subtask N [subsystem]\n- Files: ...\n- Changes: ...\n- Acceptance criteria: ...\n\nValid subsystem tags: terrain, actor, shader, engine. Do NOT use ### or em-dashes. Use ## and [tag].`,
          codebaseContext: "",
        });
        subtasks = parseSubtasks(correctedPlan);
      }

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
          subsystems: subsystems,
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

    applyFileBlocks(implementation, ctx.data.worktreePath);
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
      });
      applyFileBlocks(fix, wt);
    }
    ctx.data.testsOk = testsOk;
    return { ok: testsOk, output: testsOk ? "Tests PASSED" : "Tests FAILED" };
  },

  review: async (ctx) => {
    const diff = getDiff(ctx.data.worktreePath);
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
};

// ─── Phase Output Validation ────────────────────────────────────────────────

interface ValidationResult {
  valid: boolean;
  warnings: string[];
}

const PHASE_VALIDATORS: Record<string, (output: string, ctx: BlueprintContext) => ValidationResult> = {
  plan: (output) => {
    const warnings: string[] = [];
    if (output.length < 50) warnings.push("Plan is suspiciously short");
    if (!/##?\s*(subtask|step|task)/i.test(output) && !/\d+\.\s/m.test(output)) {
      warnings.push("Plan lacks structured subtasks (no ## Subtask or numbered steps found)");
    }
    return { valid: warnings.length === 0, warnings };
  },

  implement: (output) => {
    const warnings: string[] = [];
    const fileBlocks = (output.match(/###\s*FILE:/g) || []).length;
    if (fileBlocks === 0) warnings.push("No FILE blocks produced — no code was generated");
    if (output.length < 100) warnings.push("Implementation output is suspiciously short");
    return { valid: fileBlocks > 0, warnings };
  },

  write_tests: (output) => {
    const warnings: string[] = [];
    const fileBlocks = (output.match(/###\s*FILE:/g) || []).length;
    if (fileBlocks === 0) warnings.push("No FILE blocks in test output — no test code generated");
    if (!/DELVE_TEST|TEST|test_/i.test(output)) {
      warnings.push("Output doesn't appear to contain test definitions");
    }
    return { valid: fileBlocks > 0, warnings };
  },

  review: (output) => {
    const warnings: string[] = [];
    const hasVerdict = /\b(APPROVE|REQUEST_CHANGES)\b/.test(output);
    if (!hasVerdict) warnings.push("Review lacks APPROVE/REQUEST_CHANGES verdict");
    if (output.length < 30) warnings.push("Review is suspiciously short");
    return { valid: hasVerdict, warnings };
  },

  diagnose: (output) => {
    const warnings: string[] = [];
    if (output.length < 50) warnings.push("Diagnosis is suspiciously short");
    return { valid: output.length >= 50, warnings };
  },
};

function validatePhaseOutput(handler: string, output: string, ctx: BlueprintContext): ValidationResult {
  const validator = PHASE_VALIDATORS[handler];
  if (!validator) return { valid: true, warnings: [] };
  return validator(output, ctx);
}

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
  try {
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

      // Validate agentic phase output quality
      if (result.ok && phase.type === "agentic") {
        const validation = validatePhaseOutput(phase.handler, result.output, context);
        if (validation.warnings.length > 0) {
          for (const w of validation.warnings) {
            context.ctx.ui.notify(`[validation] ${phase.name}: ${w}`, "warning");
          }
        }
      }

      if (result.ok) {
        context.ctx.ui.notify(`${phase.name}: ${result.output}`, "success");
      } else {
        context.ctx.ui.notify(`${phase.name} (optional): ${result.output}`, "warning");
      }
    }
  } finally {
    if (context.data.worktreePath) {
      cleanupWorktree(context.data.worktreePath);
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
