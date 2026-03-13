import type { Blueprint } from "./types.js";

// ─── Builtin Blueprints ──────────────────────────────────────────────────────
//
// All blueprints are defined as TypeScript objects for type safety and
// co-location with the executor. Custom JSON overrides in .pi/blueprints/
// take precedence via loadBlueprint().

export const BUILTIN_BLUEPRINTS: Record<string, Blueprint> = {
  // ── full (backward-compat, linear) ──────────────────────────────────────
  full: {
    name: "full",
    description: "Full pipeline: branch → resolve → plan → implement → build → write tests → test → review → PR",
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
    ],
  },

  // ── branching-full (full pipeline with on_failure → commit_wip) ─────────
  "branching-full": {
    name: "branching-full",
    description: "Full pipeline with failure routing to commit_wip exit ramp",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Pre-flight", type: "deterministic", handler: "pre_flight", optional: true },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Plan", type: "agentic", handler: "plan", on_failure: "Save WIP" },
      { name: "Implement", type: "agentic", handler: "implement", on_failure: "Save WIP" },
      { name: "Build", type: "deterministic", handler: "build", max_retries: 2, on_failure: "Save WIP" },
      { name: "Write tests", type: "agentic", handler: "write_tests", on_failure: "Save WIP" },
      { name: "Run tests", type: "deterministic", handler: "test", max_retries: 2, on_failure: "Save WIP" },
      { name: "Review", type: "agentic", handler: "review", optional: true },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
      { name: "Post-flight", type: "deterministic", handler: "post_flight", optional: true },
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
      { name: "Save WIP", type: "deterministic", handler: "commit_wip", optional: true },
    ],
  },

  // ── quick (no planner, no tests, no review) ────────────────────────────
  quick: {
    name: "quick",
    description: "Quick pipeline: branch → resolve → implement → build → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement", on_failure: "Save WIP" },
      { name: "Build", type: "deterministic", handler: "build", max_retries: 2, on_failure: "Save WIP" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
      { name: "Save WIP", type: "deterministic", handler: "commit_wip", optional: true },
    ],
  },

  // ── refactor (no tests, behavior-preserving review) ─────────────────────
  refactor: {
    name: "refactor",
    description: "Refactoring pipeline: branch → resolve → implement → build → review → PR (no tests)",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement", on_failure: "Save WIP" },
      { name: "Build", type: "deterministic", handler: "build", max_retries: 2, on_failure: "Save WIP" },
      { name: "Review", type: "agentic", handler: "review", optional: true },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
      { name: "Save WIP", type: "deterministic", handler: "commit_wip", optional: true },
    ],
  },

  // ── bugfix (diagnose → implement, no test-writing) ──────────────────────
  bugfix: {
    name: "bugfix",
    description: "Bugfix pipeline: branch → resolve → diagnose → implement → build → test → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Diagnose", type: "agentic", handler: "diagnose" },
      { name: "Implement", type: "agentic", handler: "implement", on_failure: "Save WIP" },
      { name: "Build", type: "deterministic", handler: "build", max_retries: 2, on_failure: "Save WIP" },
      { name: "Run tests", type: "deterministic", handler: "test", max_retries: 2, on_failure: "Save WIP" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
      { name: "Save WIP", type: "deterministic", handler: "commit_wip", optional: true },
    ],
  },

  // ── shader (shader validation instead of tests) ────────────────────────
  shader: {
    name: "shader",
    description: "Shader pipeline: branch → resolve → implement → build → shader-validate → PR",
    phases: [
      { name: "Create branch", type: "deterministic", handler: "branch" },
      { name: "Resolve subsystems", type: "deterministic", handler: "resolve_subsystem" },
      { name: "Implement", type: "agentic", handler: "implement", on_failure: "Save WIP" },
      { name: "Build", type: "deterministic", handler: "build", max_retries: 2, on_failure: "Save WIP" },
      { name: "Validate shaders", type: "deterministic", handler: "shader_validate", on_failure: "Save WIP" },
      { name: "Commit & PR", type: "deterministic", handler: "commit_pr" },
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
      { name: "Save WIP", type: "deterministic", handler: "commit_wip", optional: true },
    ],
  },
};
