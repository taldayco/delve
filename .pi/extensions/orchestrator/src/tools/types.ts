// ─── Shared Types and Constants ─────────────────────────────────────────────

export const MAX_BUILD_FIX_ROUNDS = 3;
export const MAX_TEST_FIX_ROUNDS = 3;
export const DOMAIN_COMPLEXITY_THRESHOLD = 60;

export interface DomainReport {
  domain: string;
  directory: string;
  fileCount: number;
  totalLines: number;
  complexityScore: number;
  exceedsThreshold: boolean;
  files: string[];
}

export interface AuditFinding {
  category: "stale-agent" | "broken-rule-glob" | "unmapped-directory" | "domain-overload" | "orphaned-state";
  severity: "warning" | "error";
  message: string;
}

export interface AuditReport {
  findings: AuditFinding[];
  domainReports: DomainReport[];
  summary: string;
}

export interface FileSizeViolation {
  path: string;
  lineCount: number;
}
