// ─── Domain Complexity Metrics ───────────────────────────────────────────────

import { shell } from "./shell.js";
import { PROJECT_ROOT } from "../agents/config.js";
import { DOMAIN_COMPLEXITY_THRESHOLD } from "./types.js";
import type { DomainReport } from "./types.js";

export { DOMAIN_COMPLEXITY_THRESHOLD } from "./types.js";

export const DOMAIN_DIRECTORIES: Record<string, string> = {
  terrain: "src/game/terrain",
  actor: "src/game/render",
  shader: "src/shaders",
  engine: "src/engine",
};

export function measureDomainComplexity(): DomainReport[] {
  const reports: DomainReport[] = [];

  for (const [domain, directory] of Object.entries(DOMAIN_DIRECTORIES)) {
    const fullDir = directory;

    // Count files
    const findResult = shell(
      `find ${fullDir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) 2>/dev/null`,
      PROJECT_ROOT
    );
    const files = findResult.ok
      ? findResult.stdout.trim().split("\n").filter((f) => f.length > 0)
      : [];
    const fileCount = files.length;

    // Count total lines
    let totalLines = 0;
    if (fileCount > 0) {
      const wcResult = shell(
        `find ${fullDir} -type f \\( -name '*.h' -o -name '*.cpp' -o -name '*.glsl' \\) -exec wc -l {} + 2>/dev/null | tail -1`,
        PROJECT_ROOT
      );
      if (wcResult.ok) {
        const match = wcResult.stdout.trim().match(/^\s*(\d+)/);
        if (match) totalLines = parseInt(match[1], 10);
      }
    }

    const complexityScore = fileCount + Math.floor(totalLines / 100);

    reports.push({
      domain,
      directory,
      fileCount,
      totalLines,
      complexityScore,
      exceedsThreshold: complexityScore > DOMAIN_COMPLEXITY_THRESHOLD,
      files,
    });
  }

  return reports;
}
