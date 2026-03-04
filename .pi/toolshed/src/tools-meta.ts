import { z } from "zod";
import { TOOL_REGISTRY } from "./types.js";

// ─── suggest_tools ───────────────────────────────────────────────────────────

export const suggestToolsSchema = {
  task: z.string().describe("Natural language task description"),
};

export async function suggestTools({ task }: { task: string }) {
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

// ─── compose_toolchain ───────────────────────────────────────────────────────

export const composeToolchainSchema = {
  workflow: z
    .enum([
      "implement_feature",
      "debug_test_failure",
      "add_palette",
      "add_biome",
      "debug_build",
      "explore_codebase",
      "review_changes",
      "verify_metrics",
    ])
    .describe("Workflow type"),
};

export async function composeToolchain({ workflow }: { workflow: string }) {
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
    verify_metrics: [
      { step: 1, tool: "build_compile", purpose: "Build project" },
      { step: 2, tool: "metrics_run", purpose: "Generate metric files" },
      { step: 3, tool: "metrics_read_domain", purpose: "Read terrain metrics" },
      { step: 4, tool: "metrics_read_domain", purpose: "Read mesh metrics" },
      { step: 5, tool: "metrics_read_domain", purpose: "Read performance metrics" },
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

// ─── tool_dependencies ───────────────────────────────────────────────────────

export const toolDependenciesSchema = {
  tool: z.string().describe("Tool name to get dependency info for"),
};

export async function toolDependencies({ tool: toolName }: { tool: string }) {
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
