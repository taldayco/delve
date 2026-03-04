import { execSync } from "node:child_process";
import { existsSync, readdirSync } from "node:fs";
import { join, resolve } from "node:path";

// ─── Project Root Detection ─────────────────────────────────────────────────

function findProjectRoot(): string {
  let dir = process.cwd();
  while (dir !== "/") {
    if (existsSync(join(dir, "CMakeLists.txt")) && existsSync(join(dir, "src"))) {
      return dir;
    }
    dir = resolve(dir, "..");
  }
  return process.cwd();
}

export const PROJECT_ROOT = resolve(
  process.env.DELVE_PROJECT_ROOT || findProjectRoot()
);

// ─── Shell Helper ───────────────────────────────────────────────────────────

export function shell(
  cmd: string,
  timeout = 3_600_000
): { ok: boolean; stdout: string; stderr: string } {
  try {
    const stdout = execSync(cmd, {
      cwd: PROJECT_ROOT,
      encoding: "utf-8",
      timeout,
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

// ─── Recursive File Listing ──────────────────────────────────────────────────

export function listFilesRecursive(dir: string, root: string): string[] {
  const results: string[] = [];
  try {
    const entries = readdirSync(dir, { withFileTypes: true });
    for (const entry of entries) {
      const fullPath = join(dir, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === "node_modules" || entry.name === "build") continue;
        results.push(...listFilesRecursive(fullPath, root));
      } else {
        results.push(fullPath.replace(root + "/", ""));
      }
    }
  } catch {}
  return results;
}
