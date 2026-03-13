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
  | "fix_build"
  | "write_tests"
  | "test"
  | "fix_tests"
  | "review"
  | "shader_validate"
  | "verify"
  | "memory_iterate"
  | "commit_pr"
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
