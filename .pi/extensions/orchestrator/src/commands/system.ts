import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  executeBlueprint,
  loadBlueprint,
  validateBlueprint,
  type BlueprintContext,
} from "../blueprints.js";
import {
  askDecoupleAnalyst,
  askMapUpdater,
  writeState,
  readRunMetrics,
  pruneMetricsLog,
} from "../agents.js";
import {
  askBuildFixer,
  askReviewer,
} from "../agents.js";
import {
  cleanupMergedBranches,
  cleanupStaleState,
  runSystemAudit,
  detectMapCoverage,
  fixStaleAgentRefs,
  fixBrokenRuleGlobs,
  rebuildExtension,
  detectFileSizeViolations,
  measureDomainComplexity,
  applyFileBlocks,
  elapsed,
  slugify,
  gitBranch,
  cleanupWorktree,
  runBuild,
  getDiff,
  gitCommitAndPr,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
} from "../tools.js";
import { state, setState, attachAgentListeners, detachAgentListeners, setPhase, recordFailure } from "./display.js";
import { parseReviewDecision } from "./helpers.js";

export function registerSystemCommands(pi: ExtensionAPI) {
  // ── /minion-cleanup command ─────────────────────────────────────────────

  pi.registerCommand("minion-cleanup", {
    description: "Clean up merged minion branches and stale state files",
    handler: async (_args, ctx) => {
      const branchResult = cleanupMergedBranches();
      ctx.ui.notify(branchResult.summary, branchResult.deleted.length > 0 ? "success" : "info");

      const stateResult = cleanupStaleState();
      if (stateResult.deleted.length > 0) {
        ctx.ui.notify(stateResult.summary, "success");
      }

      pruneMetricsLog();
    },
  });

  // ── /minion-self-update command (C3) ────────────────────────────────────
  // Pipeline: detect_file_size_violations → automodularize → rebuild_ext
  // Also updates stale refs and maps missing subsystem dirs.

  pi.registerCommand("minion-self-update", {
    description: "Self-update: detect oversized source files, modularize them, rebuild extension, fix stale refs",
    handler: async (_args, ctx) => {
      ctx.ui.notify("Starting self-update pipeline...", "info");

      // 1. Detect file size violations
      ctx.ui.notify("Checking file sizes...", "info");
      const violations = detectFileSizeViolations();
      if (violations.violations.length > 0) {
        ctx.ui.notify(
          `Found ${violations.violations.length} oversized file(s): ${violations.violations.slice(0, 3).map((v) => `${v.path}(${v.lineCount}L)`).join(", ")}`,
          "warning"
        );
      } else {
        ctx.ui.notify("No file size violations found", "success");
      }

      // 2. Fix stale agent refs and broken rule globs
      ctx.ui.notify("Fixing stale refs...", "info");
      const audit = runSystemAudit();
      const refResult = fixStaleAgentRefs(audit.findings);
      const globResult = fixBrokenRuleGlobs(audit.findings);
      if (refResult.fixed > 0 || globResult.fixed > 0) {
        ctx.ui.notify(`Fixed ${refResult.fixed} stale refs, ${globResult.fixed} broken globs`, "success");
      }

      // 3. If violations exist, run the modularize blueprint
      if (violations.violations.length > 0) {
        ctx.ui.notify("Running modularize pipeline...", "info");
        const blueprint = loadBlueprint("modularize");
        if (blueprint && validateBlueprint(blueprint)) {
          const timestamp = new Date().toISOString().replace(/[:.]/g, "-").slice(0, 19);
          const bpCtx: BlueprintContext = {
            prompt: "Modularize oversized TypeScript files in the orchestrator extension",
            branch: `minion-self-update/${timestamp}`,
            startTime: Date.now(),
            ctx,
            data: { sizeViolations: violations.violations },
          };
          const result = await executeBlueprint(blueprint, bpCtx);
          ctx.ui.notify(
            result.ok ? "Modularize pipeline complete" : `Modularize pipeline failed at: ${result.failedPhase}`,
            result.ok ? "success" : "error"
          );
        } else {
          // Fallback: just rebuild
          const rebuild = rebuildExtension();
          ctx.ui.notify(rebuild.summary, rebuild.ok ? "success" : "error");
        }
      } else {
        // Just rebuild to ensure clean state
        const rebuild = rebuildExtension();
        ctx.ui.notify(rebuild.summary, rebuild.ok ? "success" : "info");
      }

      ctx.ui.notify("Self-update complete", "success");
    },
  });

  // ── /minion-audit command ───────────────────────────────────────────────

  pi.registerCommand("minion-audit", {
    description: "Run a system audit: check for stale agents, broken rules, unmapped directories, domain overload, and orphaned state — then auto-fix what it can",
    handler: async (_args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Running system audit...", "info");
      const report = runSystemAudit();

      // Format initial findings as markdown
      const lines: string[] = [
        "# System Audit Report",
        "",
        `**Initial scan:** ${report.summary}`,
        "",
      ];

      if (report.findings.length > 0) {
        lines.push("## Initial Findings", "");
        for (const finding of report.findings) {
          const icon = finding.severity === "error" ? "ERROR" : "WARNING";
          lines.push(`- **[${icon}]** (${finding.category}) ${finding.message}`);
        }
        lines.push("");
      }

      // ── Auto-fix phase ──────────────────────────────────────────────────
      const fixLines: string[] = ["## Auto-Fix Results", ""];
      let totalFixed = 0;

      // 1. Orphaned state
      const stateResult = cleanupStaleState();
      if (stateResult.deleted.length > 0) {
        fixLines.push(`- **Orphaned state:** ${stateResult.summary}`);
        totalFixed += stateResult.deleted.length;
      }

      // 2. Stale agent refs
      const staleResult = fixStaleAgentRefs(report.findings);
      if (staleResult.fixed > 0) {
        fixLines.push(`- **Stale agent refs:** fixed ${staleResult.fixed}`);
        for (const d of staleResult.details) fixLines.push(`  - ${d}`);
        totalFixed += staleResult.fixed;
      }

      // 3. Broken rule globs
      const globResult = fixBrokenRuleGlobs(report.findings);
      if (globResult.fixed > 0) {
        fixLines.push(`- **Broken rule globs:** fixed ${globResult.fixed}`);
        for (const d of globResult.details) fixLines.push(`  - ${d}`);
        totalFixed += globResult.fixed;
      }

      // 4. Unmapped directories — invoke map updater agent
      const unmappedFindings = report.findings.filter((f) => f.category === "unmapped-directory");
      if (unmappedFindings.length > 0) {
        ctx.ui.notify(`Fixing ${unmappedFindings.length} unmapped directory(s)...`, "info");
        const coverage = detectMapCoverage();
        if (coverage.unmappedDirs.length > 0) {
          const { readFileSync } = require("node:fs");
          const { join } = require("node:path");
          const toolsPath = join(process.cwd(), ".pi/extensions/orchestrator/src/tools.ts");
          let toolsContent = "";
          try {
            const full = readFileSync(toolsPath, "utf-8");
            const start = full.indexOf("const KEYWORD_SYNONYMS");
            const end = full.indexOf("};", full.indexOf("const SUBSYSTEM_CANONICAL")) + 2;
            if (start >= 0 && end > start) toolsContent = full.slice(start, end);
          } catch { /* ignore */ }

          const mapResult = await askMapUpdater({ coverageReport: coverage, currentToolsContent: toolsContent });
          const appliedCount = applyFileBlocks(mapResult);
          if (appliedCount > 0) {
            fixLines.push(`- **Unmapped directories:** updated ${appliedCount} file(s)`);
            totalFixed += appliedCount;
            // Rebuild extension so new maps take effect
            const buildResult = rebuildExtension();
            fixLines.push(`- **Extension rebuild:** ${buildResult.summary}`);
          }
        }
      }

      // 5. Domain overload — report only
      const overloadFindings = report.findings.filter((f) => f.category === "domain-overload");
      if (overloadFindings.length > 0) {
        fixLines.push(`- **Domain overload:** ${overloadFindings.length} domain(s) over threshold — run \`/minion-decouple\` to address`);
      }

      if (totalFixed === 0) {
        fixLines.push("- No auto-fixable issues found");
      }
      fixLines.push("");

      lines.push(...fixLines);

      // ── Re-audit to verify ────────────────────────────────────────────
      if (totalFixed > 0) {
        ctx.ui.notify("Re-running audit to verify fixes...", "info");
        const recheck = runSystemAudit();
        lines.push("## Post-Fix Verification", "");
        lines.push(`**Result:** ${recheck.summary}`);
        if (recheck.findings.length > 0) {
          lines.push("");
          for (const finding of recheck.findings) {
            const icon = finding.severity === "error" ? "ERROR" : "WARNING";
            lines.push(`- **[${icon}]** (${finding.category}) ${finding.message}`);
          }
        }
        lines.push("");
      }

      // Domain complexity (always shown)
      lines.push("## Domain Complexity", "");
      for (const domain of report.domainReports) {
        const flag = domain.exceedsThreshold ? " **OVER THRESHOLD**" : "";
        lines.push(`- **${domain.domain}** (${domain.directory}): score=${domain.complexityScore}, files=${domain.fileCount}, lines=${domain.totalLines}${flag}`);
      }

      const markdown = lines.join("\n");
      writeState("audit_report.md", markdown);

      const fixSummary = totalFixed > 0 ? ` | Auto-fixed ${totalFixed} issue(s)` : "";
      ctx.ui.notify(`${report.summary}${fixSummary}`, report.findings.length > 0 ? "warning" : "success");
      ctx.ui.notify("Full report: .pi/state/audit_report.md", "info");
    },
  });

  // ── /minion-decouple command ────────────────────────────────────────────

  pi.registerCommand("minion-decouple", {
    description: "Propose a domain split for an over-complex domain. Usage: /minion-decouple [domain]",
    handler: async (args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Measuring domain complexity...", "info");
      const reports = measureDomainComplexity();
      const overThreshold = reports.filter((r) => r.exceedsThreshold);

      if (overThreshold.length === 0) {
        ctx.ui.notify("No domains exceed the complexity threshold — no decoupling needed", "info");
        return;
      }

      // Pick specified domain or highest-scoring one
      const targetDomain = args?.trim() || "";
      let target = overThreshold.find((r) => r.domain === targetDomain);
      if (!target) {
        target = overThreshold.sort((a, b) => b.complexityScore - a.complexityScore)[0];
        if (targetDomain) {
          ctx.ui.notify(`Domain "${targetDomain}" not over threshold — using highest: ${target.domain}`, "warning");
        }
      }

      ctx.ui.notify(`Analyzing domain "${target.domain}" (score=${target.complexityScore})...`, "info");

      const recentMetrics = readRunMetrics(20);
      const proposal = await askDecoupleAnalyst({
        domainReport: target,
        recentMetrics,
        files: target.files,
      });

      ctx.ui.notify(`Decouple proposal written to .pi/state/decouple_proposal.md`, "success");
      ctx.ui.notify("Review the proposal, then run /minion-decouple-execute to apply it", "info");
    },
  });

  // ── /minion-update-maps command ─────────────────────────────────────────

  pi.registerCommand("minion-update-maps", {
    description: "Detect unmapped directories and update KEYWORD_SYNONYMS/SUBSYSTEM_DIRS maps",
    handler: async (_args, ctx) => {
      cleanupMergedBranches();

      ctx.ui.notify("Detecting map coverage gaps...", "info");
      const coverage = detectMapCoverage();

      if (coverage.unmappedDirs.length === 0) {
        ctx.ui.notify("All directories are mapped — no updates needed", "success");
        return;
      }

      ctx.ui.notify(`Found ${coverage.unmappedDirs.length} unmapped directory(s): ${coverage.unmappedDirs.join(", ")}`, "warning");

      // Read current tools.ts for the maps section
      const { readFileSync } = require("node:fs");
      const { join } = require("node:path");
      const toolsPath = join(process.cwd(), ".pi/extensions/orchestrator/src/tools.ts");
      let toolsContent = "";
      try {
        const full = readFileSync(toolsPath, "utf-8");
        // Extract just the maps section
        const start = full.indexOf("const KEYWORD_SYNONYMS");
        const end = full.indexOf("};", full.indexOf("const SUBSYSTEM_CANONICAL")) + 2;
        if (start >= 0 && end > start) {
          toolsContent = full.slice(start, end);
        }
      } catch { /* ignore */ }

      ctx.ui.notify("Spawning map updater agent...", "info");
      const result = await askMapUpdater({
        coverageReport: coverage,
        currentToolsContent: toolsContent,
      });

      const appliedCount = applyFileBlocks(result);
      if (appliedCount > 0) {
        ctx.ui.notify(`Updated ${appliedCount} file(s). Rebuilding extension...`, "info");

        // Auto-rebuild extension
        const buildResult = rebuildExtension();
        if (buildResult.ok) {
          ctx.ui.notify(buildResult.summary, "success");
        } else {
          ctx.ui.notify(buildResult.summary, "error");
        }

        // Re-verify coverage
        const recheck = detectMapCoverage();
        if (recheck.unmappedDirs.length === 0) {
          ctx.ui.notify("Verification passed: all directories are now mapped", "success");
        } else {
          ctx.ui.notify(`Verification: ${recheck.unmappedDirs.length} directory(s) still unmapped: ${recheck.unmappedDirs.join(", ")}`, "warning");
        }
      } else {
        writeState("map_update_suggestion.md", result);
        ctx.ui.notify("No file blocks produced. Suggestion written to .pi/state/map_update_suggestion.md", "warning");
      }
    },
  });

  // ── /minion-status command ──────────────────────────────────────────────

  pi.registerCommand("minion-status", {
    description: "Show current minion run status",
    handler: async (_args, ctx) => {
      if (!state) {
        ctx.ui.notify("No minion run in progress", "info");
        return;
      }
      ctx.ui.notify(
        `Phase: ${state.phase} | Build rounds: ${state.buildFixRound}/${MAX_BUILD_FIX_ROUNDS} | Test rounds: ${state.testFixRound}/${MAX_TEST_FIX_ROUNDS} | Elapsed: ${elapsed(state.startTime)}`,
        "info"
      );
    },
  });
}
