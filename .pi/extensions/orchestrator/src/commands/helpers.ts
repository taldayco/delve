// ─── Shared pipeline helpers ──────────────────────────────────────────────────

export const MAX_REVIEW_ROUNDS = 2;

/**
 * Extract file paths from markdown text.
 * Matches any directory/file.ext pattern.
 */
export function extractFilePaths(text: string): string[] {
  const regex = /\b([a-zA-Z0-9_.][a-zA-Z0-9_./+-]*\/[a-zA-Z0-9_./+-]*\.[a-zA-Z]{1,10})\b/g;
  const paths = new Set<string>();
  let match;
  while ((match = regex.exec(text)) !== null) {
    const p = match[1];
    if (p.includes("://") || p.includes("..")) continue;
    paths.add(p);
  }
  if (text.includes("CMakeLists.txt")) {
    paths.add("CMakeLists.txt");
  }
  return Array.from(paths);
}

/**
 * Parse a review response into APPROVE or REQUEST_CHANGES.
 * Binary — no middle ground.
 */
export function parseReviewDecision(review: string): "APPROVE" | "REQUEST_CHANGES" {
  // 1. Structured header: "## Review: APPROVE"
  const headerMatch = review.match(/##\s*Review:\s*(APPROVE|REQUEST_CHANGES)/i);
  if (headerMatch) {
    return headerMatch[1].toUpperCase() as "APPROVE" | "REQUEST_CHANGES";
  }

  // 2. Negation patterns → NOT approved
  if (/\b(cannot|can't|do not|don't|unable to|not)\s+approve\b/i.test(review)) {
    return "REQUEST_CHANGES";
  }

  // 3. Explicit REQUEST_CHANGES
  if (/\bREQUEST_CHANGES\b/.test(review)) {
    return "REQUEST_CHANGES";
  }

  // 4. Word-boundary APPROVE
  if (/\bAPPROVE\b/.test(review)) {
    return "APPROVE";
  }

  // Default: changes requested
  return "REQUEST_CHANGES";
}
