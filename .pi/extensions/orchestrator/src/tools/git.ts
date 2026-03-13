// ─── Git Tools ───────────────────────────────────────────────────────────────

import { tmpdir } from "node:os";
import { silentShell, writeState } from "../agents.js";
import { PROJECT_ROOT } from "../agents/config.js";
import { shell } from "./shell.js";

const WORKTREE_DIR = ".pi/worktrees";

/**
 * Create a git worktree for the given branch.
 * Returns the worktree path on success.
 */
export function gitBranch(branchName: string): { ok: boolean; summary: string; worktreePath?: string } {
  // Ensure we're on main and up to date
  shell("git checkout main 2>&1 && git pull --ff-only 2>&1");

  // Create worktree directory
  const safeName = branchName.replace(/[^a-zA-Z0-9._-]/g, "-");
  const worktreePath = `${WORKTREE_DIR}/${safeName}`;

  // Clean up existing worktree at this path if it exists
  shell(`git worktree remove --force "${worktreePath}" 2>&1`);

  // Delete stale branch from previous runs if it exists
  shell(`git branch -D "${branchName}" 2>&1`);

  const result = shell(
    `git worktree add -b "${branchName}" "${worktreePath}" main 2>&1`
  );
  if (!result.ok) {
    return { ok: false, summary: `Failed to create worktree: ${result.stderr}` };
  }
  return { ok: true, summary: `Worktree created: ${worktreePath} (branch: ${branchName})`, worktreePath };
}

/**
 * Force-remove a git worktree directory and its associated branch tracking.
 *
 * Called in `finally` blocks after pipeline completion (success or failure).
 * Uses `--force` to handle dirty worktrees — any uncommitted work is lost.
 * Callers that want to preserve partial progress should run `commit_wip`
 * before reaching this point (e.g. via an `on_failure` blueprint route).
 */
export function cleanupWorktree(worktreePath: string): void {
  try {
    shell(`git worktree remove --force "${worktreePath}" 2>&1`);
  } catch { /* ignore */ }
}

/**
 * List all dirs under .pi/worktrees/, check if each git worktree is still
 * registered, and remove orphaned worktree dirs.
 */
export function cleanupStaleWorktrees(): { removed: string[]; summary: string } {
  const { readdirSync, statSync, rmSync } = require("node:fs");
  const { join } = require("node:path");
  const worktreesDir = join(process.cwd(), ".pi/worktrees");
  const removed: string[] = [];

  // Get list of registered git worktrees
  const worktreeList = shell("git worktree list --porcelain 2>/dev/null");
  const registeredPaths = new Set<string>();
  if (worktreeList.ok) {
    for (const line of worktreeList.stdout.split("\n")) {
      if (line.startsWith("worktree ")) {
        registeredPaths.add(line.slice(9).trim());
      }
    }
  }

  let entries: string[];
  try {
    entries = readdirSync(worktreesDir);
  } catch {
    return { removed: [], summary: "No .pi/worktrees directory found" };
  }

  for (const entry of entries) {
    const fullPath = join(worktreesDir, entry);
    try {
      if (!statSync(fullPath).isDirectory()) continue;
      if (!registeredPaths.has(fullPath)) {
        // Orphaned worktree dir — remove it
        rmSync(fullPath, { recursive: true, force: true });
        removed.push(entry);
      }
    } catch { /* ignore */ }
  }

  return {
    removed,
    summary: removed.length > 0
      ? `Removed ${removed.length} orphaned worktree dir(s): ${removed.join(", ")}`
      : "No orphaned worktree dirs found",
  };
}

export function gitCommitAndPr(opts: {
  prompt: string;
  branch: string;
  buildOk: boolean;
  testsOk: boolean;
  cwd?: string;
}): { ok: boolean; prUrl: string; summary: string } {
  const cwd = opts.cwd;

  // Stage all changes
  const addResult = shell("git add -A 2>&1", cwd);
  if (!addResult.ok) {
    return { ok: false, prUrl: "", summary: `git add failed: ${addResult.stderr}` };
  }

  // Check for changes
  const statusResult = shell("git status --porcelain 2>&1", cwd);
  if (statusResult.stdout.trim().length === 0) {
    return { ok: false, prUrl: "", summary: "No changes to commit" };
  }

  // Collect changed files for PR body
  const diffResult = shell("git diff --cached --name-only 2>/dev/null", cwd);
  const changedFiles = diffResult.stdout
    .trim()
    .split("\n")
    .filter((f) => f.length > 0);

  // Commit — use temp file to avoid shell injection via opts.prompt
  const { writeFileSync: writeTmp, unlinkSync } = require("node:fs");
  const { join: joinPath } = require("node:path");
  const commitMsg = `feat: ${opts.prompt}\n\nImplemented by Delve minion agent (meta-agentic).\nBuild: ${opts.buildOk ? "PASS" : "FAIL"}\nTests: ${opts.testsOk ? "PASS" : "FAIL"}`;
  const tmpCommitFile = joinPath(tmpdir(), `delve-commit-${Date.now()}.txt`);
  writeTmp(tmpCommitFile, commitMsg, "utf-8");
  const commitResult = shell(`git commit -F "${tmpCommitFile}" 2>&1`, cwd);
  try { unlinkSync(tmpCommitFile); } catch { /* ignore */ }
  if (!commitResult.ok) {
    return { ok: false, prUrl: "", summary: `git commit failed: ${commitResult.stderr}` };
  }

  // Push
  const safeBranch = opts.branch.replace(/[^a-zA-Z0-9._\/-]/g, "-");
  const pushResult = shell(`git push -u origin "${safeBranch}" 2>&1`, cwd);
  if (!pushResult.ok) {
    return { ok: false, prUrl: "", summary: `git push failed: ${pushResult.stderr}` };
  }

  // Create PR — use temp file for body to avoid shell injection
  const prTitle = `feat: ${opts.prompt.slice(0, 60).replace(/[^a-zA-Z0-9 _.,:;!?()-]/g, "")}`;
  const prBody = buildPrBody(opts.prompt, changedFiles, opts.buildOk, opts.testsOk);
  const tmpBodyFile = joinPath(tmpdir(), `delve-pr-body-${Date.now()}.txt`);
  writeTmp(tmpBodyFile, prBody, "utf-8");
  const prResult = shell(
    `gh pr create --title "${prTitle.replace(/"/g, "'")}" --body-file "${tmpBodyFile}" 2>&1`,
    cwd
  );
  try { unlinkSync(tmpBodyFile); } catch { /* ignore */ }

  const prUrl = prResult.ok ? prResult.stdout.trim() : "";
  return {
    ok: prResult.ok,
    prUrl,
    summary: prResult.ok ? `PR created: ${prUrl}` : `PR creation failed: ${prResult.stderr}`,
  };
}

export function getChangedFiles(cwd?: string): string[] {
  const result = shell("git diff --name-only main 2>/dev/null", cwd);
  return result.stdout
    .trim()
    .split("\n")
    .filter((f) => f.length > 0);
}

export function getDiff(cwd?: string): string {
  const result = shell("git diff main 2>/dev/null", cwd);
  // Truncate diff to keep token count manageable
  const full = result.stdout;
  if (full.length > 12000) {
    return full.slice(0, 12000) + "\n... [diff truncated]";
  }
  return full;
}

export function cleanupMergedBranches(): { deleted: string[]; summary: string } {
  // Fetch with prune to remove stale remote tracking refs
  shell("git fetch --prune 2>&1");

  // List remote minion/* branches that are merged into main
  const result = shell(
    "git branch -r --merged main 2>/dev/null | grep 'origin/minion' | sed 's|origin/||' | tr -d ' '"
  );

  if (!result.ok || !result.stdout.trim()) {
    return { deleted: [], summary: "No merged minion branches to clean up" };
  }

  const branches = result.stdout.trim().split("\n").filter((b) => b.length > 0);
  const deleted: string[] = [];

  for (const branch of branches) {
    // Delete remote branch
    const delRemote = shell(`git push origin --delete "${branch}" 2>&1`);
    // Delete local branch if it exists
    shell(`git branch -d "${branch}" 2>&1`);
    if (delRemote.ok) {
      deleted.push(branch);
    }
  }

  return {
    deleted,
    summary: deleted.length > 0
      ? `Cleaned up ${deleted.length} merged branch(es): ${deleted.join(", ")}`
      : "No merged minion branches to clean up",
  };
}

function buildPrBody(
  prompt: string,
  changedFiles: string[],
  buildOk: boolean,
  testsOk: boolean
): string {
  return `## Summary
${prompt}

## Changed Files
${changedFiles.map((f) => "- " + f).join("\n")}

## Status
- Build: ${buildOk ? "PASS" : "FAIL"}
- Tests: ${testsOk ? "PASS" : "FAIL"}

## Architecture
Implemented using meta-agentic pipeline:
- Opus orchestrator → Sonnet meta-agents → Haiku workers
- Pi subagent spawning (isolated context windows)

---
Implemented autonomously by Delve minion agent.`;
}
