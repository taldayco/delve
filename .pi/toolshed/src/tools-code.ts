import { z } from "zod";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { shell, listFilesRecursive, PROJECT_ROOT } from "./shell.js";

// ─── code_list_subsystem ────────────────────────────────────────────────────

export const codeListSubsystemSchema = {
  subsystem: z
    .enum(["engine", "terrain", "shaders", "game", "test"])
    .describe("Subsystem to list"),
};

export async function codeListSubsystem({ subsystem }: { subsystem: "engine" | "terrain" | "shaders" | "game" | "test" }) {
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

// ─── code_find_definition ───────────────────────────────────────────────────

export const codeFindDefinitionSchema = {
  symbol: z.string().describe("Name of the symbol to find (e.g., 'MapData', 'hex_to_pixel', 'Joint')"),
};

export async function codeFindDefinition({ symbol }: { symbol: string }) {
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

// ─── code_find_usages ───────────────────────────────────────────────────────

export const codeFindUsagesSchema = {
  symbol: z.string().describe("Symbol to search for"),
  fileType: z
    .enum(["all", "headers", "source", "shaders"])
    .default("all")
    .describe("File types to search"),
};

export async function codeFindUsages({ symbol, fileType }: { symbol: string; fileType: "all" | "headers" | "source" | "shaders" }) {
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
