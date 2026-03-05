import { z } from "zod";
import { existsSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { shell, PROJECT_ROOT } from "./shell.js";

// ─── metrics_run ────────────────────────────────────────────────────────────

export const metricsRunSchema = {
  seed: z.number().default(1337).describe("Random seed for terrain generation"),
  size: z.number().default(128).describe("Map width/height"),
  output_dir: z
    .string()
    .default(".pi/state/metrics")
    .describe("Output directory for metric JSON files"),
};

export async function metricsRun({ seed, size, output_dir }: { seed: number; size: number; output_dir: string }) {
  // Build metrics binary
  const build = shell(
    "cmake --build build --target delve_metrics -j$(nproc) 2>&1"
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

  const outputPath = resolve(PROJECT_ROOT, output_dir);
  const configPath = join(PROJECT_ROOT, "config.json");
  const configArg = existsSync(configPath) ? `--config ${configPath}` : "";

  const run = shell(
    `./build/delve_metrics --seed ${seed} --size ${size} ${configArg} --output-dir "${outputPath}" 2>&1`
  );

  if (!run.ok) {
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({
            success: false,
            phase: "run",
            output: (run.stdout + run.stderr).slice(-3000),
          }),
        },
      ],
    };
  }

  // Read manifest
  const manifestPath = join(outputPath, "manifest.json");
  let manifest: any = {};
  try {
    manifest = JSON.parse(readFileSync(manifestPath, "utf-8"));
  } catch { /* ignore */ }

  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          success: true,
          output_dir: outputPath,
          ...manifest,
        }),
      },
    ],
  };
}

// ─── metrics_read_domain ────────────────────────────────────────────────────

export const metricsReadDomainSchema = {
  domain: z
    .enum(["terrain", "mesh", "performance", "manifest"])
    .describe("Domain to read"),
  metrics_dir: z
    .string()
    .default(".pi/state/metrics")
    .describe("Metrics output directory"),
};

export async function metricsReadDomain({ domain, metrics_dir }: { domain: string; metrics_dir: string }) {
  const filePath = join(
    resolve(PROJECT_ROOT, metrics_dir),
    `${domain}.json`
  );
  if (!existsSync(filePath)) {
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ success: false, error: `File not found: ${filePath}` }),
        },
      ],
    };
  }
  const content = readFileSync(filePath, "utf-8");
  return {
    content: [{ type: "text" as const, text: content }],
  };
}

// ─── project_overview ───────────────────────────────────────────────────────

export const projectOverviewSchema = {};

export async function projectOverview(_args: Record<string, never>) {
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
