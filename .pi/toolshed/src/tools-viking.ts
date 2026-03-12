import { exec } from "node:child_process";
import { existsSync } from "node:fs";
import { join, dirname, resolve } from "node:path";
import { z } from "zod";

// ─── Local Bridge Shim ─────────────────────────────────────────────────────
// Duplicates the invocation pattern from orchestrator/viking.ts because
// the toolshed is a separate npm package with no dependency on the orchestrator.
// The Python bridge script on disk is shared.

const BRIDGE_DIR = resolve(
  dirname(dirname(__dirname)),
  "extensions",
  "orchestrator",
  "viking-bridge",
);
const BRIDGE_SCRIPT = join(BRIDGE_DIR, "bridge.py");
const BRIDGE_TIMEOUT = 30_000;
const BRIDGE_MAX_BUFFER = 10 * 1024 * 1024;

function getPythonBin(): string {
  if (process.env.VIKING_PYTHON) return process.env.VIKING_PYTHON;
  const venvPython = join(BRIDGE_DIR, ".venv", "bin", "python");
  if (existsSync(venvPython)) return venvPython;
  return "python3";
}

function callBridge(request: { command: string; args: Record<string, any> }): Promise<{ ok: boolean; result?: any; error?: string }> {
  return new Promise((resolve) => {
    if (!existsSync(BRIDGE_SCRIPT)) {
      resolve({ ok: false, error: "Bridge script not found" });
      return;
    }
    const child = exec(`${getPythonBin()} "${BRIDGE_SCRIPT}"`, {
      timeout: BRIDGE_TIMEOUT,
      maxBuffer: BRIDGE_MAX_BUFFER,
    }, (_error, stdout, _stderr) => {
      if (_error) {
        resolve({ ok: false, error: _error.message });
        return;
      }
      try {
        resolve(JSON.parse(stdout.trim()));
      } catch (e: any) {
        resolve({ ok: false, error: `JSON parse: ${e.message}` });
      }
    });
    child.stdin!.write(JSON.stringify(request));
    child.stdin!.end();
  });
}

// ─── viking_search ──────────────────────────────────────────────────────────

export const vikingSearchSchema = {
  query: z.string().describe("Natural language search query"),
  level: z.enum(["L0", "L1", "L2"]).default("L1").describe("Retrieval tier: L0 (summary), L1 (overview), L2 (full)"),
  uri: z.string().optional().describe("Optional URI scope (e.g. viking://resources/delve/terrain)"),
  limit: z.number().default(10).describe("Max results"),
} as const;

export async function vikingSearch(args: { query: string; level?: string; uri?: string; limit?: number }) {
  const resp = await callBridge({
    command: "search",
    args: { query: args.query, level: args.level || "L1", uri: args.uri, limit: args.limit || 10 },
  });
  return {
    content: [{
      type: "text" as const,
      text: resp.ok ? JSON.stringify(resp.result, null, 2) : JSON.stringify({ error: resp.error }),
    }],
  };
}

// ─── viking_read ────────────────────────────────────────────────────────────

export const vikingReadSchema = {
  uri: z.string().describe("Viking URI to read (e.g. viking://resources/delve/terrain/noise.cpp)"),
} as const;

export async function vikingRead(args: { uri: string }) {
  const resp = await callBridge({ command: "read", args: { uri: args.uri } });
  return {
    content: [{
      type: "text" as const,
      text: resp.ok ? (resp.result?.content || "[empty]") : JSON.stringify({ error: resp.error }),
    }],
  };
}

// ─── viking_write_memory ────────────────────────────────────────────────────

export const vikingWriteMemorySchema = {
  uri: z.string().describe("Target memory URI (e.g. viking://memories/delve/notes/my-note)"),
  content: z.string().describe("Content to persist"),
} as const;

export async function vikingWriteMemory(args: { uri: string; content: string }) {
  const resp = await callBridge({ command: "write", args: { uri: args.uri, content: args.content } });
  return {
    content: [{
      type: "text" as const,
      text: resp.ok ? "Written successfully" : JSON.stringify({ error: resp.error }),
    }],
  };
}
