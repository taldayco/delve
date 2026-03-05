// ─── Verifier & Domain Analyzer ──────────────────────────────────────────────

import { spawnSubagent } from "./spawn.js";
import { loadAgentConfig, loadAgentSystemPrompt, loadFile, AGENTS_DIR } from "./config.js";
import { join } from "node:path";
import { existsSync, readFileSync } from "node:fs";

export async function spawnDomainAnalyzer(opts: {
  domain: string;
  metricData: string;
  task: string;
  cwd?: string;
}): Promise<{ domain: string; result: string }> {
  const systemPrompt = `You are a METRICS ANALYZER for the "${opts.domain}" domain of the Delve game engine.
You receive JSON metric data and must determine if the metrics are healthy.

Output EXACTLY one of:
- PASS: [brief reason]
- FAIL: [brief reason with the specific problematic metric and value]
- WARNING: [brief reason]

Be concise. One line only.`;

  const prompt = `## Task Context
${opts.task}

## ${opts.domain}.json
\`\`\`json
${opts.metricData}
\`\`\`

Analyze these metrics. Is this domain healthy?`;

  const result = await spawnSubagent({
    prompt,
    systemPrompt,
    model: "anthropic/claude-haiku-4-5",
    thinking: "off",
    agentName: `analyzer-${opts.domain}`,
    cwd: opts.cwd,
  });

  return { domain: opts.domain, result };
}

export async function askVerifier(opts: {
  task: string;
  metricsDir: string;
  domains: string[];
  cwd?: string;
}): Promise<string> {
  const verifierConfig = loadAgentConfig("verifier");
  const model = verifierConfig.model || "anthropic/claude-sonnet-4-6";

  // Read all domain metric files
  const domainData: Record<string, string> = {};
  for (const domain of opts.domains) {
    const filePath = join(opts.metricsDir, `${domain}.json`);
    if (existsSync(filePath)) {
      domainData[domain] = readFileSync(filePath, "utf-8");
    }
  }

  // Step 1: Fan out Haiku domain analyzers in parallel (one per domain)
  console.error(`[meta-verifier] Delegating ${Object.keys(domainData).length} domain analyses to workers`);
  const workerResults = await Promise.all(
    Object.entries(domainData).map(([domain, metricData]) =>
      spawnDomainAnalyzer({ domain, metricData, task: opts.task, cwd: opts.cwd })
    ),
  );

  const workerSummary = workerResults
    .map((r) => `### ${r.domain}\n${r.result}`)
    .join("\n\n");

  // Step 2: Sonnet synthesizes final verification summary from worker reports
  const agentBody = loadFile(join(AGENTS_DIR, "verifier.md"));
  const bodyStart = agentBody.indexOf("---", 3);
  const verifierInstructions = bodyStart >= 0 ? agentBody.slice(bodyStart + 3).trim() : "";

  const synthesisPrompt = `${loadAgentSystemPrompt()}
${verifierInstructions ? "\n\n" + verifierInstructions : ""}

---

## Your Role
You are a VERIFICATION SYNTHESIZER. Given per-domain analysis results from workers,
produce a unified verification summary.

## Output Format
## Verification Summary

**Overall: PASS | FAIL**

### Per-Domain Worker Results
[Include the worker analysis results]

### Issues Found
- [List any FAIL items with explanation]

## Decision Rules
- If ANY domain reported FAIL → Overall FAIL
- If all domains reported PASS (with optional WARNINGs) → Overall PASS
- Include all worker-reported issues`;

  const domainSections = Object.entries(domainData)
    .map(([domain, content]) => `### ${domain}.json\n\`\`\`json\n${content}\n\`\`\``)
    .join("\n\n");

  const synthesisInput = `## Task Being Verified
${opts.task}

## Worker Domain Analysis Results
${workerSummary}

## Raw Metric Data (for reference)
${domainSections}

Synthesize a final verification verdict from the worker findings.`;

  return spawnSubagent({
    prompt: synthesisInput,
    systemPrompt: synthesisPrompt,
    model,
    thinking: verifierConfig.thinking || "low",
    agentName: "verifier-synthesizer",
    cwd: opts.cwd,
  });
}
