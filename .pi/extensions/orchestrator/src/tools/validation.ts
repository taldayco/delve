// ─── System Validation ───────────────────────────────────────────────────────

import { SUBSYSTEM_DIRS } from "./context.js";
import { measureDomainComplexity } from "./metrics.js";
import { PROTECTED_STATE_FILES } from "./state.js";
import { DOMAIN_COMPLEXITY_THRESHOLD } from "./types.js";
import type { AuditFinding, AuditReport, FileSizeViolation } from "./types.js";

export function runSystemAudit(): AuditReport {
  const { readdirSync, readFileSync, existsSync, statSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = process.cwd();
  const findings: AuditFinding[] = [];

  // 1. Stale agents — agent .md files referencing non-existent src/ directories
  const agentsDir = join(cwd, ".pi/agents");
  try {
    const agentFiles = readdirSync(agentsDir).filter((f: string) => f.endsWith(".md"));
    for (const file of agentFiles) {
      const content = readFileSync(join(agentsDir, file), "utf-8");
      const dirRefs = content.match(/src\/[a-zA-Z0-9_/]+/g) || [];
      for (const ref of dirRefs) {
        // Skip template/placeholder paths used in agent prompt examples
        if (ref.startsWith("src/path/")) continue;
        const refPath = join(cwd, ref);
        if (!existsSync(refPath)) {
          findings.push({
            category: "stale-agent",
            severity: "warning",
            message: `Agent ${file} references non-existent directory: ${ref}`,
          });
        }
      }
    }
  } catch { /* ignore */ }

  // 2. Broken rule globs — rule YAML globs: pointing to missing base directories
  const rulesDir = join(cwd, ".pi/rules");
  try {
    const ruleFiles = readdirSync(rulesDir).filter((f: string) => f.endsWith(".md") || f.endsWith(".yaml") || f.endsWith(".yml"));
    for (const file of ruleFiles) {
      const content = readFileSync(join(rulesDir, file), "utf-8");
      const globMatch = content.match(/globs?:\s*(.+)/gi);
      if (globMatch) {
        for (const line of globMatch) {
          const paths = line.replace(/globs?:\s*/i, "").split(",").map((p: string) => p.trim().replace(/["'[\]]/g, ""));
          for (const p of paths) {
            // Extract base directory from glob pattern
            const baseDir = p.split("*")[0].replace(/\/+$/, "");
            if (baseDir && baseDir.startsWith("src/") && !existsSync(join(cwd, baseDir))) {
              findings.push({
                category: "broken-rule-glob",
                severity: "warning",
                message: `Rule ${file} has glob pointing to missing directory: ${baseDir}`,
              });
            }
          }
        }
      }
    }
  } catch { /* ignore */ }

  // 3. Unmapped directories — new src/game/ or src/engine/ subdirectories not in SUBSYSTEM_DIRS
  const knownDirs = new Set(Object.values(SUBSYSTEM_DIRS).flat());
  for (const parent of ["src/game", "src/engine"]) {
    const parentPath = join(cwd, parent);
    try {
      const entries = readdirSync(parentPath);
      for (const entry of entries) {
        const entryPath = join(parentPath, entry);
        try {
          if (statSync(entryPath).isDirectory()) {
            const relPath = `${parent}/${entry}`;
            if (!knownDirs.has(relPath)) {
              findings.push({
                category: "unmapped-directory",
                severity: "warning",
                message: `Directory ${relPath} is not mapped in SUBSYSTEM_DIRS`,
              });
            }
          }
        } catch { /* ignore */ }
      }
    } catch { /* ignore */ }
  }

  // 4. Domain overload — uses measureDomainComplexity
  const domainReports = measureDomainComplexity();
  for (const report of domainReports) {
    if (report.exceedsThreshold) {
      findings.push({
        category: "domain-overload",
        severity: "error",
        message: `Domain "${report.domain}" exceeds complexity threshold: score=${report.complexityScore} (threshold=${DOMAIN_COMPLEXITY_THRESHOLD}), ${report.fileCount} files, ${report.totalLines} lines in ${report.directory}`,
      });
    }
  }

  // 5. Orphaned state — state files older than 7 days
  const stateDir = join(cwd, ".pi/state");
  const cutoff = Date.now() - 7 * 24 * 60 * 60 * 1000;
  try {
    const stateFiles = readdirSync(stateDir);
    for (const file of stateFiles) {
      if (PROTECTED_STATE_FILES.has(file)) continue;
      const filePath = join(stateDir, file);
      try {
        const stat = statSync(filePath);
        if (stat.isFile() && stat.mtimeMs < cutoff) {
          findings.push({
            category: "orphaned-state",
            severity: "warning",
            message: `State file "${file}" is older than 7 days (last modified: ${new Date(stat.mtimeMs).toISOString()})`,
          });
        }
      } catch { /* ignore */ }
    }
  } catch { /* ignore */ }

  const errorCount = findings.filter((f) => f.severity === "error").length;
  const warningCount = findings.filter((f) => f.severity === "warning").length;

  return {
    findings,
    domainReports,
    summary: findings.length === 0
      ? "All checks passed — system is healthy"
      : `Found ${errorCount} error(s) and ${warningCount} warning(s) across ${findings.length} finding(s)`,
  };
}

export function fixStaleAgentRefs(findings: AuditFinding[]): { fixed: number; details: string[] } {
  const { readFileSync, writeFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = process.cwd();
  const agentsDir = join(cwd, ".pi/agents");
  const details: string[] = [];
  let fixed = 0;

  const staleFindings = findings.filter((f) => f.category === "stale-agent");
  for (const finding of staleFindings) {
    // Extract agent file and stale path from message
    const match = finding.message.match(/Agent (\S+) references non-existent directory: (src\/\S+)/);
    if (!match) continue;
    const [, agentFile, stalePath] = match;

    // Walk up to find nearest valid parent directory
    let candidate = stalePath;
    let validParent = "";
    while (candidate.includes("/")) {
      candidate = candidate.replace(/\/[^/]+$/, "");
      if (existsSync(join(cwd, candidate))) {
        validParent = candidate;
        break;
      }
    }
    if (!validParent) continue;

    const agentPath = join(agentsDir, agentFile);
    try {
      let content = readFileSync(agentPath, "utf-8");
      if (content.includes(stalePath)) {
        content = content.replaceAll(stalePath, validParent);
        writeFileSync(agentPath, content, "utf-8");
        details.push(`${agentFile}: ${stalePath} → ${validParent}`);
        fixed++;
      }
    } catch { /* ignore */ }
  }

  return { fixed, details };
}

export function fixBrokenRuleGlobs(findings: AuditFinding[]): { fixed: number; details: string[] } {
  const { readFileSync, writeFileSync, existsSync } = require("node:fs");
  const { join } = require("node:path");
  const cwd = process.cwd();
  const rulesDir = join(cwd, ".pi/rules");
  const details: string[] = [];
  let fixed = 0;

  const brokenFindings = findings.filter((f) => f.category === "broken-rule-glob");
  for (const finding of brokenFindings) {
    const match = finding.message.match(/Rule (\S+) has glob pointing to missing directory: (src\/\S+)/);
    if (!match) continue;
    const [, ruleFile, brokenBase] = match;

    // Walk up to find nearest valid parent directory
    let candidate = brokenBase;
    let validParent = "";
    while (candidate.includes("/")) {
      candidate = candidate.replace(/\/[^/]+$/, "");
      if (existsSync(join(cwd, candidate))) {
        validParent = candidate;
        break;
      }
    }
    if (!validParent) continue;

    const rulePath = join(rulesDir, ruleFile);
    try {
      let content = readFileSync(rulePath, "utf-8");
      if (content.includes(brokenBase)) {
        content = content.replaceAll(brokenBase, validParent);
        writeFileSync(rulePath, content, "utf-8");
        details.push(`${ruleFile}: ${brokenBase} → ${validParent}`);
        fixed++;
      }
    } catch { /* ignore */ }
  }

  return { fixed, details };
}

/**
 * Walk all .ts files under rootDir and collect files exceeding 500 lines.
 * Writes results to .pi/state/size_violations.json.
 */
export function detectFileSizeViolations(
  rootDir?: string,
): { violations: FileSizeViolation[] } {
  const { readdirSync, statSync, readFileSync, writeFileSync, mkdirSync } = require("node:fs");
  const { join, relative } = require("node:path");
  const root = rootDir || join(process.cwd(), ".pi/extensions/orchestrator/src");
  const violations: FileSizeViolation[] = [];

  function walk(dir: string) {
    let entries: string[];
    try { entries = readdirSync(dir); } catch { return; }
    for (const entry of entries) {
      const fullPath = join(dir, entry);
      try {
        const stat = statSync(fullPath);
        if (stat.isDirectory()) {
          if (entry !== "node_modules" && entry !== "dist") walk(fullPath);
        } else if (entry.endsWith(".ts") && !entry.endsWith(".d.ts")) {
          const content = readFileSync(fullPath, "utf-8");
          const lineCount = content.split("\n").length;
          if (lineCount > 500) {
            violations.push({ path: relative(process.cwd(), fullPath), lineCount });
          }
        }
      } catch { /* ignore */ }
    }
  }

  walk(root);
  violations.sort((a, b) => b.lineCount - a.lineCount);

  // Write results to state
  const stateDir = join(process.cwd(), ".pi/state");
  try {
    mkdirSync(stateDir, { recursive: true });
    writeFileSync(join(stateDir, "size_violations.json"), JSON.stringify({ violations }, null, 2), "utf-8");
  } catch { /* ignore */ }

  return { violations };
}
