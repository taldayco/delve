import { agentEvents, writeState } from "../agents.js";
import { elapsed } from "../tools.js";
import type { Phase, MinionState } from "./types.js";

// ─── Constants ────────────────────────────────────────────────────────────────

export const SPINNER_FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

export const PHASE_ORDER: Phase[] = [
  "branch", "plan", "diagnose", "implement", "build", "fix-build",
  "write-tests", "test", "fix-tests", "review", "shader-validate", "commit-pr",
];

// Display labels: group fix-* phases with their parent
export const PHASE_LABELS: Record<string, string> = {
  "branch": "branch",
  "plan": "plan",
  "diagnose": "diagnose",
  "implement": "implement",
  "build": "build",
  "fix-build": "build",
  "write-tests": "tests",
  "test": "tests",
  "fix-tests": "tests",
  "review": "review",
  "shader-validate": "shaders",
  "commit-pr": "pr",
};

// Deduplicated display phases in order
export function getDisplayPhases(pipelinePhases: Phase[]): string[] {
  const seen = new Set<string>();
  const result: string[] = [];
  for (const p of pipelinePhases) {
    const label = PHASE_LABELS[p] || p;
    if (!seen.has(label)) {
      seen.add(label);
      result.push(label);
    }
  }
  return result;
}

// Phase icons for breadcrumbs
export const PHASE_ICONS = { done: "✓", current: "▸", pending: "○" };

// ─── MinionDisplay ────────────────────────────────────────────────────────────

export class MinionDisplay {
  private interval: ReturnType<typeof setInterval> | null = null;
  private spinnerIdx = 0;
  private agentName = "";
  private agentModel = "";
  private currentPhase: Phase = "idle";
  private lastProgress = 0;
  private pipelinePhases: Phase[] = [];
  private completedPhases = new Set<string>();
  private ctx: any = null;
  private _onStart: ((...args: any[]) => void) | null = null;
  private _onEnd: ((...args: any[]) => void) | null = null;
  private _onProgress: ((...args: any[]) => void) | null = null;

  start(ctx: any, minionState: MinionState, pipelinePhases?: Phase[]) {
    this.stop();
    this.ctx = ctx;
    this.currentPhase = minionState.phase;
    this.completedPhases.clear();
    this.agentName = "";
    this.agentModel = "";
    this.lastProgress = Date.now();
    this.pipelinePhases = pipelinePhases || PHASE_ORDER;

    // Event listeners
    this._onStart = ({ name, model }: { name: string; model?: string }) => {
      this.agentName = name;
      this.agentModel = model || "";
      this.lastProgress = Date.now();
    };
    this._onEnd = () => {
      this.agentName = "";
      this.agentModel = "";
    };
    this._onProgress = () => {
      this.lastProgress = Date.now();
    };

    agentEvents.on("agent:start", this._onStart);
    agentEvents.on("agent:end", this._onEnd);
    agentEvents.on("agent:progress", this._onProgress);

    // Render loop at 500ms
    this.interval = setInterval(() => this.render(minionState), 500);
    this.render(minionState);
  }

  updatePhase(phase: Phase, minionState: MinionState) {
    // Mark previous display label as completed
    if (this.currentPhase !== "idle") {
      this.completedPhases.add(PHASE_LABELS[this.currentPhase] || this.currentPhase);
    }
    this.currentPhase = phase;
    minionState.phase = phase;
    this.render(minionState);
  }

  stop() {
    if (this.interval) {
      clearInterval(this.interval);
      this.interval = null;
    }
    if (this._onStart) {
      agentEvents.removeListener("agent:start", this._onStart);
      this._onStart = null;
    }
    if (this._onEnd) {
      agentEvents.removeListener("agent:end", this._onEnd);
      this._onEnd = null;
    }
    if (this._onProgress) {
      agentEvents.removeListener("agent:progress", this._onProgress);
      this._onProgress = null;
    }
    if (this.ctx) {
      try {
        this.ctx.ui.setWidget("minion", []);
        this.ctx.ui.setStatus("minion", "");
      } catch { /* ignore */ }
      this.ctx = null;
    }
  }

  private render(minionState: MinionState) {
    if (!this.ctx || !minionState) return;

    const spinner = SPINNER_FRAMES[this.spinnerIdx % SPINNER_FRAMES.length];
    this.spinnerIdx++;

    const elapsedStr = elapsed(minionState.startTime);
    const phaseLabel = PHASE_LABELS[this.currentPhase] || this.currentPhase;

    // Line 1: spinner + phase + agent + elapsed
    let line1 = ` ${spinner} minion — ${phaseLabel}`;
    if (this.agentName) {
      line1 += `: ${this.agentName}`;
      if (this.agentModel) {
        line1 += ` (${this.agentModel})`;
      }
    }
    // Activity indicator
    const silentMs = Date.now() - this.lastProgress;
    if (this.agentName && silentMs > 10_000) {
      line1 += "  waiting...";
    }
    line1 += `        ${elapsedStr}`;

    // Line 2: horizontal rule
    const line2 = " " + "━".repeat(60);

    // Line 3: phase breadcrumbs
    const displayPhases = getDisplayPhases(this.pipelinePhases);
    const breadcrumbs = displayPhases.map((label) => {
      if (this.completedPhases.has(label)) {
        return `${PHASE_ICONS.done} ${label}`;
      } else if (label === phaseLabel) {
        return `${PHASE_ICONS.current} ${label}`;
      } else {
        return `${PHASE_ICONS.pending} ${label}`;
      }
    });
    const line3 = " " + breadcrumbs.join("  ");

    try {
      this.ctx.ui.setWidget("minion", [line1, line2, line3]);
      // Footer status: compact one-liner
      let footer = `${spinner} ${phaseLabel}`;
      if (this.agentName) footer += `: ${this.agentName}`;
      footer += ` — ${elapsedStr}`;
      this.ctx.ui.setStatus("minion", footer);
    } catch { /* ignore if API not available */ }
  }
}

// ─── Module-level display singleton and helpers ───────────────────────────────

export const display = new MinionDisplay();

export function attachAgentListeners(ctx: any, state: MinionState) {
  display.start(ctx, state);
}

export function detachAgentListeners() {
  display.stop();
}

export function setPhase(phase: Phase, state: MinionState | null) {
  if (state) {
    display.updatePhase(phase, state);
  }
}

// ─── Shared mutable state ─────────────────────────────────────────────────────
// All command handlers read/write this single nullable state to enforce
// the "only one minion at a time" invariant.

export let state: MinionState | null = null;

export function getState(): MinionState | null {
  return state;
}

export function setState(s: MinionState | null) {
  state = s;
}

export function recordFailure(phase: string, reason: string): void {
  const timestamp = new Date().toISOString();
  const report = `# Pipeline Failure\n\n- **Phase:** ${phase}\n- **Reason:** ${reason}\n- **Time:** ${timestamp}\n`;
  // Write individual report (writeState archives previous version)
  writeState("failure_report.md", report);
  // Also append to running failure log for quick debugging
  const logEntry = `- [${timestamp}] **${phase}**: ${reason}\n`;
  try {
    const { appendFileSync, mkdirSync } = require("node:fs");
    const { join } = require("node:path");
    const logPath = join(process.cwd(), ".pi/state/failure_log.md");
    mkdirSync(join(process.cwd(), ".pi/state"), { recursive: true });
    appendFileSync(logPath, logEntry, "utf-8");
  } catch { /* ignore append failures */ }
}
