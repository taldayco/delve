// ─── Agent Types & Interfaces ─────────────────────────────────────────────────

export interface SpawnSubagentOpts {
  prompt: string;
  systemPrompt: string;
  model?: string;
  thinking?: string;
  tools?: string[];
  signal?: AbortSignal;
  agentName?: string;
  timeoutMs?: number;
  cwd?: string;
}

/**
 * A focused subtask produced by a Sonnet meta-agent decomposer.
 * Each subtask targets exactly one file and includes a self-contained
 * worker prompt so Haiku workers need no additional context.
 */
export interface WorkerSubtask {
  file: string;
  action: "CREATE" | "MODIFY";
  instructions: string;
  context_files: string[];
  worker_prompt: string;
}

/**
 * Structured output from a meta-agent decomposer.
 */
export interface MetaDecomposition {
  subtasks: WorkerSubtask[];
}

export interface RunMetricsRecord {
  runId: string;
  timestamp: string;
  agentName: string;
  model: string;
  phase: string;
  durationMs: number;
  contextUsagePct: number;
  success: boolean;
  escalated: boolean;
  truncated: boolean;
}

export interface AgentConfig {
  name: string;
  tools: string[];
  model: string;
  thinking?: string;
}

export interface Subtask {
  subsystem: string;
  task: string;
}

export interface SubsystemAgentOpts {
  task: string;
  files: string[];
  cwd?: string;
  signal?: AbortSignal;
}

export interface FailureSignal {
  failed: boolean;
  reason: string;
  category: "capability" | "tool" | "context" | "unknown";
}

export type ErrorSeverity = "syntactic" | "structural" | "architectural";
export interface ErrorClassification {
  severity: ErrorSeverity;
  summary: string;
  affected_files: string[];
}

export interface TruncateResult {
  text: string;
  droppedSections: number;
  droppedChars: number;
}
