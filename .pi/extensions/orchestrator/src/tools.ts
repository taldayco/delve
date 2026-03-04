import { silentShell, writeState } from "./agents.js";
import { execSync } from "node:child_process";

// ─── Configuration ─────────────────────────────────────────────────────────

const MAX_BUILD_FIX_ROUNDS = 3;
const MAX_TEST_FIX_ROUNDS = 3;

export { MAX_BUILD_FIX_ROUNDS, MAX_TEST_FIX_ROUNDS };

// ─── Shell Helper (full control variant) ───────────────────────────────────

function shell(cmd: string, cwd?: string): { ok: boolean; stdout: string; stderr: string } {
  try {
    const stdout = execSync(cmd, {
      cwd: cwd || process.cwd(),
      encoding: "utf-8",
      timeout: 300_000,
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

// ─── Utility ───────────────────────────────────────────────────────────────

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

// ─── Deterministic Tool: cleanup_merged_branches ─────────────────────────

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

// ─── Deterministic Tool: git_branch (worktree-based) ────────────────────────

const WORKTREE_DIR = ".pi/worktrees";

/**
 * Create a git worktree for the given branch.
 * Returns the worktree path on success.
 */
export function gitBranch(branchName: string): { ok: boolean; summary: string; worktreePath?: string } {
  // Ensure we're on main and up to date
  shell("git checkout main 2>&1 && git pull --ff-only 2>&1");

  // Create worktree directory
  const safeName = branchName.replace(/\//g, "-");
  const worktreePath = `${WORKTREE_DIR}/${safeName}`;

  // Clean up existing worktree at this path if it exists
  shell(`git worktree remove --force "${worktreePath}" 2>&1`);

  const result = shell(
    `git worktree add -b "${branchName}" "${worktreePath}" main 2>&1`
  );
  if (!result.ok && !result.stderr.includes("already exists")) {
    return { ok: false, summary: `Failed to create worktree: ${result.stderr}` };
  }
  return { ok: true, summary: `Worktree created: ${worktreePath} (branch: ${branchName})`, worktreePath };
}

/**
 * Clean up a git worktree after pipeline completion.
 */
export function cleanupWorktree(worktreePath: string): void {
  try {
    shell(`git worktree remove --force "${worktreePath}" 2>&1`);
  } catch { /* ignore */ }
}

// ─── Deterministic Tool: run_build ─────────────────────────────────────────

export function runBuild(cwd?: string): { ok: boolean; summary: string; full: string } {
  const result = silentShell(
    "cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 && cmake --build build -j$(nproc) 2>&1",
    50,
    cwd
  );
  writeState("build_log.txt", result.full);
  return result;
}

// ─── Deterministic Tool: run_tests ─────────────────────────────────────────

export function runTests(cwd?: string): {
  buildOk: boolean;
  testsOk: boolean;
  summary: string;
  full: string;
} {
  // Build tests first
  const build = silentShell(
    "cmake --build build --target delve_tests -j$(nproc) 2>&1",
    30,
    cwd
  );
  if (!build.ok) {
    writeState("test_results.json", JSON.stringify({ phase: "build", success: false }));
    return {
      buildOk: false,
      testsOk: false,
      summary: build.summary,
      full: build.full,
    };
  }

  // Run tests
  const run = silentShell("./build/delve_tests 2>&1", 40, cwd);
  writeState(
    "test_results.json",
    JSON.stringify({ phase: "run", success: run.ok, output: run.summary })
  );
  return {
    buildOk: true,
    testsOk: run.ok,
    summary: run.summary,
    full: run.full,
  };
}

// ─── Deterministic Tool: git_commit_and_pr ─────────────────────────────────

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

  // Commit
  const commitMsg = `feat: ${opts.prompt}\n\nImplemented by Delve minion agent (meta-agentic).\nBuild: ${opts.buildOk ? "PASS" : "FAIL"}\nTests: ${opts.testsOk ? "PASS" : "FAIL"}`;
  const commitResult = shell(
    `git commit -m "${commitMsg.replace(/"/g, '\\"')}" 2>&1`,
    cwd
  );
  if (!commitResult.ok) {
    return { ok: false, prUrl: "", summary: `git commit failed: ${commitResult.stderr}` };
  }

  // Push
  const pushResult = shell(`git push -u origin "${opts.branch}" 2>&1`, cwd);
  if (!pushResult.ok) {
    return { ok: false, prUrl: "", summary: `git push failed: ${pushResult.stderr}` };
  }

  // Create PR
  const prTitle = `feat: ${opts.prompt.slice(0, 60)}`;
  const prBody = buildPrBody(opts.prompt, changedFiles, opts.buildOk, opts.testsOk);
  const prResult = shell(
    `gh pr create --title "${prTitle.replace(/"/g, '\\"')}" --body "${prBody.replace(/"/g, '\\"').replace(/\n/g, "\\n")}" 2>&1`,
    cwd
  );

  const prUrl = prResult.ok ? prResult.stdout.trim() : "";
  return {
    ok: prResult.ok,
    prUrl,
    summary: prResult.ok ? `PR created: ${prUrl}` : `PR creation failed: ${prResult.stderr}`,
  };
}

// ─── Deterministic Tool: get_changed_files ─────────────────────────────────

export function getChangedFiles(cwd?: string): string[] {
  const result = shell("git diff --name-only main 2>/dev/null", cwd);
  return result.stdout
    .trim()
    .split("\n")
    .filter((f) => f.length > 0);
}

// ─── Deterministic Tool: get_diff ──────────────────────────────────────────

export function getDiff(cwd?: string): string {
  const result = shell("git diff main 2>/dev/null", cwd);
  // Truncate diff to keep token count manageable
  const full = result.stdout;
  if (full.length > 12000) {
    return full.slice(0, 12000) + "\n... [diff truncated]";
  }
  return full;
}

// ─── Deterministic Tool: get_codebase_context ──────────────────────────────

export function getCodebaseContext(task: string): string {
  // Gather relevant context based on task keywords
  const sections: string[] = [];

  // Always include project overview
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = process.cwd();

  // Read config.h for constants
  const configPath = join(cwd, "src/game/config.h");
  if (existsSync(configPath)) {
    sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
  }

  // Resolve task keywords → canonical subsystem names via synonym map
  // Use word-boundary regex to avoid substring matches
  const taskLower = (task || "").toLowerCase();
  const matchedSubsystems = new Set<string>();

  for (const [keyword, subsystems] of Object.entries(KEYWORD_SYNONYMS)) {
    const re = new RegExp(`\\b${keyword}\\b`, "i");
    if (re.test(taskLower)) {
      for (const sub of subsystems) {
        matchedSubsystems.add(sub);
      }
    }
  }

  // Resolve subsystems → directories and list files
  const listedDirs = new Set<string>();
  for (const subsystem of matchedSubsystems) {
    const dirs = SUBSYSTEM_DIRS[subsystem] || [];
    for (const dir of dirs) {
      if (listedDirs.has(dir)) continue;
      listedDirs.add(dir);
      const ls = shell(`find ${dir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) | sort 2>/dev/null`);
      if (ls.ok && ls.stdout.trim()) {
        sections.push(`### Files in ${dir}\n${ls.stdout.trim()}`);
      }
    }
  }

  // If no specific subsystem matched, show the main terrain and game dirs
  if (listedDirs.size === 0) {
    const ls = shell("find src/game -type f -name '*.h' | sort 2>/dev/null");
    if (ls.ok) {
      sections.push(`### Header files in src/game\n${ls.stdout.trim()}`);
    }
  }

  return sections.join("\n\n");
}

// ─── Keyword → Subsystem Mapping ────────────────────────────────────────────

// Two-tier synonym map: natural language terms → fine-grained subsystem names
const KEYWORD_SYNONYMS: Record<string, string[]> = {
  // Actor / character / humanoid terms
  player: ["actor"], character: ["actor"], humanoid: ["actor"],
  bipedal: ["actor"], avatar: ["actor"], sprite: ["actor"],
  npc: ["actor"], creature: ["actor"], figure: ["actor"],
  // Movement terms
  walk: ["actor", "animation"], run: ["actor", "animation"],
  move: ["actor", "animation"], locomot: ["actor", "animation"],
  gait: ["actor", "animation"], stride: ["actor", "animation"],
  step: ["actor", "animation"],
  // Body part terms
  limb: ["actor"], joint: ["actor"], arm: ["actor"], leg: ["actor"],
  torso: ["actor"], body: ["actor"], head: ["actor"], spine: ["actor"],
  shoulder: ["actor"], hip: ["actor"], knee: ["actor"], elbow: ["actor"],
  // Proportion / sizing
  proportio: ["actor"], scale: ["actor"], ratio: ["actor"], height: ["actor"],
  // Animation terms
  animation: ["animation"], animate: ["animation"], skeleton: ["animation"],
  bone: ["animation"], keyframe: ["animation"], rig: ["animation"],
  pose: ["animation"], ik: ["animation"],
  // Rendering terms
  actor: ["render"], render: ["render"], draw: ["render"],
  // Terrain terms
  terrain: ["terrain"], noise: ["terrain"], palette: ["terrain"],
  hex: ["terrain"], contour: ["terrain"], lava: ["terrain"],
  mesh: ["terrain"], elevation: ["terrain"], plateau: ["terrain"],
  // Engine terms
  engine: ["engine"], camera: ["camera"], input: ["input"],
  gpu: ["gpu"], shader: ["shader"], test: ["test"],
};

// Fine-grained subsystem → directories to scan
const SUBSYSTEM_DIRS: Record<string, string[]> = {
  actor: ["src/game/render"], animation: ["src/game/render"],
  render: ["src/game/render"], terrain: ["src/game/terrain"],
  engine: ["src/engine"], camera: ["src/engine/camera"],
  input: ["src/engine/input"], gpu: ["src/engine/gpu"],
  shader: ["src/shaders"], test: ["src/test"],
};

// Fine-grained → 4 canonical meta-agent subsystems
const SUBSYSTEM_CANONICAL: Record<string, string> = {
  actor: "actor", animation: "actor", render: "actor",
  terrain: "terrain", shader: "shader",
  engine: "engine", camera: "engine", input: "engine",
  gpu: "engine", test: "engine",
};

/**
 * Resolve a task description to canonical subsystem names.
 * Uses word-boundary matching to avoid substring false positives.
 * Returns deduplicated array of: "terrain", "actor", "shader", "engine".
 */
export function resolveSubsystems(task: string): string[] {
  const taskLower = (task || "").toLowerCase();
  const matched = new Set<string>();

  for (const [keyword, subsystems] of Object.entries(KEYWORD_SYNONYMS)) {
    const re = new RegExp(`\\b${keyword}\\b`, "i");
    if (re.test(taskLower)) {
      for (const sub of subsystems) {
        const canonical = SUBSYSTEM_CANONICAL[sub];
        if (canonical) matched.add(canonical);
      }
    }
  }

  return Array.from(matched);
}

// ─── Deterministic Tool: run_shader_validation ─────────────────────────────

export function runShaderValidation(): { ok: boolean; summary: string } {
  return silentShell(
    "find src/shaders -name '*.glsl' ! -name '*.inc.glsl' " +
      "-exec glslc --target-env=vulkan1.2 -o /dev/null {} \\; 2>&1",
    30
  );
}

// ─── File Block Parser (shared) ─────────────────────────────────────────────

/**
 * Parse ### FILE: blocks from agent output and write files directly.
 * Uses a line-by-line parser instead of regex to correctly handle
 * inner triple-backtick code blocks within file content.
 * Returns the number of files written.
 */
export function applyFileBlocks(text: string, cwd?: string): number {
  const { writeFileSync, mkdirSync } = require("node:fs");
  const { dirname, join } = require("node:path");
  const baseCwd = cwd || process.cwd();

  const lines = text.split("\n");
  let count = 0;
  let currentFile: string | null = null;
  let contentLines: string[] = [];
  let inFence = false;
  let fenceLength = 0; // number of backticks that opened the fence

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    // Detect ### FILE: header
    const fileMatch = line.match(/^###\s*FILE:\s*(\S+)/);
    if (fileMatch && !inFence) {
      // Flush previous file if any
      if (currentFile && contentLines.length > 0) {
        writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
        count++;
      }
      currentFile = fileMatch[1];
      contentLines = [];
      inFence = false;
      fenceLength = 0;
      continue;
    }

    // Skip #### ACTION: lines when not inside a fence
    if (!inFence && currentFile && /^####\s*ACTION:/i.test(line)) {
      continue;
    }

    // Detect opening fence (only when we have a currentFile and not already in a fence)
    if (!inFence && currentFile) {
      const openMatch = line.match(/^(`{3,})\w*\s*$/);
      if (openMatch) {
        inFence = true;
        fenceLength = openMatch[1].length;
        contentLines = [];
        continue;
      }
    }

    // Detect closing fence: line is solely backticks of equal or greater length
    if (inFence && currentFile) {
      const closeMatch = line.match(/^(`{3,})\s*$/);
      if (closeMatch && closeMatch[1].length >= fenceLength) {
        // Write the file
        writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
        count++;
        currentFile = null;
        contentLines = [];
        inFence = false;
        fenceLength = 0;
        continue;
      }
    }

    // Accumulate content lines inside fence
    if (inFence && currentFile) {
      contentLines.push(line);
    }
  }

  // Flush any unclosed file (shouldn't happen with well-formed output, but be safe)
  if (currentFile && contentLines.length > 0 && inFence) {
    writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
    count++;
  }

  return count;
}

function writeFileBlock(
  filePath: string,
  content: string,
  cwd: string,
  writeFileSync: Function,
  mkdirSync: Function,
  dirname: Function,
  join: Function,
): void {
  const fullPath = filePath.startsWith("/") ? filePath : join(cwd, filePath);
  try {
    mkdirSync(dirname(fullPath), { recursive: true });
    writeFileSync(fullPath, content, "utf-8");
  } catch (e: any) {
    console.error(`Failed to write ${fullPath}: ${e.message}`);
  }
}

// ─── PR Body Builder ───────────────────────────────────────────────────────

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
