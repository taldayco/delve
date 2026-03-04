// ─── State Management ────────────────────────────────────────────────────────

export const PROTECTED_STATE_FILES = new Set(["metrics.jsonl", "audit_report.md", "history"]);

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
