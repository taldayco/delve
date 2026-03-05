import { z } from "zod";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { PROJECT_ROOT } from "./shell.js";

// ─── terrain_list_palettes ──────────────────────────────────────────────────

export const terrainListPalettesSchema = {};

export async function terrainListPalettes(_args: Record<string, never>) {
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

// ─── terrain_get_config ─────────────────────────────────────────────────────

export const terrainGetConfigSchema = {};

export async function terrainGetConfig(_args: Record<string, never>) {
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

// ─── terrain_set_config ─────────────────────────────────────────────────────

export const terrainSetConfigSchema = {
  config: z
    .string()
    .describe("Full config.json content as a JSON string"),
};

export async function terrainSetConfig({ config }: { config: string }) {
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

// ─── terrain_pipeline_info ──────────────────────────────────────────────────

export const terrainPipelineInfoSchema = {
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
};

export async function terrainPipelineInfo({ stage }: { stage: string }) {
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
