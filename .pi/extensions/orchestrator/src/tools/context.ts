// ─── Codebase Context ────────────────────────────────────────────────────────

import { shell } from "./shell.js";
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

export function getCodebaseContext(task: string, baseCwd?: string): string {
  // Gather relevant context based on task keywords
  const sections: string[] = [];

  // Always include project overview
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();

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
      const ls = shell(`find ${dir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) | sort 2>/dev/null`, cwd);
      if (ls.ok && ls.stdout.trim()) {
        sections.push(`### Files in ${dir}\n${ls.stdout.trim()}`);
      }
    }
  }

  // If no specific subsystem matched, show the main terrain and game dirs
  if (listedDirs.size === 0) {
    const ls = shell("find src/game -type f -name '*.h' | sort 2>/dev/null", cwd);
    if (ls.ok) {
      sections.push(`### Header files in src/game\n${ls.stdout.trim()}`);
    }
  }

  return sections.join("\n\n");
}

/**
 * Get codebase context scoped to a single canonical subsystem.
 * Returns config.h + file listings only for that subsystem's directories.
 */
export function getSubsystemCodebaseContext(subsystem: string, baseCwd?: string): string {
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();
  const sections: string[] = [];

  // Always include project overview
  const configPath = join(cwd, "src/game/config.h");
  if (existsSync(configPath)) {
    sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
  }

  // Find all fine-grained subsystem names that map to this canonical subsystem
  const fineGrained = Object.entries(SUBSYSTEM_CANONICAL)
    .filter(([_, canonical]) => canonical === subsystem)
    .map(([fine]) => fine);

  // Collect directories for this subsystem
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

  // Check for directory names not covered by any keyword
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
