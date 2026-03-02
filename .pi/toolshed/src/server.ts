import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { execSync } from "node:child_process";
import { readFileSync, existsSync, readdirSync, statSync } from "node:fs";
import { join, resolve } from "node:path";

// ─── Project Root Detection ─────────────────────────────────────────────────

const PROJECT_ROOT = resolve(
  process.env.DELVE_PROJECT_ROOT || findProjectRoot()
);

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

// ─── Shell Helper ───────────────────────────────────────────────────────────

function shell(
  cmd: string,
  timeout = 120_000
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

// ─── Server Setup ───────────────────────────────────────────────────────────

const server = new McpServer({
  name: "delve-toolshed",
  version: "1.0.0",
});

// ═══════════════════════════════════════════════════════════════════════════
// BUILD TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "build_configure",
  "Run cmake configure step. Call before build_compile if CMakeLists.txt changed.",
  {
    buildType: z
      .enum(["Release", "Debug", "RelWithDebInfo"])
      .default("Release")
      .describe("CMake build type"),
  },
  async ({ buildType }) => {
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
);

server.tool(
  "build_compile",
  "Compile the project with cmake. Returns compiler output including any errors or warnings.",
  {
    target: z
      .enum(["topogen", "delve_tests", "shaders"])
      .default("topogen")
      .describe("Build target"),
    jobs: z
      .number()
      .default(0)
      .describe("Parallel jobs (0 = auto-detect nproc)"),
  },
  async ({ target, jobs }) => {
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
);

server.tool(
  "build_clean",
  "Clean the build directory. Use when build state is corrupted.",
  {},
  async () => {
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
);

// ═══════════════════════════════════════════════════════════════════════════
// TEST TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "test_run",
  "Build and run the delve_tests binary. Returns JSON test results with pass/fail for each test.",
  {},
  async () => {
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
);

server.tool(
  "test_list",
  "List all registered test cases by scanning test source files.",
  {},
  async () => {
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
);

// ═══════════════════════════════════════════════════════════════════════════
// TERRAIN TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "terrain_list_palettes",
  "List all available terrain palettes defined in palettes.h.",
  {},
  async () => {
    const palettesFile = join(
      PROJECT_ROOT,
      "src/game/terrain/palettes.h"
    );
    if (!existsSync(palettesFile)) {
      return {
        content: [{ type: "text" as const, text: "palettes.h not found" }],
      };
    }
    const content = readFileSync(palettesFile, "utf-8");

    // Extract palette names
    const names: string[] = [];
    const regex = /\{"([^"]+)"/g;
    let match;
    while ((match = regex.exec(content)) !== null) {
      names.push(match[1]);
    }

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ count: names.length, palettes: names }),
        },
      ],
    };
  }
);

server.tool(
  "terrain_get_config",
  "Read the current terrain configuration from config.json (noise params, composition, display settings).",
  {},
  async () => {
    const configPath = join(PROJECT_ROOT, "config.json");
    if (!existsSync(configPath)) {
      return {
        content: [{ type: "text" as const, text: "config.json not found" }],
      };
    }
    const config = readFileSync(configPath, "utf-8");
    return {
      content: [{ type: "text" as const, text: config }],
    };
  }
);

server.tool(
  "terrain_set_config",
  "Update terrain configuration in config.json. Provide the full JSON object.",
  {
    config: z
      .string()
      .describe("Full config.json content as a JSON string"),
  },
  async ({ config }) => {
    try {
      // Validate it's valid JSON
      JSON.parse(config);
      const { writeFileSync } = await import("node:fs");
      writeFileSync(join(PROJECT_ROOT, "config.json"), config, "utf-8");
      return {
        content: [
          {
            type: "text" as const,
            text: JSON.stringify({ success: true }),
          },
        ],
      };
    } catch (e: any) {
      return {
        content: [
          {
            type: "text" as const,
            text: JSON.stringify({
              success: false,
              error: e.message,
            }),
          },
        ],
      };
    }
  }
);

server.tool(
  "terrain_pipeline_info",
  "Get information about a specific terrain pipeline stage. Useful for understanding data flow.",
  {
    stage: z
      .enum([
        "noise",
        "composition",
        "contour",
        "generation",
        "mesh",
        "rendering",
        "all",
      ])
      .describe("Pipeline stage to get info about"),
  },
  async ({ stage }) => {
    const stages: Record<
      string,
      { files: string[]; input: string; output: string; description: string }
    > = {
      noise: {
        files: ["noise.cpp", "noise_layers.cpp", "noise_layers.h"],
        input: "ElevationParams, WorleyParams, RiverParams",
        output:
          "MapData.elevation, MapData.worley, MapData.worley_edge, MapData.worley_cell_value, MapData.river_mask",
        description:
          "Generates raw noise layers using FastNoiseLite (Perlin/Simplex/Worley)",
      },
      composition: {
        files: ["noise_composer.cpp", "noise_composer.h"],
        input: "MapData (raw noise layers)",
        output:
          "MapData.final_elevation, MapData.liquid_mask, MapData.basalt_height",
        description:
          "Blends elevation + worley layers, applies river/liquid masks",
      },
      contour: {
        files: ["contour.cpp", "contour.h"],
        input: "MapData.final_elevation",
        output:
          "ContourData (heightmap, band_map, contour_lines), Plateaus, terrain_map updates",
        description:
          "Extracts contour lines and detects plateaus via flood-fill",
      },
      generation: {
        files: ["terrain_generator.cpp", "terrain_generator.h", "lava.cpp", "lava.h"],
        input: "Heightmap, band_map, Plateaus",
        output:
          "TerrainData (columns, lava_bodies, void_bodies, terrain_map)",
        description:
          "Creates HexColumns per plateau, generates lava/void regions",
      },
      mesh: {
        files: ["terrain_mesh.cpp", "terrain_mesh.h"],
        input: "TerrainData, MapData, ContourData",
        output:
          "TerrainMesh (basalt_layers with vertices/indices, lava_vertices, contour_vertices)",
        description: "Converts hex geometry to GPU-ready vertex/index buffers",
      },
      rendering: {
        files: [
          "terrain_renderer.cpp",
          "terrain_renderer.h",
          "src/shaders/terrain.vert.glsl",
          "src/shaders/terrain.frag.glsl",
        ],
        input: "TerrainMesh, SceneUniforms, GpuPointLights",
        output: "Rendered frame",
        description:
          "Uploads mesh to GPU, renders via SPIR-V shaders with clustered lighting",
      },
    };

    if (stage === "all") {
      return {
        content: [
          {
            type: "text" as const,
            text: JSON.stringify(stages, null, 2),
          },
        ],
      };
    }

    const info = stages[stage];
    if (!info) {
      return {
        content: [
          { type: "text" as const, text: `Unknown stage: ${stage}` },
        ],
      };
    }

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify(info, null, 2),
        },
      ],
    };
  }
);

// ═══════════════════════════════════════════════════════════════════════════
// CODE INTELLIGENCE TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "code_list_subsystem",
  "List all source files in a subsystem directory.",
  {
    subsystem: z
      .enum(["engine", "terrain", "shaders", "game", "test"])
      .describe("Subsystem to list"),
  },
  async ({ subsystem }) => {
    const paths: Record<string, string> = {
      engine: "src/engine",
      terrain: "src/game/terrain",
      shaders: "src/shaders",
      game: "src/game",
      test: "src/test",
    };

    const dir = join(PROJECT_ROOT, paths[subsystem]);
    if (!existsSync(dir)) {
      return {
        content: [
          { type: "text" as const, text: `Directory not found: ${dir}` },
        ],
      };
    }

    const files = listFilesRecursive(dir, PROJECT_ROOT);
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({
            subsystem,
            path: paths[subsystem],
            count: files.length,
            files,
          }),
        },
      ],
    };
  }
);

server.tool(
  "code_find_definition",
  "Find where a struct, class, function, or enum is defined in the codebase.",
  {
    symbol: z.string().describe("Name of the symbol to find (e.g., 'MapData', 'hex_to_pixel', 'Joint')"),
  },
  async ({ symbol }) => {
    // Search for struct/class/enum/function definitions
    const patterns = [
      `struct ${symbol}`,
      `class ${symbol}`,
      `enum.*${symbol}`,
      `^[a-zA-Z].*\\b${symbol}\\b.*\\(`, // function definition
    ];

    const results: string[] = [];
    for (const pattern of patterns) {
      const r = shell(
        `grep -rn '${pattern}' src/ --include='*.h' --include='*.cpp' 2>/dev/null | head -10`
      );
      if (r.stdout.trim()) {
        results.push(r.stdout.trim());
      }
    }

    return {
      content: [
        {
          type: "text" as const,
          text:
            results.length > 0
              ? results.join("\n")
              : `No definition found for: ${symbol}`,
        },
      ],
    };
  }
);

server.tool(
  "code_find_usages",
  "Find where a symbol is used across the codebase.",
  {
    symbol: z.string().describe("Symbol to search for"),
    fileType: z
      .enum(["all", "headers", "source", "shaders"])
      .default("all")
      .describe("File types to search"),
  },
  async ({ symbol, fileType }) => {
    const includes: Record<string, string> = {
      all: "--include='*.h' --include='*.cpp' --include='*.glsl'",
      headers: "--include='*.h'",
      source: "--include='*.cpp'",
      shaders: "--include='*.glsl'",
    };

    const result = shell(
      `grep -rn '\\b${symbol}\\b' src/ ${includes[fileType]} 2>/dev/null | head -30`
    );
    const lines = result.stdout.trim().split("\n").filter(Boolean);
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({
            symbol,
            matches: lines.length,
            results: lines,
          }),
        },
      ],
    };
  }
);

// ═══════════════════════════════════════════════════════════════════════════
// GIT TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "git_status",
  "Get current git status: branch, staged files, modified files, untracked files.",
  {},
  async () => {
    const branch = shell("git branch --show-current 2>/dev/null");
    const status = shell("git status --porcelain 2>/dev/null");
    const ahead = shell(
      "git rev-list --count HEAD..origin/main 2>/dev/null || echo 0"
    );

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
);

server.tool(
  "git_diff_from_main",
  "Show files changed compared to main branch.",
  {},
  async () => {
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
);

// ═══════════════════════════════════════════════════════════════════════════
// PROJECT INFO TOOLS
// ═══════════════════════════════════════════════════════════════════════════

server.tool(
  "project_overview",
  "Get a high-level overview of the Delve project structure, build targets, and key entry points.",
  {},
  async () => {
    const overview = {
      name: "Delve",
      description:
        "Procedural hex-grid terrain generator with skeletal animation, rendered via SDL3-GPU",
      language: "C++20",
      buildSystem: "CMake 3.20+",
      entryPoint: "src/game/main.cpp → TopoGame → Application::run()",
      buildTargets: {
        topogen: "Main executable",
        delve_tests: "Quantitative visual test binary",
        shaders: "SPIR-V shader compilation",
      },
      subsystems: {
        "src/engine/": "Reusable framework (app, gpu, input, camera, ui, render, ecs)",
        "src/game/terrain/":
          "Terrain pipeline (noise → composition → contour → generation → mesh → rendering)",
        "src/game/render/": "Actor rendering",
        "src/shaders/": "GLSL 4.5 shaders → SPIR-V",
        "src/test/": "Quantitative visual test infrastructure",
      },
      keyTypes: [
        "MapData (map_data.h) — central terrain data container",
        "HexColumn/HexCoord (hex.h) — hexagonal grid",
        "TerrainMesh (terrain_mesh.h) — GPU vertex/index buffers",
        "SkeletonPose/Joint (actor.h) — 17-joint skeletal system",
        "SceneUniforms (terrain_mesh.h) — GPU uniform block",
      ],
      configFile: "config.json",
    };

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify(overview, null, 2),
        },
      ],
    };
  }
);

// ═══════════════════════════════════════════════════════════════════════════
// META-TOOLS — Tools that help agents select other tools
// ═══════════════════════════════════════════════════════════════════════════

// ─── Tool Registry ─────────────────────────────────────────────────────────

interface ToolMeta {
  name: string;
  category: string;
  keywords: string[];
  prerequisites: string[];
  enables: string[];
  description: string;
  cost: "cheap" | "moderate" | "expensive";
}

const TOOL_REGISTRY: ToolMeta[] = [
  // Build tools
  {
    name: "build_configure",
    category: "build",
    keywords: ["cmake", "configure", "setup", "build type", "debug", "release"],
    prerequisites: [],
    enables: ["build_compile"],
    description: "Run cmake configure step. Required before first build or after CMakeLists.txt changes.",
    cost: "moderate",
  },
  {
    name: "build_compile",
    category: "build",
    keywords: ["build", "compile", "make", "link", "error", "warning"],
    prerequisites: ["build_configure (if CMakeLists.txt changed)"],
    enables: ["test_run"],
    description: "Compile the project. Returns compiler output including errors.",
    cost: "expensive",
  },
  {
    name: "build_clean",
    category: "build",
    keywords: ["clean", "reset", "corrupted", "rebuild"],
    prerequisites: [],
    enables: ["build_configure"],
    description: "Clean the build directory. Use when build state is corrupted.",
    cost: "cheap",
  },
  // Test tools
  {
    name: "test_run",
    category: "test",
    keywords: ["test", "verify", "check", "pass", "fail", "metrics", "quantitative"],
    prerequisites: ["build_compile"],
    enables: [],
    description: "Build and run delve_tests. Returns JSON with pass/fail per test.",
    cost: "expensive",
  },
  {
    name: "test_list",
    category: "test",
    keywords: ["test", "list", "discover", "available", "registered"],
    prerequisites: [],
    enables: [],
    description: "List all registered test cases by scanning DELVE_TEST macros.",
    cost: "cheap",
  },
  // Terrain tools
  {
    name: "terrain_list_palettes",
    category: "terrain",
    keywords: ["palette", "color", "theme", "biome", "tundra", "desert", "ocean"],
    prerequisites: [],
    enables: [],
    description: "List all terrain palettes defined in palettes.h.",
    cost: "cheap",
  },
  {
    name: "terrain_get_config",
    category: "terrain",
    keywords: ["config", "parameters", "noise", "elevation", "worley", "settings"],
    prerequisites: [],
    enables: ["terrain_set_config"],
    description: "Read terrain config from config.json (noise params, composition, display).",
    cost: "cheap",
  },
  {
    name: "terrain_set_config",
    category: "terrain",
    keywords: ["config", "update", "change", "parameters", "settings"],
    prerequisites: ["terrain_get_config"],
    enables: [],
    description: "Update terrain configuration in config.json.",
    cost: "cheap",
  },
  {
    name: "terrain_pipeline_info",
    category: "terrain",
    keywords: ["pipeline", "stage", "noise", "composition", "contour", "generation", "mesh", "rendering", "data flow"],
    prerequisites: [],
    enables: [],
    description: "Get info about a terrain pipeline stage: files, input/output, data flow.",
    cost: "cheap",
  },
  // Code intelligence tools
  {
    name: "code_list_subsystem",
    category: "code",
    keywords: ["files", "list", "directory", "subsystem", "engine", "terrain", "shader", "test"],
    prerequisites: [],
    enables: ["code_find_definition"],
    description: "List all source files in a subsystem directory.",
    cost: "cheap",
  },
  {
    name: "code_find_definition",
    category: "code",
    keywords: ["definition", "struct", "class", "function", "enum", "where", "find", "symbol"],
    prerequisites: [],
    enables: ["code_find_usages"],
    description: "Find where a symbol is defined (struct, class, function, enum).",
    cost: "cheap",
  },
  {
    name: "code_find_usages",
    category: "code",
    keywords: ["usage", "reference", "caller", "used", "import", "include"],
    prerequisites: [],
    enables: [],
    description: "Find where a symbol is used across the codebase.",
    cost: "cheap",
  },
  // Git tools
  {
    name: "git_status",
    category: "git",
    keywords: ["status", "branch", "staged", "modified", "untracked", "changes"],
    prerequisites: [],
    enables: [],
    description: "Get current git status: branch, staged/modified/untracked files.",
    cost: "cheap",
  },
  {
    name: "git_diff_from_main",
    category: "git",
    keywords: ["diff", "changes", "compare", "main", "branch"],
    prerequisites: [],
    enables: [],
    description: "Show files changed compared to main branch.",
    cost: "cheap",
  },
  // Project info
  {
    name: "project_overview",
    category: "project",
    keywords: ["overview", "structure", "architecture", "entry point", "build targets", "subsystems"],
    prerequisites: [],
    enables: ["code_list_subsystem", "terrain_pipeline_info"],
    description: "High-level project structure, build targets, and key entry points.",
    cost: "cheap",
  },
];

// ─── Meta-Tool: suggest_tools ──────────────────────────────────────────────

server.tool(
  "suggest_tools",
  "Given a task description, suggest which tools are most relevant. Meta-tool: tools that select tools.",
  {
    task: z.string().describe("Natural language task description"),
  },
  async ({ task }) => {
    const taskLower = task.toLowerCase();
    const scored = TOOL_REGISTRY.map((tool) => {
      let score = 0;
      // Keyword matching
      for (const kw of tool.keywords) {
        if (taskLower.includes(kw)) score += 2;
      }
      // Category matching
      if (taskLower.includes(tool.category)) score += 1;
      // Partial keyword matching (substring)
      for (const kw of tool.keywords) {
        for (const word of taskLower.split(/\s+/)) {
          if (kw.includes(word) && word.length > 2) score += 0.5;
        }
      }
      return { ...tool, score };
    })
      .filter((t) => t.score > 0)
      .sort((a, b) => b.score - a.score)
      .slice(0, 6);

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify(
            {
              task,
              suggestions: scored.map((t) => ({
                tool: t.name,
                category: t.category,
                relevance: t.score,
                description: t.description,
                cost: t.cost,
              })),
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

// ─── Meta-Tool: compose_toolchain ──────────────────────────────────────────

server.tool(
  "compose_toolchain",
  "Given a workflow, return an ordered sequence of tools to call with dependency info.",
  {
    workflow: z
      .enum([
        "implement_feature",
        "debug_test_failure",
        "add_palette",
        "add_biome",
        "debug_build",
        "explore_codebase",
        "review_changes",
      ])
      .describe("Workflow type"),
  },
  async ({ workflow }) => {
    const chains: Record<string, { step: number; tool: string; purpose: string }[]> = {
      implement_feature: [
        { step: 1, tool: "project_overview", purpose: "Understand project structure" },
        { step: 2, tool: "code_list_subsystem", purpose: "Find files in affected subsystem" },
        { step: 3, tool: "code_find_definition", purpose: "Locate key types/functions" },
        { step: 4, tool: "terrain_pipeline_info", purpose: "Understand data flow (if terrain)" },
        { step: 5, tool: "build_compile", purpose: "Verify compilation" },
        { step: 6, tool: "test_run", purpose: "Run tests" },
      ],
      debug_test_failure: [
        { step: 1, tool: "test_run", purpose: "Reproduce failure" },
        { step: 2, tool: "test_list", purpose: "Identify which test failed" },
        { step: 3, tool: "code_find_definition", purpose: "Find failing test code" },
        { step: 4, tool: "code_find_usages", purpose: "Trace data flow to failure" },
        { step: 5, tool: "build_compile", purpose: "Verify fix compiles" },
        { step: 6, tool: "test_run", purpose: "Verify fix passes" },
      ],
      add_palette: [
        { step: 1, tool: "terrain_list_palettes", purpose: "See existing palettes" },
        { step: 2, tool: "code_find_definition", purpose: "Find PALETTES array" },
        { step: 3, tool: "terrain_pipeline_info", purpose: "Understand mesh color flow" },
        { step: 4, tool: "build_compile", purpose: "Verify compilation" },
        { step: 5, tool: "test_run", purpose: "Run tests" },
      ],
      add_biome: [
        { step: 1, tool: "terrain_pipeline_info", purpose: "Understand full pipeline" },
        { step: 2, tool: "code_find_definition", purpose: "Find noise params and MapData" },
        { step: 3, tool: "terrain_get_config", purpose: "See current config" },
        { step: 4, tool: "code_list_subsystem", purpose: "List terrain files" },
        { step: 5, tool: "build_compile", purpose: "Verify compilation" },
        { step: 6, tool: "test_run", purpose: "Run tests" },
      ],
      debug_build: [
        { step: 1, tool: "build_compile", purpose: "Reproduce build error" },
        { step: 2, tool: "code_find_definition", purpose: "Find symbols in error" },
        { step: 3, tool: "code_find_usages", purpose: "Find callers/includers" },
        { step: 4, tool: "build_compile", purpose: "Verify fix" },
      ],
      explore_codebase: [
        { step: 1, tool: "project_overview", purpose: "High-level structure" },
        { step: 2, tool: "code_list_subsystem", purpose: "Browse specific subsystem" },
        { step: 3, tool: "terrain_pipeline_info", purpose: "Understand terrain data flow" },
        { step: 4, tool: "code_find_definition", purpose: "Find specific types" },
      ],
      review_changes: [
        { step: 1, tool: "git_status", purpose: "See current state" },
        { step: 2, tool: "git_diff_from_main", purpose: "See what changed" },
        { step: 3, tool: "build_compile", purpose: "Verify build" },
        { step: 4, tool: "test_run", purpose: "Run tests" },
      ],
    };

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify(
            { workflow, toolchain: chains[workflow] || [] },
            null,
            2
          ),
        },
      ],
    };
  }
);

// ─── Meta-Tool: tool_dependencies ──────────────────────────────────────────

server.tool(
  "tool_dependencies",
  "Get prerequisite and downstream tools for a specific tool.",
  {
    tool: z.string().describe("Tool name to get dependency info for"),
  },
  async ({ tool: toolName }) => {
    const meta = TOOL_REGISTRY.find((t) => t.name === toolName);
    if (!meta) {
      return {
        content: [
          {
            type: "text" as const,
            text: JSON.stringify({ error: `Unknown tool: ${toolName}` }),
          },
        ],
      };
    }

    // Find tools that list this tool as a prerequisite
    const downstreamTools = TOOL_REGISTRY.filter((t) =>
      t.prerequisites.some((p) => p.includes(toolName))
    ).map((t) => t.name);

    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify(
            {
              tool: meta.name,
              category: meta.category,
              description: meta.description,
              cost: meta.cost,
              requires: meta.prerequisites,
              enables: meta.enables,
              downstream: downstreamTools,
            },
            null,
            2
          ),
        },
      ],
    };
  }
);

// ─── Helpers ────────────────────────────────────────────────────────────────

function listFilesRecursive(dir: string, root: string): string[] {
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

// ─── Start Server ───────────────────────────────────────────────────────────

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("Delve tool shed MCP server running on stdio");
}

main().catch(console.error);
