import {
  askBuildFixer,
  askTestFixer,
  askDecoupleAnalyst,
  readRunMetrics,
  writeState,
  readState,
  pruneStateHistory,
  pruneMetricsLog,
} from "../agents.js";
import {
  applyFileBlocks,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  getChangedFiles,
  measureDomainComplexity,
  runSystemAudit,
  fixStaleAgentRefs,
  fixBrokenRuleGlobs,
  rebuildExtension,
  cleanupStaleState,
  cleanupMergedBranches,
  detectFileSizeViolations,
  cleanupStaleWorktrees,
  DOMAIN_COMPLEXITY_THRESHOLD,
} from "../tools.js";
import type { BlueprintContext } from "./types.js";

type PhaseResult = { ok: boolean; output: string };
type PhaseHandler = (ctx: BlueprintContext) => Promise<PhaseResult>;

// ─── Self-management phase handlers (fix, audit, cleanup) ────────────────────

export const SELF_PHASE_HANDLERS: Record<string, PhaseHandler> = {
  fix_build: async (ctx) => {
    const wt = ctx.data.worktreePath!;
    const buildLog = readState("build_log.txt");
    const fix = await askBuildFixer({
      buildOutput: buildLog.slice(-3000),
      round: (ctx.data.buildFixRound as number || 0) + 1,
      maxRounds: MAX_BUILD_FIX_ROUNDS,
      cwd: wt,
      signal: ctx.signal,
    });
    applyFileBlocks(fix, wt);
    ctx.data.buildFix = fix;
    ctx.data.buildFixRound = (ctx.data.buildFixRound as number || 0) + 1;
    return { ok: true, output: `Build fix applied (round ${ctx.data.buildFixRound})` };
  },

  fix_tests: async (ctx) => {
    const wt = ctx.data.worktreePath!;
    const testResults = readState("test_results.json");
    const fix = await askTestFixer({
      testOutput: testResults.slice(-3000),
      round: (ctx.data.testFixRound as number || 0) + 1,
      maxRounds: MAX_TEST_FIX_ROUNDS,
      isBuildFailure: !ctx.data.buildOk,
      cwd: wt,
      signal: ctx.signal,
    });
    applyFileBlocks(fix, wt);
    ctx.data.testFix = fix;
    ctx.data.testFixRound = (ctx.data.testFixRound as number || 0) + 1;
    return { ok: true, output: `Test fix applied (round ${ctx.data.testFixRound})` };
  },

  fix_review: async (ctx) => {
    const wt = ctx.data.worktreePath!;
    const review = readState("review.md");
    const _changedFiles = getChangedFiles(wt);
    const fix = await askBuildFixer({
      buildOutput: `Review requested changes:\n${review}`,
      round: 1,
      maxRounds: 1,
      cwd: wt,
      signal: ctx.signal,
    });
    applyFileBlocks(fix, wt);
    return { ok: true, output: "Review fix applied" };
  },

  system_audit: async (ctx) => {
    const audit = runSystemAudit();
    writeState("audit_report.md", `# System Audit\n\n${audit.summary}\n\n## Findings\n${
      audit.findings.map((f) => `- [${f.severity}] ${f.category}: ${f.message}`).join("\n")
    }\n\n## Domain Reports\n${
      audit.domainReports.map((d) => `- ${d.domain}: score=${d.complexityScore}, files=${d.fileCount}, lines=${d.totalLines}${d.exceedsThreshold ? " ⚠ OVERLOADED" : ""}`).join("\n")
    }`);
    ctx.data.auditFindings = audit.findings;
    ctx.data.domainReports = audit.domainReports;
    // F2: Scheduled state cleanup on every audit run
    pruneStateHistory(5);
    pruneMetricsLog(1000);
    cleanupStaleWorktrees();
    cleanupMergedBranches();
    return {
      ok: true,
      output: audit.summary,
    };
  },

  autofix_refs: async (ctx) => {
    const findings = ctx.data.auditFindings || [];
    const refResult = fixStaleAgentRefs(findings);
    const globResult = fixBrokenRuleGlobs(findings);
    return {
      ok: true,
      output: `Fixed ${refResult.fixed} stale agent refs, ${globResult.fixed} broken rule globs`,
    };
  },

  measure_complexity: async (ctx) => {
    const reports = measureDomainComplexity();
    const overloaded = reports.filter((r) => r.exceedsThreshold);
    ctx.data.domainReports = reports;
    ctx.data.overloadedDomains = overloaded.map((r) => r.domain);

    if (overloaded.length > 0) {
      const { writeFileSync, mkdirSync } = await import("node:fs");
      const { join } = await import("node:path");
      const stateDir = join(process.cwd(), ".pi/state");
      mkdirSync(stateDir, { recursive: true });
      writeFileSync(
        join(stateDir, "overloaded_domains.json"),
        JSON.stringify({ domains: overloaded.map((r) => r.domain), threshold: DOMAIN_COMPLEXITY_THRESHOLD }, null, 2),
        "utf-8"
      );
    }

    return {
      ok: true,
      output: `Complexity measured: ${overloaded.length} domain(s) overloaded`,
    };
  },

  trigger_decouple: async (ctx) => {
    const overloadedDomains: string[] = ctx.data.overloadedDomains || [];
    if (overloadedDomains.length === 0) {
      return { ok: true, output: "No overloaded domains to decouple" };
    }

    const domainReports: any[] = ctx.data.domainReports || [];
    const results: string[] = [];

    for (const domain of overloadedDomains) {
      const report = domainReports.find((r: any) => r.domain === domain);
      if (!report) continue;
      const recentMetrics = readRunMetrics(20).filter((m) => m.agentName.includes(domain));
      const proposal = await askDecoupleAnalyst({
        domainReport: report,
        recentMetrics,
        files: report.files,
        cwd: ctx.data.worktreePath,
      });
      results.push(`${domain}: ${proposal.slice(0, 200)}`);
    }

    return {
      ok: true,
      output: `Decouple analysis complete for ${overloadedDomains.join(", ")}`,
    };
  },

  detect_file_size_violations: async (ctx) => {
    const result = detectFileSizeViolations();
    ctx.data.sizeViolations = result.violations;
    const count = result.violations.length;
    return {
      ok: true,
      output: count > 0
        ? `Found ${count} file(s) exceeding 500 lines: ${result.violations.slice(0, 3).map((v) => `${v.path} (${v.lineCount})`).join(", ")}`
        : "No file size violations found",
    };
  },

  automodularize: async (ctx) => {
    const violations: Array<{ path: string; lineCount: number }> = ctx.data.sizeViolations || [];
    if (violations.length === 0) {
      return { ok: true, output: "No oversized files to modularize" };
    }

    const topViolation = violations[0];
    const syntheticReport = {
      domain: topViolation.path.split("/").slice(-2, -1)[0] || "unknown",
      directory: topViolation.path.replace(/\/[^/]+$/, ""),
      fileCount: violations.length,
      totalLines: violations.reduce((s, v) => s + v.lineCount, 0),
      complexityScore: violations.length + Math.floor(violations.reduce((s, v) => s + v.lineCount, 0) / 100),
      exceedsThreshold: true,
      files: violations.map((v) => v.path),
    };

    const proposal = await askDecoupleAnalyst({
      domainReport: syntheticReport,
      recentMetrics: readRunMetrics(10),
      files: syntheticReport.files,
      cwd: ctx.data.worktreePath,
    });

    applyFileBlocks(proposal, ctx.data.worktreePath!);
    return { ok: true, output: `Modularization applied for ${topViolation.path}` };
  },

  rebuild_ext: async (_ctx) => {
    const result = rebuildExtension();
    // F1: After rebuild, check for size violations and warn
    if (result.ok) {
      const violations = detectFileSizeViolations();
      if (violations.violations.length > 0) {
        console.error(
          `[size-check] Warning: ${violations.violations.length} file(s) still exceed 500 lines after rebuild: ` +
          violations.violations.map((v) => `${v.path}(${v.lineCount})`).join(", ")
        );
      }
    }
    return { ok: result.ok, output: result.summary };
  },

  cleanup_stale_state: async (_ctx) => {
    pruneStateHistory(5);
    const pruned = pruneMetricsLog(1000);
    const staleResult = cleanupStaleState(7);
    return {
      ok: true,
      output: `Pruned history, ${pruned} metric line(s), ${staleResult.deleted.length} stale state file(s)`,
    };
  },

  cleanup_stale_worktrees: async (_ctx) => {
    const result = cleanupStaleWorktrees();
    cleanupMergedBranches();
    return { ok: true, output: result.summary };
  },

  // ─── Pre-flight: maintenance checks before feature work ──────────────────

  pre_flight: async (ctx) => {
    const { existsSync } = await import("node:fs");
    const { join } = await import("node:path");

    // Bail if critical infrastructure is missing
    if (!existsSync(join(process.cwd(), "src"))) {
      return { ok: false, output: "Critical: src/ directory not found" };
    }

    const lines: string[] = ["# Pre-Flight Maintenance Report", ""];

    // 1. File size violations
    const sizeResult = detectFileSizeViolations();
    const violations = sizeResult.violations;
    if (violations.length > 0) {
      lines.push("## File Size Violations", "");
      for (const v of violations) {
        lines.push(`- \`${v.path}\` — ${v.lineCount} lines`);
      }
      lines.push("");
    }

    // 2. Stale refs audit + auto-fix
    const audit = runSystemAudit();
    const refResult = fixStaleAgentRefs(audit.findings);
    const globResult = fixBrokenRuleGlobs(audit.findings);
    if (refResult.fixed > 0 || globResult.fixed > 0) {
      lines.push("## Stale Refs Fixed", "");
      if (refResult.fixed > 0) lines.push(`- Agent refs: ${refResult.fixed} fixed`);
      if (globResult.fixed > 0) lines.push(`- Rule globs: ${globResult.fixed} fixed`);
      lines.push("");
    }

    // 3. Domain complexity
    const domainReports = measureDomainComplexity();
    const overloaded = domainReports.filter((r) => r.exceedsThreshold);
    lines.push("## Domain Complexity", "");
    for (const d of domainReports) {
      const flag = d.exceedsThreshold ? " **OVER THRESHOLD**" : "";
      lines.push(`- **${d.domain}**: score=${d.complexityScore}, files=${d.fileCount}, lines=${d.totalLines}${flag}`);
    }
    lines.push("");

    writeState("maintenance_backlog.md", lines.join("\n"));

    // Store structured data for post_flight
    ctx.data.maintenanceBacklog = {
      violations,
      overloadedDomains: overloaded,
    };

    const parts: string[] = [];
    if (violations.length > 0) parts.push(`${violations.length} size violation(s)`);
    if (refResult.fixed + globResult.fixed > 0) parts.push(`${refResult.fixed + globResult.fixed} stale ref(s) fixed`);
    if (overloaded.length > 0) parts.push(`${overloaded.length} overloaded domain(s)`);

    return {
      ok: true,
      output: parts.length > 0 ? `Pre-flight: ${parts.join(", ")}` : "Pre-flight: all clear",
    };
  },

  // ─── Post-flight: cleanup and conditional maintenance after commit ───────

  post_flight: async (ctx) => {
    const results: string[] = [];

    // 1. Branch cleanup
    cleanupMergedBranches();
    cleanupStaleWorktrees();
    results.push("Branch/worktree cleanup done");

    // 2. State pruning
    pruneStateHistory(5);
    pruneMetricsLog(1000);
    cleanupStaleState(7);
    results.push("State pruned");

    // 3. Conditional agentic work based on pre_flight backlog
    const backlog = ctx.data.maintenanceBacklog as {
      violations?: Array<{ path: string; lineCount: number }>;
      overloadedDomains?: Array<{ domain: string; directory: string; fileCount: number; totalLines: number; complexityScore: number; exceedsThreshold: boolean; files: string[] }>;
    } | undefined;

    if (backlog) {
      // 3a. Decoupling proposal for highest-scoring overloaded domain
      if (backlog.overloadedDomains && backlog.overloadedDomains.length > 0) {
        try {
          const sorted = [...backlog.overloadedDomains].sort((a, b) => b.complexityScore - a.complexityScore);
          const target = sorted[0];
          const recentMetrics = readRunMetrics(20);
          const proposal = await askDecoupleAnalyst({
            domainReport: target,
            recentMetrics,
            files: target.files,
            cwd: ctx.data.worktreePath,
          });
          writeState("decouple_proposal.md", proposal);
          results.push(`Decouple proposal written for ${target.domain}`);
        } catch (e) {
          results.push(`Decouple: skipped (${(e as Error).message})`);
        }
      }

      // 3c. Modularization proposal for file size violations
      if (backlog.violations && backlog.violations.length > 0) {
        try {
          const topViolation = backlog.violations[0];
          const syntheticReport = {
            domain: topViolation.path.split("/").slice(-2, -1)[0] || "unknown",
            directory: topViolation.path.replace(/\/[^/]+$/, ""),
            fileCount: backlog.violations.length,
            totalLines: backlog.violations.reduce((s, v) => s + v.lineCount, 0),
            complexityScore: backlog.violations.length + Math.floor(backlog.violations.reduce((s, v) => s + v.lineCount, 0) / 100),
            exceedsThreshold: true,
            files: backlog.violations.map((v) => v.path),
          };
          const proposal = await askDecoupleAnalyst({
            domainReport: syntheticReport,
            recentMetrics: readRunMetrics(10),
            files: syntheticReport.files,
            cwd: ctx.data.worktreePath,
          });
          writeState("modularize_proposal.md", proposal);
          results.push(`Modularize proposal written for ${topViolation.path}`);
        } catch (e) {
          results.push(`Modularize: skipped (${(e as Error).message})`);
        }
      }
    }

    return { ok: true, output: `Post-flight: ${results.join("; ")}` };
  },
};
