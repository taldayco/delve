// ─── Types ───────────────────────────────────────────────────────────────────

export type Phase =
  | "idle"
  | "pre_flight"
  | "branch"
  | "resolve_subsystem"
  | "plan"
  | "diagnose"
  | "research"
  | "math_verify"
  | "worker_fan_out"
  | "implement"
  | "build"
  | "fix-build"
  | "write-tests"
  | "test"
  | "fix-tests"
  | "review"
  | "shader-validate"
  | "verify"
  | "memory_iterate"
  | "commit-pr"
  | "commit_wip"
  | "post_flight"
  | "done"
  | "failed"
  | `dynamic:${string}`;

export interface MinionState {
  prompt: string;
  branch: string;
  phase: Phase;
  startTime: number;
  buildFixRound: number;
  testFixRound: number;
  worktreePath?: string;
}
