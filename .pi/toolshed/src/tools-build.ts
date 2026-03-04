import { z } from "zod";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { shell, PROJECT_ROOT } from "./shell.js";

// ─── build_configure ────────────────────────────────────────────────────────

export const buildConfigureSchema = {
  buildType: z
    .enum(["Release", "Debug", "RelWithDebInfo"])
    .default("Release")
    .describe("CMake build type"),
};

export async function buildConfigure({ buildType }: { buildType: "Release" | "Debug" | "RelWithDebInfo" }) {
  const result = shell(
    `cmake -B build -DCMAKE_BUILD_TYPE=${buildType} 2>&1`
  );
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          success: result.ok,
          output: (result.stdout + result.stderr).slice(-3000),
        }),
      },
    ],
  };
}

// ─── build_compile ──────────────────────────────────────────────────────────

export const buildCompileSchema = {
  target: z
    .enum(["topogen", "delve_tests", "shaders"])
    .default("topogen")
    .describe("Build target"),
  jobs: z
    .number()
    .default(0)
    .describe("Parallel jobs (0 = auto-detect nproc)"),
};

export async function buildCompile({ target, jobs }: { target: "topogen" | "delve_tests" | "shaders"; jobs: number }) {
  const j = jobs > 0 ? jobs : "$(nproc)";
  const result = shell(
    `cmake --build build --target ${target} -j${j} 2>&1`
  );
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          success: result.ok,
          output: (result.stdout + result.stderr).slice(-4000),
        }),
      },
    ],
  };
}

// ─── build_clean ────────────────────────────────────────────────────────────

export const buildCleanSchema = {};

export async function buildClean(_args: Record<string, never>) {
  const result = shell("cmake --build build --target clean 2>&1");
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({ success: result.ok }),
      },
    ],
  };
}

// ─── test_run ───────────────────────────────────────────────────────────────

export const testRunSchema = {};

export async function testRun(_args: Record<string, never>) {
  // Build tests first
  const build = shell(
    "cmake --build build --target delve_tests -j$(nproc) 2>&1"
  );
  if (!build.ok) {
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({
            success: false,
            phase: "build",
            output: (build.stdout + build.stderr).slice(-3000),
          }),
        },
      ],
    };
  }

  // Run tests
  const run = shell("./build/delve_tests 2>&1");
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          success: run.ok,
          phase: "run",
          results: run.stdout.slice(-4000),
          errors: run.stderr.slice(-1000),
        }),
      },
    ],
  };
}

// ─── test_list ──────────────────────────────────────────────────────────────

export const testListSchema = {};

export async function testList(_args: Record<string, never>) {
  const testDir = join(PROJECT_ROOT, "src/test/tests");
  if (!existsSync(testDir)) {
    return {
      content: [{ type: "text" as const, text: "No test directory found" }],
    };
  }

  const result = shell(
    `grep -rn 'DELVE_TEST(' src/test/tests/ 2>/dev/null | sed 's/.*DELVE_TEST(\\(.*\\)).*/\\1/'`
  );
  const tests = result.stdout.trim().split("\n").filter(Boolean);
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({ count: tests.length, tests }),
      },
    ],
  };
}
