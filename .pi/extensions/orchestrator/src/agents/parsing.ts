// ─── Subtask Parsing & Prompt Building ───────────────────────────────────────

import {
  loadAgentSystemPrompt,
  loadSkill,
  loadRule,
} from "./config.js";
import type { Subtask, WorkerSubtask, MetaDecomposition } from "./types.js";

export { Subtask };

/** Strip markdown fences and preamble from LLM JSON output. */
export function sanitizeJsonOutput(raw: string): string {
  const fenced = raw.match(/```\w*\s*([\s\S]*?)```/);
  const inner = fenced ? fenced[1].trim() : raw.trim();
  const jsonStart = inner.search(/[{\[]/);
  return jsonStart >= 0 ? inner.slice(jsonStart) : inner;
}

export const SUBSYSTEM_PATH_PATTERNS: [RegExp, string][] = [
  [/src\/game\/terrain\//i, "terrain"],
  [/src\/game\/render\//i, "actor"],
  [/src\/shaders\//i, "shader"],
  [/src\/engine\//i, "engine"],
  [/src\/test\//i, "engine"],
];

/**
 * Infer subsystem from subtask body text by looking for file path patterns.
 * Falls back to "engine" if no pattern matches.
 */
export function inferSubsystem(text: string): string {
  for (const [pattern, subsystem] of SUBSYSTEM_PATH_PATTERNS) {
    if (pattern.test(text)) return subsystem;
  }
  // Keyword fallback
  const lower = text.toLowerCase();
  if (/\b(noise|hex|contour|plateau|lava|mesh|elevation)\b/.test(lower)) return "terrain";
  if (/\b(skeleton|ik|gait|animation|actor|arm|leg|joint)\b/.test(lower)) return "actor";
  if (/\b(glsl|spir-?v|vertex|fragment|compute|shader)\b/.test(lower)) return "shader";
  return "engine";
}

/**
 * Parse a planner's output into per-subsystem subtasks.
 * Tolerates common LLM format deviations:
 *   - ## or ### heading levels
 *   - [tag] anywhere on the header line (not just after the number)
 *   - em-dash descriptions, checkmarks, bold text in headers
 *   - Missing [tag] (infers subsystem from body content)
 */
export function parseSubtasks(plan: string): Subtask[] {
  const subtasks: Subtask[] = [];
  let match;

  // Pass 1: headers with explicit [subsystem] bracket tags
  const taggedRegex = /#{2,3}\s*Subtask\s+\d+[^[\n]*\[(\w+)\][^\n]*\n([\s\S]*?)(?=#{2,3}\s*Subtask\s+\d+|$)/gi;
  while ((match = taggedRegex.exec(plan)) !== null) {
    const subsystem = match[1].toLowerCase();
    const task = match[2].trim();
    if (task.length > 0) {
      subtasks.push({ subsystem, task });
    }
  }

  if (subtasks.length > 0) return subtasks;

  // Pass 2: headers without bracket tags — infer subsystem from body
  const untaggedRegex = /#{2,3}\s*Subtask\s+\d+[^\n]*\n([\s\S]*?)(?=#{2,3}\s*Subtask\s+\d+|$)/gi;
  while ((match = untaggedRegex.exec(plan)) !== null) {
    const body = match[1].trim();
    if (body.length > 0) {
      subtasks.push({ subsystem: inferSubsystem(body), task: body });
    }
  }

  return subtasks;
}

/**
 * Parse a Sonnet meta-agent's JSON decomposition output.
 * Tolerates markdown fences around JSON.
 */
export function parseMetaDecomposition(output: string): MetaDecomposition {
  try {
    const raw = sanitizeJsonOutput(output);
    const parsed = JSON.parse(raw);
    const subtasks: WorkerSubtask[] = (parsed.subtasks || []).map((st: any) => ({
      file: st.file || "",
      action: (st.action || "MODIFY").toUpperCase(),
      instructions: st.instructions || "",
      context_files: st.context_files || st.dependencies || [],
      worker_prompt: st.worker_prompt || st.instructions || "",
    }));
    return { subtasks };
  } catch {
    // Fallback: couldn't parse decomposition — return empty
    return { subtasks: [] };
  }
}

export const SUBSYSTEM_CONFIG: Record<string, { skill: string; rule: string; description: string }> = {
  terrain: {
    skill: "terrain",
    rule: "terrain",
    description: "TERRAIN SPECIALIST. You own noise generation, composition, contour detection, hex columns, lava/void, and mesh generation. Directories: src/game/terrain/.",
  },
  actor: {
    skill: "actor",
    rule: "actors",
    description: "ACTOR SPECIALIST. You own skeleton, inverse kinematics, gait cycles, proportions, and animation. Directories: src/game/render/, src/game/actor.h.",
  },
  shader: {
    skill: "shader",
    rule: "shaders",
    description: "SHADER SPECIALIST. You own GLSL 4.5, SPIR-V compilation, vertex layouts, compute shaders, and lighting. Directories: src/shaders/.",
  },
  engine: {
    skill: "engine",
    rule: "engine",
    description: "ENGINE SPECIALIST. You own application lifecycle, GPU context, camera, input, ECS, ImGui UI, and rendering pipeline. Directories: src/engine/.",
  },
};

/**
 * Build a system prompt for a subsystem agent by concatenating
 * SYSTEM.md + skill file + rule file + role description.
 */
export function buildSubsystemPrompt(subsystem: string): string {
  const config = SUBSYSTEM_CONFIG[subsystem];
  if (!config) return loadAgentSystemPrompt();

  const parts = [loadAgentSystemPrompt()];

  const skill = loadSkill(config.skill);
  if (skill) parts.push("---\n\n" + skill);

  const rule = loadRule(config.rule);
  if (rule) parts.push("---\n\n" + rule);

  parts.push(`---

## Your Role
You are a ${config.description}

Given a task and relevant source files, produce a complete implementation.

## Output Format
For each file change, output:

### FILE: <path>
#### ACTION: [CREATE | MODIFY]
\`\`\`cpp
[complete file content]
\`\`\`

## Constraints
- Output ONLY file blocks. No preamble, no explanation after.
- Follow existing code conventions exactly.
- Only change what the task requires — no unnecessary refactoring.
- Include all necessary #includes.
- Every file block must contain the COMPLETE file content.`);

  return parts.join("\n\n");
}

/**
 * Build a meta-decomposer system prompt for a subsystem agent.
 * The decomposer's job is to analyze the task and produce a JSON
 * decomposition of per-file worker subtasks.
 */
export function buildMetaDecomposerPrompt(subsystem: string): string {
  const config = SUBSYSTEM_CONFIG[subsystem];
  if (!config) return loadAgentSystemPrompt();

  const parts = [loadAgentSystemPrompt()];

  const skill = loadSkill(config.skill);
  if (skill) parts.push("---\n\n" + skill);

  const rule = loadRule(config.rule);
  if (rule) parts.push("---\n\n" + rule);

  parts.push(`---

## Your Role
You are a META-${config.description}

You do NOT implement changes yourself. Instead, you DECOMPOSE the task into focused per-file
worker subtasks that Haiku-tier workers can execute independently.

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/path/to/file.cpp",
      "action": "MODIFY",
      "instructions": "Detailed description of what to change in this file",
      "context_files": ["src/path/to/dependency.h"],
      "worker_prompt": "You are editing file.cpp in a C++20 terrain generator. [Focused instructions for the Haiku worker including exact function signatures, includes needed, and expected output]"
    }
  ]
}
\`\`\`

## Constraints
- Each subtask targets EXACTLY ONE file.
- worker_prompt must be self-contained — the worker has NO other context.
- Include all necessary details: function signatures, types, includes, conventions.
- worker_prompt MUST include the exact filepath and line numbers of every function or block to add/modify (e.g., "Modify function foo at line 42 of src/bar.cpp to add..."). Line numbers come from the file contents provided above.
- worker_prompt MUST include EXACT code to write — not descriptions of code. Include literal function bodies, struct definitions, and #include directives. The Haiku worker is a code typist, not an architect.
- Order subtasks by dependency (headers before implementations).
- Only decompose changes the task requires — no unnecessary refactoring.
- Output ONLY the JSON block. No preamble, no explanation.`);

  return parts.join("\n\n");
}
