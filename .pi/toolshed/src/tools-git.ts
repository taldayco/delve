import { shell } from "./shell.js";

// ─── git_status ─────────────────────────────────────────────────────────────

export const gitStatusSchema = {};

export async function gitStatus(_args: Record<string, never>) {
  const branch = shell("git branch --show-current 2>/dev/null");
  const status = shell("git status --porcelain 2>/dev/null");

  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          branch: branch.stdout.trim(),
          files: status.stdout
            .trim()
            .split("\n")
            .filter(Boolean)
            .map((line) => ({
              status: line.substring(0, 2).trim(),
              file: line.substring(3),
            })),
        }),
      },
    ],
  };
}

// ─── git_diff_from_main ─────────────────────────────────────────────────────

export const gitDiffFromMainSchema = {};

export async function gitDiffFromMain(_args: Record<string, never>) {
  const result = shell("git diff --stat main 2>/dev/null");
  const files = shell("git diff --name-only main 2>/dev/null");
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          summary: result.stdout.trim().split("\n").slice(-1)[0] || "no changes",
          files: files.stdout.trim().split("\n").filter(Boolean),
        }),
      },
    ],
  };
}
