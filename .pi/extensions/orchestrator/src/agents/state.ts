// ─── State File I/O & Run Metrics ─────────────────────────────────────────────

import { readFileSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import * as fs from "node:fs";
import { join } from "node:path";
import { PROJECT_ROOT } from "./config.js";
import type { RunMetricsRecord } from "./types.js";

export const STATE_DIR = join(PROJECT_ROOT, ".pi/state");

export function writeState(filename: string, content: string): void {
  mkdirSync(STATE_DIR, { recursive: true });
  const filePath = join(STATE_DIR, filename);

  // Archive previous version before overwriting
  if (existsSync(filePath)) {
    const historyDir = join(STATE_DIR, "history", new Date().toISOString().replace(/[:.]/g, "-"));
    mkdirSync(historyDir, { recursive: true });
    try {
      fs.copyFileSync(filePath, join(historyDir, filename));
    } catch { /* ignore archive failures */ }
    pruneStateHistory();
  }

  writeFileSync(filePath, content, "utf-8");
}

/**
 * Keep only the last N history snapshots (default 5).
 */
export function pruneStateHistory(maxSnapshots = 5): void {
  const historyRoot = join(STATE_DIR, "history");
  if (!existsSync(historyRoot)) return;
  try {
    const dirs = fs.readdirSync(historyRoot)
      .filter((d) => {
        try { return fs.statSync(join(historyRoot, d)).isDirectory(); } catch { return false; }
      })
      .sort();
    while (dirs.length > maxSnapshots) {
      const oldest = dirs.shift()!;
      const dirPath = join(historyRoot, oldest);
      try {
        for (const f of fs.readdirSync(dirPath)) {
          fs.unlinkSync(join(dirPath, f));
        }
        fs.rmdirSync(dirPath);
      } catch { /* ignore */ }
    }
  } catch { /* ignore */ }
}

export function readState(filename: string): string {
  const path = join(STATE_DIR, filename);
  return existsSync(path) ? readFileSync(path, "utf-8") : "";
}

// ─── Run Metrics Tracking ──────────────────────────────────────────────────

export const METRICS_FILE = join(STATE_DIR, "metrics.jsonl");

export function appendRunMetrics(record: RunMetricsRecord): void {
  mkdirSync(STATE_DIR, { recursive: true });
  fs.appendFileSync(METRICS_FILE, JSON.stringify(record) + "\n", "utf-8");
}

export function readRunMetrics(maxRecords = 50): RunMetricsRecord[] {
  if (!existsSync(METRICS_FILE)) return [];
  const lines = readFileSync(METRICS_FILE, "utf-8").trim().split("\n").filter(Boolean);
  const records: RunMetricsRecord[] = [];
  const start = Math.max(0, lines.length - maxRecords);
  for (let i = start; i < lines.length; i++) {
    try {
      records.push(JSON.parse(lines[i]));
    } catch { /* skip malformed lines */ }
  }
  return records;
}

export function pruneMetricsLog(maxLines = 500): number {
  if (!existsSync(METRICS_FILE)) return 0;
  const lines = readFileSync(METRICS_FILE, "utf-8").trim().split("\n").filter(Boolean);
  if (lines.length <= maxLines) return 0;
  const pruned = lines.length - maxLines;
  writeFileSync(METRICS_FILE, lines.slice(pruned).join("\n") + "\n", "utf-8");
  return pruned;
}

export let metricsRunCounter = 0;

export function incrementMetricsRunCounter(): number {
  return ++metricsRunCounter;
}
