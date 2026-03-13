// ─── Filesystem Tools ────────────────────────────────────────────────────────

import { resolve } from "node:path";
import { shell } from "./shell.js";

/**
 * Parse ### FILE: blocks from agent output and write files directly.
 * Uses a line-by-line parser instead of regex to correctly handle
 * inner triple-backtick code blocks within file content.
 * Returns the number of files written.
 */
export function applyFileBlocks(text: string, targetDir: string): number {
  const { writeFileSync, mkdirSync } = require("node:fs");
  const { dirname, join } = require("node:path");
  const baseCwd = targetDir;

  const lines = text.split("\n");
  let count = 0;
  let currentFile: string | null = null;
  let contentLines: string[] = [];
  let inFence = false;
  let fenceLength = 0; // number of backticks that opened the fence

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];

    // Detect ### FILE: header
    const fileMatch = line.match(/^###\s*FILE:\s*(\S+)/);
    if (fileMatch && !inFence) {
      // Flush previous file if any
      if (currentFile && contentLines.length > 0) {
        writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
        count++;
      }
      currentFile = fileMatch[1];
      contentLines = [];
      inFence = false;
      fenceLength = 0;
      continue;
    }

    // Skip #### ACTION: lines when not inside a fence
    if (!inFence && currentFile && /^####\s*ACTION:/i.test(line)) {
      continue;
    }

    // Detect opening fence (only when we have a currentFile and not already in a fence)
    if (!inFence && currentFile) {
      const openMatch = line.match(/^(`{3,})\w*\s*$/);
      if (openMatch) {
        inFence = true;
        fenceLength = openMatch[1].length;
        contentLines = [];
        continue;
      }
    }

    // Detect closing fence: line is solely backticks of equal or greater length
    if (inFence && currentFile) {
      const closeMatch = line.match(/^(`{3,})\s*$/);
      if (closeMatch && closeMatch[1].length >= fenceLength) {
        // Write the file
        writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
        count++;
        currentFile = null;
        contentLines = [];
        inFence = false;
        fenceLength = 0;
        continue;
      }
    }

    // Accumulate content lines inside fence
    if (inFence && currentFile) {
      contentLines.push(line);
    }
  }

  // Flush any unclosed file (shouldn't happen with well-formed output, but be safe)
  if (currentFile && contentLines.length > 0 && inFence) {
    writeFileBlock(currentFile, contentLines.join("\n"), baseCwd, writeFileSync, mkdirSync, dirname, join);
    count++;
  }

  return count;
}

function writeFileBlock(
  filePath: string,
  content: string,
  cwd: string,
  writeFileSync: Function,
  mkdirSync: Function,
  dirname: Function,
  join: Function,
): void {
  const rawPath = filePath.startsWith("/") ? filePath : join(cwd, filePath);
  const fullPath = resolve(rawPath);
  const resolvedCwd = resolve(cwd);
  if (!fullPath.startsWith(resolvedCwd + "/") && fullPath !== resolvedCwd) {
    throw new Error(`Path traversal blocked: ${filePath} resolved to ${fullPath} (outside ${resolvedCwd})`);
  }
  try {
    mkdirSync(dirname(fullPath), { recursive: true });
    writeFileSync(fullPath, content, "utf-8");
  } catch (e: any) {
    console.error(`Failed to write ${fullPath}: ${e.message}`);
  }
}

export function rebuildExtension(): { ok: boolean; summary: string } {
  const extDir = require("node:path").join(process.cwd(), ".pi/extensions/orchestrator");
  const result = shell("npm run build 2>&1", extDir);
  return {
    ok: result.ok,
    summary: result.ok
      ? "Extension rebuilt successfully"
      : `Extension build failed: ${result.stderr || result.stdout}`,
  };
}
