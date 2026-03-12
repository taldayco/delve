// ─── State Management ────────────────────────────────────────────────────────

export const PROTECTED_STATE_FILES = new Set(["metrics.jsonl", "audit_report.md", "history", "run.lock"]);

export function cleanupStaleState(maxAgeDays = 7): { deleted: string[]; summary: string } {
  const { readdirSync, statSync, unlinkSync } = require("node:fs");
  const { join } = require("node:path");
  const stateDir = join(process.cwd(), ".pi/state");

  let entries: string[];
  try {
    entries = readdirSync(stateDir);
  } catch {
    return { deleted: [], summary: "No .pi/state directory found" };
  }

  const cutoff = Date.now() - maxAgeDays * 24 * 60 * 60 * 1000;
  const deleted: string[] = [];

  for (const entry of entries) {
    if (PROTECTED_STATE_FILES.has(entry)) continue;

    const fullPath = join(stateDir, entry);
    try {
      const stat = statSync(fullPath);
      if (stat.isFile() && stat.mtimeMs < cutoff) {
        unlinkSync(fullPath);
        deleted.push(entry);
      }
    } catch { /* ignore */ }
  }

  return {
    deleted,
    summary: deleted.length > 0
      ? `Cleaned ${deleted.length} stale state file(s): ${deleted.join(", ")}`
      : "No stale state files to clean up",
  };
}

// ─── File-Based Run Lock ──────────────────────────────────────────────────────

const LOCK_FILE = "run.lock";

export function acquireRunLock(): { acquired: true } | { acquired: false; reason: string } {
  const { existsSync, readFileSync, writeFileSync, mkdirSync, unlinkSync } = require("node:fs");
  const { join } = require("node:path");

  const stateDir = join(process.cwd(), ".pi/state");
  const lockPath = join(stateDir, LOCK_FILE);

  if (existsSync(lockPath)) {
    try {
      const content = readFileSync(lockPath, "utf-8");
      const { pid } = JSON.parse(content);
      try {
        process.kill(pid, 0);
        return { acquired: false, reason: `Another minion session is running (PID ${pid}). Wait or kill it.` };
      } catch {
        // PID is dead — stale lock, remove it
        unlinkSync(lockPath);
      }
    } catch {
      // Corrupt lock file — remove it
      try { unlinkSync(lockPath); } catch { /* ignore */ }
    }
  }

  try {
    mkdirSync(stateDir, { recursive: true });
    writeFileSync(lockPath, JSON.stringify({ pid: process.pid, timestamp: Date.now() }));
  } catch {
    return { acquired: false, reason: "Failed to write run lock file" };
  }

  return { acquired: true };
}

export function releaseRunLock(): void {
  const { unlinkSync } = require("node:fs");
  const { join } = require("node:path");
  const lockPath = join(process.cwd(), ".pi/state", LOCK_FILE);
  try { unlinkSync(lockPath); } catch { /* ignore */ }
}
