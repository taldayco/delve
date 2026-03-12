import type { Blueprint } from "./types.js";

// ─── Builtin Blueprints ──────────────────────────────────────────────────────

export const BUILTIN_BLUEPRINTS: Record<string, Blueprint> = {
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
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
      { name: "Learn from run", type: "deterministic", handler: "memory_iterate", optional: true },
    ],
  },
};
