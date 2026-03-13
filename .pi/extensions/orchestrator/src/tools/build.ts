// ─── Build Tools ─────────────────────────────────────────────────────────────

import { silentShell, writeState } from "../agents.js";

export function runBuild(cwd: string): { ok: boolean; summary: string; full: string } {
  const result = silentShell(
    "cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 && cmake --build build -j$(nproc) 2>&1",
    50,
    cwd
  );
  writeState("build_log.txt", result.full);
  return result;
}

export function runTests(cwd: string): {
  buildOk: boolean;
  testsOk: boolean;
  summary: string;
  full: string;
} {
  // A1 fix: ensure cmake is configured before building tests
  const { existsSync } = require("node:fs");
  const { join } = require("node:path");
  const baseCwd = cwd;
  if (!existsSync(join(baseCwd, "build/compile_commands.json"))) {
    silentShell("cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1", 30, cwd);
  }

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

export function runMetrics(cwd: string): {
  ok: boolean;
  outputDir: string;
  domains: string[];
  summary: string;
} {
  const { mkdirSync, readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const baseCwd = cwd;
  const outputDir = join(baseCwd, ".pi/state/metrics");

  // Build metrics binary first
  const build = silentShell(
    "cmake --build build --target delve_metrics -j$(nproc) 2>&1",
    30,
    baseCwd
  );
  if (!build.ok) {
    return { ok: false, outputDir, domains: [], summary: `Metrics build failed: ${build.summary}` };
  }

  // Run metrics with per-domain output
  const configPath = join(baseCwd, "config.json");
  const configArg = existsSync(configPath) ? `--config ${configPath}` : "";
  const run = silentShell(
    `./build/delve_metrics ${configArg} --output-dir "${outputDir}" 2>&1`,
    30,
    baseCwd
  );

  if (!run.ok) {
    return { ok: false, outputDir, domains: [], summary: `Metrics run failed: ${run.summary}` };
  }

  // Read manifest to discover domains
  const manifestPath = join(outputDir, "manifest.json");
  let domains: string[] = [];
  try {
    const manifest = JSON.parse(readFileSync(manifestPath, "utf-8"));
    domains = manifest.domains || [];
  } catch { /* ignore */ }

  return { ok: true, outputDir, domains, summary: `Metrics: ${domains.length} domains emitted` };
}

export function runShaderValidation(cwd: string): { ok: boolean; summary: string } {
  return silentShell(
    "find src/shaders -name '*.glsl' ! -name '*.inc.glsl' " +
      "-exec glslc --target-env=vulkan1.2 -o /dev/null {} \\; 2>&1",
    30,
    cwd
  );
}
