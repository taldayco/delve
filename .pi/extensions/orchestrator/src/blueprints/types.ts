// ─── Types ───────────────────────────────────────────────────────────────────

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
  // B1: Branching pipeline fields
  on_success?: string;   // phase name to jump to on success (default: next in array)
  on_failure?: string;   // phase name to jump to on failure (default: abort)
  max_retries?: number;  // retry this phase N times before following on_failure
  skip_if?: string;      // skip phase if named context data key is truthy
}

export interface BlueprintContext {
  prompt: string;
  branch: string;
  startTime: number;
  ctx: any;
  signal?: AbortSignal;
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
    // B1: Additional state for branching pipeline
    [key: string]: any;
  };
}

export interface BlueprintResult {
  ok: boolean;
  failedPhase?: string;
  errorMessage?: string;
  completedPhases: string[];
  worktreePath?: string;
}
