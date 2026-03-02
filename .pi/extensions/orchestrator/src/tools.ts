import { silentShell } from "./models.js";
import { writeState } from "./agents.js";
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

// ─── Deterministic Tool: git_branch ────────────────────────────────────────

export function gitBranch(branchName: string): { ok: boolean; summary: string } {
  const result = shell(
    `git checkout main 2>&1 && git pull --ff-only 2>&1; git checkout -b "${branchName}" 2>&1`
  );
  if (!result.ok && !result.stderr.includes("already exists")) {
    return { ok: false, summary: `Failed to create branch: ${result.stderr}` };
  }
  return { ok: true, summary: `Branch created: ${branchName}` };
}

// ─── Deterministic Tool: run_build ─────────────────────────────────────────

export function runBuild(): { ok: boolean; summary: string; full: string } {
  const result = silentShell(
    "cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 && cmake --build build -j$(nproc) 2>&1",
    50
  );
  writeState("build_log.txt", result.full);
  return result;
}

// ─── Deterministic Tool: run_tests ─────────────────────────────────────────

export function runTests(): {
  buildOk: boolean;
  testsOk: boolean;
  summary: string;
  full: string;
} {
  // Build tests first
  const build = silentShell(
    "cmake --build build --target delve_tests -j$(nproc) 2>&1",
    30
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
  const run = silentShell("./build/delve_tests 2>&1", 40);
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
}): { ok: boolean; prUrl: string; summary: string } {
  // Stage all changes
  const addResult = shell("git add -A 2>&1");
  if (!addResult.ok) {
    return { ok: false, prUrl: "", summary: `git add failed: ${addResult.stderr}` };
  }

  // Check for changes
  const statusResult = shell("git status --porcelain 2>&1");
  if (statusResult.stdout.trim().length === 0) {
    return { ok: false, prUrl: "", summary: "No changes to commit" };
  }

  // Collect changed files for PR body
  const diffResult = shell("git diff --cached --name-only 2>/dev/null");
  const changedFiles = diffResult.stdout
    .trim()
    .split("\n")
    .filter((f) => f.length > 0);

  // Commit
  const commitMsg = `feat: ${opts.prompt}\n\nImplemented by Delve minion agent (meta-agentic).\nBuild: ${opts.buildOk ? "PASS" : "FAIL"}\nTests: ${opts.testsOk ? "PASS" : "FAIL"}`;
  const commitResult = shell(
    `git commit -m "${commitMsg.replace(/"/g, '\\"')}" 2>&1`
  );
  if (!commitResult.ok) {
    return { ok: false, prUrl: "", summary: `git commit failed: ${commitResult.stderr}` };
  }

  // Push
  const pushResult = shell(`git push -u origin "${opts.branch}" 2>&1`);
  if (!pushResult.ok) {
    return { ok: false, prUrl: "", summary: `git push failed: ${pushResult.stderr}` };
  }

  // Create PR
  const prTitle = `feat: ${opts.prompt.slice(0, 60)}`;
  const prBody = buildPrBody(opts.prompt, changedFiles, opts.buildOk, opts.testsOk);
  const prResult = shell(
    `gh pr create --title "${prTitle.replace(/"/g, '\\"')}" --body "${prBody.replace(/"/g, '\\"').replace(/\n/g, "\\n")}" 2>&1`
  );

  const prUrl = prResult.ok ? prResult.stdout.trim() : "";
  return {
    ok: prResult.ok,
    prUrl,
    summary: prResult.ok ? `PR created: ${prUrl}` : `PR creation failed: ${prResult.stderr}`,
  };
}

// ─── Deterministic Tool: get_changed_files ─────────────────────────────────

export function getChangedFiles(): string[] {
  const result = shell("git diff --name-only main 2>/dev/null");
  return result.stdout
    .trim()
    .split("\n")
    .filter((f) => f.length > 0);
}

// ─── Deterministic Tool: get_diff ──────────────────────────────────────────

export function getDiff(): string {
  const result = shell("git diff main 2>/dev/null");
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

  // File listing for affected subsystems
  const taskLower = (task || "").toLowerCase();
  const subsystems: { keyword: string; dir: string }[] = [
    { keyword: "terrain", dir: "src/game/terrain" },
    { keyword: "noise", dir: "src/game/terrain" },
    { keyword: "palette", dir: "src/game/terrain" },
    { keyword: "hex", dir: "src/game/terrain" },
    { keyword: "contour", dir: "src/game/terrain" },
    { keyword: "lava", dir: "src/game/terrain" },
    { keyword: "mesh", dir: "src/game/terrain" },
    { keyword: "engine", dir: "src/engine" },
    { keyword: "camera", dir: "src/engine/camera" },
    { keyword: "input", dir: "src/engine/input" },
    { keyword: "gpu", dir: "src/engine/gpu" },
    { keyword: "shader", dir: "src/shaders" },
    { keyword: "actor", dir: "src/game/render" },
    { keyword: "animation", dir: "src/game/render" },
    { keyword: "skeleton", dir: "src/game/render" },
    { keyword: "test", dir: "src/test" },
  ];

  const listedDirs = new Set<string>();
  for (const { keyword, dir } of subsystems) {
    if (taskLower.includes(keyword) && !listedDirs.has(dir)) {
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
- Stateless sub-agent calls (no context accumulation)

---
Implemented autonomously by Delve minion agent.`;
}
