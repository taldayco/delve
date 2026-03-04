// ─── Shell Helpers ───────────────────────────────────────────────────────────

import { execSync } from "node:child_process";
export { silentShell } from "../agents.js";

export function shell(cmd: string, cwd?: string): { ok: boolean; stdout: string; stderr: string } {
  try {
    const stdout = execSync(cmd, {
      cwd: cwd || process.cwd(),
      encoding: "utf-8",
      timeout: 3_600_000,
      maxBuffer: 10 * 1024 * 1024,
      stdio: ["pipe", "pipe", "pipe"],
    });
    return { ok: true, stdout: stdout || "", stderr: "" };
  } catch (e: any) {
    return {
      ok: false,
      stdout: (e.stdout || "").toString(),
      stderr: (e.stderr || e.message || "").toString(),
    };
  }
}

export function slugify(text: string): string {
  return (text || "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 40);
}

export function elapsed(startTime: number): string {
  const secs = Math.round((Date.now() - startTime) / 1000);
  if (secs < 60) return `${secs}s`;
  return `${Math.floor(secs / 60)}m${secs % 60}s`;
}
