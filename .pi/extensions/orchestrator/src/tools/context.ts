// ─── Codebase Context ────────────────────────────────────────────────────────

import { shell } from "./shell.js";
import {
  isVikingAvailable,
  vikingSearch,
  vikingOverview,
  SUBSYSTEM_VIKING_URI,
} from "./viking.js";
import type { MapCoverageReport } from "./types.js";

// Two-tier synonym map: natural language terms → fine-grained subsystem names
export const KEYWORD_SYNONYMS: Record<string, string[]> = {
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
export const SUBSYSTEM_DIRS: Record<string, string[]> = {
  actor: ["src/game/render"], animation: ["src/game/render"],
  render: ["src/game/render"], terrain: ["src/game/terrain"],
  engine: ["src/engine"], camera: ["src/engine/camera"],
  input: ["src/engine/input"], gpu: ["src/engine/gpu"],
  // A6: added missing engine subdirectory mappings
  engine_core: ["src/engine/core"], engine_render: ["src/engine/render"],
  engine_ui: ["src/engine/ui"], engine_ipc: ["src/engine/ipc"],
  shader: ["src/shaders"], test: ["src/test"],
};

// Fine-grained → 4 canonical meta-agent subsystems
export const SUBSYSTEM_CANONICAL: Record<string, string> = {
  actor: "actor", animation: "actor", render: "actor",
  terrain: "terrain", shader: "shader",
  engine: "engine", camera: "engine", input: "engine",
  gpu: "engine", test: "engine",
  // A6: added missing engine subdirectory canonical mappings
  engine_core: "engine", engine_render: "engine",
  engine_ui: "engine", engine_ipc: "engine",
};

// ─── Keyword Fallback Implementations ───────────────────────────────────────

function resolveSubsystemsKeyword(task: string): string[] {
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

function getCodebaseContextKeyword(task: string, baseCwd?: string): string {
  const sections: string[] = [];
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();

  const configPath = join(cwd, "src/game/config.h");
  if (existsSync(configPath)) {
    sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
  }

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

  const listedDirs = new Set<string>();
  for (const subsystem of matchedSubsystems) {
    const dirs = SUBSYSTEM_DIRS[subsystem] || [];
    for (const dir of dirs) {
      if (listedDirs.has(dir)) continue;
      listedDirs.add(dir);
      const ls = shell(`find ${dir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) | sort 2>/dev/null`, cwd);
      if (ls.ok && ls.stdout.trim()) {
        sections.push(`### Files in ${dir}\n${ls.stdout.trim()}`);
      }
    }
  }

  if (listedDirs.size === 0) {
    const ls = shell("find src/game -type f -name '*.h' | sort 2>/dev/null", cwd);
    if (ls.ok) {
      sections.push(`### Header files in src/game\n${ls.stdout.trim()}`);
    }
  }

  return sections.join("\n\n");
}

function getSubsystemCodebaseContextKeyword(subsystem: string, baseCwd?: string): string {
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();
  const sections: string[] = [];

  const configPath = join(cwd, "src/game/config.h");
  if (existsSync(configPath)) {
    sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
  }

  const fineGrained = Object.entries(SUBSYSTEM_CANONICAL)
    .filter(([_, canonical]) => canonical === subsystem)
    .map(([fine]) => fine);

  const listedDirs = new Set<string>();
  for (const fine of fineGrained) {
    const dirs = SUBSYSTEM_DIRS[fine] || [];
    for (const dir of dirs) {
      if (listedDirs.has(dir)) continue;
      listedDirs.add(dir);
      const ls = shell(`find ${dir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) | sort 2>/dev/null`, cwd);
      if (ls.ok && ls.stdout.trim()) {
        sections.push(`### Files in ${dir}\n${ls.stdout.trim()}`);
      }
    }
  }

  return sections.join("\n\n");
}

// ─── Public API (Viking with Keyword Fallback) ──────────────────────────────

/**
 * Resolve a task description to canonical subsystem names.
 * Viking path: L0 search scoped to viking://resources/delve, extract subsystem from URIs.
 * Fallback: keyword regex matching.
 */
export async function resolveSubsystems(task: string): Promise<string[]> {
  if (await isVikingAvailable()) {
    const results = await vikingSearch(task, "L0", "viking://resources/delve", 5);
    if (results.length > 0) {
      const matched = new Set<string>();
      for (const r of results) {
        // Extract subsystem from URI: viking://resources/delve/<subsystem>/...
        const m = r.uri.match(/^viking:\/\/resources\/delve\/([^/]+)/);
        if (m) {
          const sub = m[1];
          // Map known URI segments to canonical names
          if (sub in SUBSYSTEM_VIKING_URI || ["terrain", "actor", "shader", "engine", "test", "game"].includes(sub)) {
            const canonical = sub === "test" ? "engine" : sub === "game" ? "engine" : sub;
            matched.add(canonical);
          }
        }
      }
      if (matched.size > 0) return Array.from(matched);
    }
  }

  return resolveSubsystemsKeyword(task);
}

/**
 * Get codebase context for a task.
 * Viking path: config.h + L1 search results (~2K token overviews per match).
 * Fallback: config.h + directory file listings.
 */
export async function getCodebaseContext(task: string, baseCwd?: string): Promise<string> {
  if (await isVikingAvailable()) {
    const sections: string[] = [];
    const { readFileSync, existsSync } = require("node:fs");
    const { join } = require("node:path");
    const cwd = baseCwd || process.cwd();

    // Always include config.h
    const configPath = join(cwd, "src/game/config.h");
    if (existsSync(configPath)) {
      sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
    }

    // L1 search for relevant overviews
    const results = await vikingSearch(task, "L1", "viking://resources/delve", 8);
    if (results.length > 0) {
      for (const r of results) {
        sections.push(`### ${r.uri}\n${r.snippet}`);
      }
      return sections.join("\n\n");
    }
    // If Viking search returned no results, fall through
  }

  return getCodebaseContextKeyword(task, baseCwd);
}

/**
 * Get codebase context scoped to a single canonical subsystem.
 * Viking path: config.h + L1 overview of the subsystem's viking:// directory.
 * Fallback: config.h + file listings.
 */
export async function getSubsystemCodebaseContext(subsystem: string, baseCwd?: string): Promise<string> {
  if (await isVikingAvailable()) {
    const vikingUri = SUBSYSTEM_VIKING_URI[subsystem];
    if (vikingUri) {
      const overview = await vikingOverview(vikingUri);
      if (overview) {
        const sections: string[] = [];
        const { readFileSync, existsSync } = require("node:fs");
        const { join } = require("node:path");
        const cwd = baseCwd || process.cwd();

        const configPath = join(cwd, "src/game/config.h");
        if (existsSync(configPath)) {
          sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
        }

        sections.push(`### ${vikingUri} (L1 Overview)\n${overview}`);
        return sections.join("\n\n");
      }
    }
  }

  return getSubsystemCodebaseContextKeyword(subsystem, baseCwd);
}

export function detectMapCoverage(): MapCoverageReport {
  const { readdirSync, statSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = process.cwd();
  const knownDirs = new Set(Object.values(SUBSYSTEM_DIRS).flat());
  const unmappedDirs: string[] = [];

  for (const parent of ["src/game", "src/engine"]) {
    const parentPath = join(cwd, parent);
    try {
      const entries = readdirSync(parentPath);
      for (const entry of entries) {
        try {
          if (statSync(join(parentPath, entry)).isDirectory()) {
            const relPath = `${parent}/${entry}`;
            if (!knownDirs.has(relPath)) {
              unmappedDirs.push(relPath);
            }
          }
        } catch { /* ignore */ }
      }
    } catch { /* ignore */ }
  }

  const allKeywords = new Set(Object.keys(KEYWORD_SYNONYMS));
  const unmappedKeywords: string[] = [];
  for (const dir of unmappedDirs) {
    const dirName = dir.split("/").pop() || "";
    if (!allKeywords.has(dirName)) {
      unmappedKeywords.push(dirName);
    }
  }

  const suggestion = unmappedDirs.length > 0
    ? `Add mappings for: ${unmappedDirs.join(", ")}`
    : "All directories are mapped";

  return {
    unmappedDirs,
    unmappedKeywords,
    currentKeywordCount: allKeywords.size,
    suggestion,
  };
}
