// ─── Codebase Context ────────────────────────────────────────────────────────

import {
  isVikingAvailable,
  requireViking,
  vikingSearch,
  vikingOverview,
  vikingRead,
  SUBSYSTEM_VIKING_URI,
} from "./viking.js";

// Fine-grained subsystem → directories to scan (used by readSubsystemHeaders)
export const SUBSYSTEM_DIRS: Record<string, string[]> = {
  actor: ["src/game/render"], animation: ["src/game/render"],
  render: ["src/game/render"], terrain: ["src/game/terrain"],
  engine: ["src/engine"], camera: ["src/engine/camera"],
  input: ["src/engine/input"], gpu: ["src/engine/gpu"],
  engine_core: ["src/engine/core"], engine_render: ["src/engine/render"],
  engine_ui: ["src/engine/ui"], engine_ipc: ["src/engine/ipc"],
  shader: ["src/shaders"], test: ["src/test"],
};

// Fine-grained → 4 canonical meta-agent subsystems (used for URI-to-canonical mapping)
export const SUBSYSTEM_CANONICAL: Record<string, string> = {
  actor: "actor", animation: "actor", render: "actor",
  terrain: "terrain", shader: "shader",
  engine: "engine", camera: "engine", input: "engine",
  gpu: "engine", test: "engine",
  engine_core: "engine", engine_render: "engine",
  engine_ui: "engine", engine_ipc: "engine",
};

// ─── Public API (Viking required) ────────────────────────────────────────────

/**
 * Resolve a task description to canonical subsystem names.
 * Uses Viking L0 search scoped to viking://resources/delve, extracts subsystem from URIs.
 */
export async function resolveSubsystems(task: string): Promise<string[]> {
  await requireViking();

  const results = await vikingSearch(task, "L0", "viking://resources/delve", 5);
  if (results.length > 0) {
    const matched = new Set<string>();
    for (const r of results) {
      const m = r.uri.match(/^viking:\/\/resources\/delve\/([^/]+)/);
      if (m) {
        const sub = m[1];
        if (sub in SUBSYSTEM_VIKING_URI || ["terrain", "actor", "shader", "engine", "test", "game"].includes(sub)) {
          const canonical = sub === "test" ? "engine" : sub === "game" ? "engine" : sub;
          matched.add(canonical);
        }
      }
    }
    if (matched.size > 0) return Array.from(matched);
  }

  return [];
}

/**
 * Get codebase context for a task.
 * Uses Viking L1 search results (~2K token overviews per match).
 * Returns config.h only if Viking returns nothing.
 */
export async function getCodebaseContext(task: string, baseCwd?: string): Promise<string> {
  await requireViking();

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
  for (const r of results) {
    sections.push(`### ${r.uri}\n${r.snippet}`);
  }

  return sections.join("\n\n");
}

/**
 * Get codebase context scoped to a single canonical subsystem.
 * Uses Viking L1 overview of the subsystem's viking:// directory.
 */
export async function getSubsystemCodebaseContext(subsystem: string, baseCwd?: string): Promise<string> {
  await requireViking();

  const sections: string[] = [];
  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();

  const configPath = join(cwd, "src/game/config.h");
  if (existsSync(configPath)) {
    sections.push(`### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``);
  }

  const vikingUri = SUBSYSTEM_VIKING_URI[subsystem];
  if (vikingUri) {
    const overview = await vikingOverview(vikingUri);
    if (overview && overview.trim().length > 0) {
      sections.push(`### ${vikingUri} (L1 Overview)\n${overview}`);

      // If L1 overview is shallow (lacks function signatures), supplement with
      // L2 full reads of primary header files for this subsystem
      const hasSignatures = /\b\w+\s+\w+\s*\(/.test(overview);
      if (!hasSignatures) {
        console.error(`[context] L1 overview for ${subsystem} lacks function signatures — supplementing with L2 headers`);
        const headerSections = await readSubsystemHeaders(subsystem, vikingUri);
        if (headerSections.length > 0) {
          sections.push(...headerSections);
        }
      }
    }
  }

  return sections.join("\n\n");
}

/**
 * Read primary header files for a subsystem via Viking L2.
 * Searches for .h files under the subsystem's Viking URI and reads up to 3.
 */
async function readSubsystemHeaders(subsystem: string, vikingUri: string): Promise<string[]> {
  const sections: string[] = [];
  const results = await vikingSearch(`${subsystem} header declarations`, "L2", vikingUri, 3);
  for (const r of results) {
    if (r.uri.endsWith(".h") || r.uri.includes(".h/")) {
      const content = await vikingRead(r.uri);
      if (content && content.trim().length > 0) {
        sections.push(`### ${r.uri} (L2 Full)\n${content.slice(0, 8000)}`);
      }
    }
  }
  // If search didn't find headers, try direct L2 read of known dirs
  if (sections.length === 0) {
    const dirs = SUBSYSTEM_DIRS[subsystem] || [];
    for (const dir of dirs) {
      const headerUri = vikingUri + "/" + dir.split("/").pop();
      const content = await vikingRead(headerUri);
      if (content && content.trim().length > 0) {
        sections.push(`### ${headerUri} (L2 Full)\n${content.slice(0, 8000)}`);
      }
    }
  }
  return sections;
}

// ─── Model Context Limits (mirrors spawn.ts) ────────────────────────────────

const MODEL_CONTEXT_LIMITS: Record<string, number> = {
  "anthropic/claude-opus-4-6": 200_000,
  "anthropic/claude-sonnet-4-6": 200_000,
  "anthropic/claude-haiku-4-5": 200_000,
};
const CONTEXT_BUDGET_RATIO = 0.4;

/**
 * Get codebase context with Viking tier downgrade based on token budget.
 * Tries L1 (overview) first; if results exceed budget, downgrades to L0 (summary);
 * if still over budget, truncates to fit.
 *
 * Budget = 40% of model context window × 4 chars/token.
 */
export async function getCodebaseContextBudgeted(
  task: string,
  model: string,
  baseCwd?: string,
): Promise<string> {
  await requireViking();

  const { readFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = baseCwd || process.cwd();

  const contextLimit = MODEL_CONTEXT_LIMITS[model] || 200_000;
  const budgetChars = Math.floor(contextLimit * CONTEXT_BUDGET_RATIO * 4);

  // Always include config.h
  const sections: string[] = [];
  const configPath = join(cwd, "src/game/config.h");
  let configSection = "";
  if (existsSync(configPath)) {
    configSection = `### src/game/config.h\n\`\`\`cpp\n${readFileSync(configPath, "utf-8").slice(0, 3000)}\n\`\`\``;
    sections.push(configSection);
  }

  const reservedChars = configSection.length;
  const availableChars = budgetChars - reservedChars;

  // Try L1 first (detailed overviews)
  const l1Results = await vikingSearch(task, "L1", "viking://resources/delve", 8);
  if (l1Results.length > 0) {
    const l1Sections = l1Results.map((r) => `### ${r.uri}\n${r.snippet}`);
    const l1Total = l1Sections.reduce((sum, s) => sum + s.length, 0);

    if (l1Total <= availableChars) {
      console.error(`[context-budget] L1 tier: ${l1Total} chars within ${availableChars} budget`);
      sections.push(...l1Sections);
      return sections.join("\n\n");
    }

    // L1 exceeds budget — downgrade to L0 (summaries)
    console.error(`[context-budget] L1 tier ${l1Total} chars exceeds ${availableChars} budget — downgrading to L0`);
    const l0Results = await vikingSearch(task, "L0", "viking://resources/delve", 8);
    if (l0Results.length > 0) {
      const l0Sections = l0Results.map((r) => `### ${r.uri}\n${r.snippet}`);
      const l0Total = l0Sections.reduce((sum, s) => sum + s.length, 0);

      if (l0Total <= availableChars) {
        console.error(`[context-budget] L0 tier: ${l0Total} chars within ${availableChars} budget`);
        sections.push(...l0Sections);
        return sections.join("\n\n");
      }

      // L0 still exceeds — take what fits
      console.error(`[context-budget] L0 tier ${l0Total} chars still exceeds budget — truncating to fit`);
      let used = 0;
      for (const s of l0Sections) {
        if (used + s.length > availableChars) break;
        sections.push(s);
        used += s.length;
      }
    }
  }

  return sections.join("\n\n");
}
