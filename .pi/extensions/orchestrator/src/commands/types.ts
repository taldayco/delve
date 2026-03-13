// ─── Types ───────────────────────────────────────────────────────────────────

export type Phase =
  | "idle"
  | "pre_flight"
  | "branch"
  | "resolve_subsystem"
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
  | "memory_iterate"
  | "commit-pr"
  | "commit_wip"
  | "post_flight"
  | "done"
  | "failed";

export interface MinionState {
  prompt: string;
  branch: string;
  phase: Phase;
  startTime: number;
  buildFixRound: number;
  testFixRound: number;
  worktreePath?: string;
}
