// ─── Generator / Analyst Agents ───────────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, PROJECT_ROOT } from "./config.js";
import { writeState } from "./state.js";
import { askWorker } from "./workers.js";
import { parseMetaDecomposition } from "./parsing.js";
import { readFileSync, existsSync, statSync } from "node:fs";
import { join } from "node:path";
import type { RunMetricsRecord } from "./types.js";

export async function askDecoupleAnalyst(opts: {
  domainReport: { domain: string; directory: string; fileCount: number; totalLines: number; complexityScore: number; files: string[] };
  recentMetrics: RunMetricsRecord[];
  files: string[];
  cwd?: string;
}): Promise<string> {
  const decouplerConfig = loadAgentConfig("decoupler");
  const model = decouplerConfig.model || "anthropic/claude-sonnet-4-6";

  const fileList = opts.files.map((f) => `- ${f}`).join("\n");
  const metricsSection = opts.recentMetrics.length > 0
    ? `## Recent Agent Metrics\n${opts.recentMetrics.map((m) => `- ${m.agentName}: ${m.contextUsagePct}% context, ${m.durationMs}ms, success=${m.success}`).join("\n")}`
    : "## Recent Agent Metrics\nNo recent metrics available.";

  // Step 1: Sonnet decomposes the domain analysis into per-file-group analysis tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a META-DECOUPLER. You do NOT propose splits yourself. Instead, you DECOMPOSE the domain
into file groups for Haiku workers to analyze for coupling and responsibility boundaries.

For each potential file group, produce a worker task that asks the worker to:
- Identify the group's primary responsibility
- List its public interfaces (headers used outside the group)
- Identify dependencies on other files in the domain

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": "src/game/terrain/noise.cpp",
      "action": "MODIFY",
      "instructions": "Analyze file group: noise generation files",
      "context_files": ["src/game/terrain/noise.h", "src/game/terrain/noise_layers.cpp"],
      "worker_prompt": "You are analyzing files for domain splitting in a C++20 terrain generator. Files in this group: [list]. For each file, output: RESPONSIBILITY: [1 sentence] | PUBLIC_API: [exported symbols used outside this group] | DEPENDS_ON: [files from other groups this file imports]. Output one line per file."
    }
  ]
}
\`\`\`

## Constraints
- Group related files (e.g., noise.h + noise.cpp + noise_layers.cpp).
- Each subtask analyzes one logical file group (3-8 files).
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Domain Analysis
- Domain: ${opts.domainReport.domain}
- Directory: ${opts.domainReport.directory}
- Files: ${opts.domainReport.fileCount}
- Lines: ${opts.domainReport.totalLines}
- Complexity Score: ${opts.domainReport.complexityScore}

## File Listing
${fileList}

${metricsSection}

Decompose this domain into file groups for coupling analysis.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model,
    thinking: decouplerConfig.thinking || "medium",
    tools: decouplerConfig.tools.length > 0 ? decouplerConfig.tools : ["read"],
    agentName: "decoupler-meta",
    cwd: opts.cwd,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if decomposition failed, use direct analysis
  if (parsed.subtasks.length === 0) {
    console.error("[meta-decoupler] Decomposition failed — falling back to direct analysis");
    const directPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a DOMAIN SPLIT SPECIALIST. Analyze this domain and propose how to split it into well-bounded sub-domains.

## Output Format
Produce a structured SPLIT_PROPOSAL in markdown with sub-domains, agent definitions, and keyword map additions.`;

    const result = await spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model,
      thinking: decouplerConfig.thinking || "medium",
      tools: decouplerConfig.tools.length > 0 ? decouplerConfig.tools : ["read"],
      agentName: "decoupler",
      cwd: opts.cwd,
    });
    writeState("decouple_proposal.md", result);
    return result;
  }

  // Step 2: Delegate per-group coupling analysis to Haiku workers in parallel
  console.error(`[meta-decoupler] Decomposed into ${parsed.subtasks.length} file group analyses`);
  const basePath = opts.cwd || PROJECT_ROOT;
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const fileContents: Record<string, string> = {};
      const allFiles = [st.file, ...st.context_files].slice(0, 5);
      for (const f of allFiles) {
        const fullPath = f.startsWith("/") ? f : join(basePath, f);
        if (existsSync(fullPath) && statSync(fullPath).isFile()) {
          const content = readFileSync(fullPath, "utf-8");
          fileContents[f] = content.length > 3000 ? content.slice(0, 3000) + "\n... [truncated]" : content;
        }
      }
      const result = await askWorker({
        systemPrompt: `You are a code coupling analyzer for a C++20 project.
Analyze the provided files and output per-file: RESPONSIBILITY: [1 sentence] | PUBLIC_API: [exported symbols] | DEPENDS_ON: [external dependencies].`,
        task: st.worker_prompt,
        fileContents,
        cwd: opts.cwd,
      });
      return `### File Group: ${st.file}\n${result}`;
    }),
  );

  // Step 3: Sonnet synthesizes split proposal from worker coupling analyses
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a SPLIT SYNTHESIZER. Given per-file-group coupling analyses from workers,
produce a structured domain split proposal.

## Output Format
## SPLIT_PROPOSAL

### New Sub-domains
#### Sub-domain: <name>
- **Directory:** <path>
- **Files to move:** <list>
- **Responsibility:** <description>

### New Agent Definitions
### FILE: .pi/agents/<name>.md
(agent definition for each sub-domain)

### Keyword Map Additions
New entries for KEYWORD_SYNONYMS and SUBSYSTEM_DIRS.

## Constraints
- Each sub-domain must have >= 5 files
- Preserve public interfaces
- Don't cross game/engine boundary`;

  const synthesisInput = `## Domain
${opts.domainReport.domain} (${opts.domainReport.directory}, ${opts.domainReport.fileCount} files, ${opts.domainReport.totalLines} lines)

## Worker Coupling Analyses
${workerResults.join("\n\n")}

${metricsSection}

Synthesize a domain split proposal from the coupling analyses.`;

  const result = await spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: decouplerConfig.thinking || "medium",
    agentName: "decoupler-synthesizer",
    cwd: opts.cwd,
  });

  writeState("decouple_proposal.md", result);
  return result;
}

export async function askMapUpdater(opts: {
  coverageReport: { unmappedDirs: string[]; unmappedKeywords: string[]; currentKeywordCount: number };
  currentToolsContent: string;
  cwd?: string;
}): Promise<string> {
  // Step 1: Sonnet decomposes unmapped dirs into per-directory classification tasks
  const decomposerPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a META-MAP-UPDATER. You do NOT write map code yourself. Instead, you DECOMPOSE
the unmapped directories into classification tasks that Haiku workers can handle.

For each unmapped directory, produce a worker task that classifies it into a canonical subsystem.

## Output Format (JSON only)
\`\`\`json
{
  "subtasks": [
    {
      "file": ".pi/extensions/orchestrator/src/tools.ts",
      "action": "MODIFY",
      "instructions": "Classify directory src/game/new_feature/ into a canonical subsystem",
      "context_files": [],
      "worker_prompt": "Classify this directory into one of these canonical subsystems: terrain, actor, shader, engine. Directory: [path]. Files in it: [list]. Output EXACTLY: SUBSYSTEM: [name] | KEYWORDS: [comma-separated relevant keywords]"
    }
  ]
}
\`\`\`

## Constraints
- One subtask per unmapped directory.
- Output ONLY the JSON block. No preamble, no explanation.`;

  const prompt = `## Unmapped Directories
${opts.coverageReport.unmappedDirs.map((d) => `- ${d}`).join("\n") || "None"}

## Unmapped Keywords
${opts.coverageReport.unmappedKeywords.map((k) => `- ${k}`).join("\n") || "None"}

## Current Keyword Count
${opts.coverageReport.currentKeywordCount}

## Current Map Definitions (from tools.ts)
\`\`\`typescript
${opts.currentToolsContent}
\`\`\`

Decompose unmapped directories into classification tasks.`;

  const decomposition = await spawnSubagent({
    prompt,
    systemPrompt: decomposerPrompt,
    model: "anthropic/claude-sonnet-4-6",
    thinking: "low",
    tools: ["read"],
    agentName: "map-updater-meta",
    cwd: opts.cwd,
  });

  const parsed = parseMetaDecomposition(decomposition);

  // Fallback: if no unmapped dirs or decomposition failed, do direct update
  if (parsed.subtasks.length === 0) {
    console.error("[meta-map-updater] Decomposition failed — falling back to direct update");
    const directPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a MAP UPDATER. Produce updated map code for unmapped directories.
Output a single FILE block with updated KEYWORD_SYNONYMS, SUBSYSTEM_DIRS, and SUBSYSTEM_CANONICAL.`;

    return spawnSubagent({
      prompt,
      systemPrompt: directPrompt,
      model: "anthropic/claude-sonnet-4-6",
      thinking: "low",
      tools: ["read"],
      agentName: "map-updater",
      cwd: opts.cwd,
    });
  }

  // Step 2: Delegate per-directory classification to Haiku workers
  console.error(`[meta-map-updater] Decomposed into ${parsed.subtasks.length} classification tasks`);
  const workerResults = await Promise.all(
    parsed.subtasks.map(async (st) => {
      const result = await askWorker({
        systemPrompt: `You are a directory classifier for a C++20 project. Classify directories into: terrain, actor, shader, or engine.
Output EXACTLY: SUBSYSTEM: [name] | KEYWORDS: [comma-separated keywords]`,
        task: st.worker_prompt,
        cwd: opts.cwd,
      });
      return result;
    }),
  );

  // Step 3: Sonnet synthesizes map updates from worker classifications
  const synthesisPrompt = `${loadAgentSystemPrompt()}

---

## Your Role
You are a MAP SYNTHESIZER. Given per-directory classifications from workers,
produce the updated map code for tools.ts.

## Output Format
### FILE: .pi/extensions/orchestrator/src/tools.ts
\`\`\`typescript
// Updated KEYWORD_SYNONYMS, SUBSYSTEM_DIRS, and SUBSYSTEM_CANONICAL objects
\`\`\`

## Constraints
- Preserve ALL existing mappings
- Only ADD new entries based on worker classifications`;

  const synthesisInput = `## Worker Classifications
${workerResults.map((r, i) => `### Directory ${i + 1}\n${r}`).join("\n\n")}

## Current Map Definitions
\`\`\`typescript
${opts.currentToolsContent}
\`\`\`

Produce the updated map code incorporating these classifications.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model: "anthropic/claude-sonnet-4-6",
    thinking: "low",
    agentName: "map-updater-synthesizer",
    cwd: opts.cwd,
  });
}
