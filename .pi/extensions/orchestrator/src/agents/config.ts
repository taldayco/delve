// ─── Skill & Rule Loader + Agent Config ──────────────────────────────────────

import { readFileSync } from "node:fs";
import { join } from "node:path";
import type { AgentConfig } from "./types.js";

export const PROJECT_ROOT = process.cwd();
export const SKILLS_DIR = join(PROJECT_ROOT, ".pi/skills");
export const RULES_DIR = join(PROJECT_ROOT, ".pi/rules");
export const AGENTS_DIR = join(PROJECT_ROOT, ".pi/agents");

export function loadFile(path: string): string {
  try {
    return readFileSync(path, "utf-8");
  } catch {
    return "";
  }
}

export function loadSkill(name: string): string {
  return loadFile(join(SKILLS_DIR, name, `${name}.md`));
}

export function loadRule(name: string): string {
  return loadFile(join(RULES_DIR, `${name}.md`));
}

export function loadSystemPrompt(): string {
  return loadFile(join(PROJECT_ROOT, ".pi/SYSTEM.md"));
}

export function loadAgentSystemPrompt(): string {
  return loadFile(join(PROJECT_ROOT, ".pi/SYSTEM_AGENT.md"));
}

// ─── Agent Config Loader ──────────────────────────────────────────────────

export const agentConfigCache = new Map<string, AgentConfig>();

/**
 * Parse YAML frontmatter from .pi/agents/<name>.md to extract tool restrictions.
 * Returns tool list and model from the agent definition file.
 */
export function loadAgentConfig(agentName: string): AgentConfig {
  const cached = agentConfigCache.get(agentName);
  if (cached) return cached;

  const defaults: AgentConfig = { name: agentName, tools: [], model: "anthropic/claude-sonnet-4-6" };
  const filePath = join(AGENTS_DIR, `${agentName}.md`);
  const content = loadFile(filePath);
  if (!content) return defaults;

  // Parse YAML frontmatter between --- delimiters
  const fmMatch = content.match(/^---\n([\s\S]*?)\n---/);
  if (!fmMatch) return defaults;

  const frontmatter = fmMatch[1];
  const config: AgentConfig = { ...defaults };

  const toolsMatch = frontmatter.match(/^tools:\s*(.+)$/m);
  if (toolsMatch) {
    config.tools = toolsMatch[1].split(",").map((t) => t.trim()).filter(Boolean);
  }

  const modelMatch = frontmatter.match(/^model:\s*(.+)$/m);
  if (modelMatch) {
    config.model = modelMatch[1].trim();
  }

  const thinkingMatch = frontmatter.match(/^thinking:\s*(.+)$/m);
  if (thinkingMatch) {
    config.thinking = thinkingMatch[1].trim();
  }

  agentConfigCache.set(agentName, config);
  return config;
}

/**
 * Map canonical subsystem names to their agent config file names.
 */
export const SUBSYSTEM_AGENT_MAP: Record<string, string> = {
  terrain: "terrain",
  actor: "actor",
  shader: "shader",
  engine: "engine",
};
