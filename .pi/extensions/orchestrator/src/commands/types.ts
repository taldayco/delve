// ─── Types ───────────────────────────────────────────────────────────────────

export type Phase =
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

export interface MinionState {
  prompt: string;
  branch: string;
  phase: Phase;
  startTime: number;
  buildFixRound: number;
  testFixRound: number;
  worktreePath?: string;
}
