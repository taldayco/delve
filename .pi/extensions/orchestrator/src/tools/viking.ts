// ─── OpenViking Bridge ──────────────────────────────────────────────────────
//
// TypeScript interface to the Python OpenViking bridge (bridge.py).
// All calls are async (child_process.exec) to avoid blocking the event loop.
// Every public function handles failures gracefully — returns undefined/empty
// on error so callers can fall through to keyword-based fallback.

import { exec, execSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join, dirname } from "node:path";

const START_BRAIN_SCRIPT = join("/home", process.env.USER || "neto", "start_brain.sh");

// ─── Types ──────────────────────────────────────────────────────────────────

export interface VikingSearchResult {
  uri: string;
  title: string;
  snippet: string;
  score: number;
}

export interface VikingBridgeResponse {
  ok: boolean;
  result?: any;
  error?: string;
}

// ─── Constants ──────────────────────────────────────────────────────────────

const BRIDGE_DIR = join(dirname(dirname(dirname(__filename))), "viking-bridge");
const BRIDGE_SCRIPT = join(BRIDGE_DIR, "bridge.py");
const BRIDGE_TIMEOUT = 30_000; // 30s
const BRIDGE_MAX_BUFFER = 10 * 1024 * 1024; // 10MB

const VIKING_DEBUG = !!process.env.VIKING_DEBUG;

/** Maps canonical subsystem names to viking:// URIs */
export const SUBSYSTEM_VIKING_URI: Record<string, string> = {
  terrain: "viking://resources/delve/terrain",
  actor: "viking://resources/delve/actor",
  shader: "viking://resources/delve/shader",
  engine: "viking://resources/delve/engine",
  test: "viking://resources/delve/test",
  game: "viking://resources/delve/game",
};

// ─── Debug Logging ──────────────────────────────────────────────────────────

function vikingDebug(msg: string): void {
  if (VIKING_DEBUG) console.error(`[viking:debug] ${msg}`);
}

// ─── Python Resolution ──────────────────────────────────────────────────────

function getPythonBin(): string {
  if (process.env.VIKING_PYTHON) {
    vikingDebug(`using VIKING_PYTHON override: ${process.env.VIKING_PYTHON}`);
    return process.env.VIKING_PYTHON;
  }
  const venvPython = join(BRIDGE_DIR, ".venv", "bin", "python");
  if (existsSync(venvPython)) {
    vikingDebug(`using venv python: ${venvPython}`);
    return venvPython;
  }
  vikingDebug("using system python3");
  return "python3";
}

// ─── Fatal Brain Error ──────────────────────────────────────────────────────

export class FatalBrainError extends Error {
  constructor(message = "Viking/LiteLLM brain is offline — cannot proceed without semantic backend") {
    super(message);
    this.name = "FatalBrainError";
  }
}

/**
 * Hard gate: throws FatalBrainError if Viking is not available.
 * Use this in code paths that MUST have the semantic backend.
 */
export async function requireViking(): Promise<void> {
  if (!(await isVikingAvailable())) {
    throw new FatalBrainError();
  }
}

// ─── Availability Cache ─────────────────────────────────────────────────────

let _vikingAvailable: boolean | null = null;

/**
 * Cached health check: bridge script exists + OpenViking server responds.
 * Result is cached for the process lifetime — restart to re-check.
 */
export async function isVikingAvailable(): Promise<boolean> {
  if (_vikingAvailable !== null) return _vikingAvailable;

  if (!existsSync(BRIDGE_SCRIPT)) {
    vikingDebug("bridge script not found");
    console.error("[viking] Bridge script not found — using keyword fallback");
    _vikingAvailable = false;
    return false;
  }

  let resp = await callBridge({ command: "status", args: {} });
  if (!resp.ok) {
    vikingDebug(`status check failed: ${resp.error} — attempting auto-start`);
    if (existsSync(START_BRAIN_SCRIPT)) {
      console.error("[viking] OpenViking unavailable — running start_brain.sh...");
      try {
        execSync(START_BRAIN_SCRIPT, { timeout: 60_000, stdio: "pipe" });
        resp = await callBridge({ command: "status", args: {} });
      } catch (e: any) {
        vikingDebug(`start_brain.sh failed: ${e.message}`);
      }
    }
    if (!resp.ok) {
      console.error("[viking] OpenViking unavailable after auto-start — using keyword fallback");
      _vikingAvailable = false;
      return false;
    }
  }

  vikingDebug("OpenViking available");
  _vikingAvailable = true;
  return true;
}

// ─── Low-Level Bridge Call ──────────────────────────────────────────────────

/**
 * Invoke the Python bridge via async exec (stdin JSON → stdout JSON).
 * Returns { ok: false } on any failure (timeout, parse error, bridge crash).
 */
export function callBridge(request: { command: string; args: Record<string, any> }): Promise<VikingBridgeResponse> {
  const startTime = Date.now();
  vikingDebug(`calling bridge: ${request.command}`);

  return new Promise((resolve) => {
    const child = exec(`${getPythonBin()} "${BRIDGE_SCRIPT}"`, {
      timeout: BRIDGE_TIMEOUT,
      maxBuffer: BRIDGE_MAX_BUFFER,
    }, (error, stdout, stderr) => {
      const elapsed = Date.now() - startTime;
      if (error) {
        vikingDebug(`bridge error (${elapsed}ms): ${error.message}`);
        resolve({ ok: false, error: error.message });
        return;
      }
      if (stderr) vikingDebug(`bridge stderr: ${stderr}`);
      try {
        const result = JSON.parse(stdout.trim());
        vikingDebug(`bridge ${request.command} ok (${elapsed}ms)`);
        resolve(result);
      } catch (e: any) {
        vikingDebug(`bridge JSON parse failed (${elapsed}ms): ${e.message}`);
        resolve({ ok: false, error: `JSON parse: ${e.message}` });
      }
    });
    child.stdin!.write(JSON.stringify(request));
    child.stdin!.end();
  });
}

// ─── Search ─────────────────────────────────────────────────────────────────

/**
 * Semantic search across OpenViking resources.
 * @param query - Natural language search query
 * @param level - Retrieval tier: "L0" (summary), "L1" (overview), "L2" (full)
 * @param targetUri - Optional URI scope (e.g. "viking://resources/delve/terrain")
 * @param limit - Max results (default 10)
 */
export async function vikingSearch(
  query: string,
  level: "L0" | "L1" | "L2" = "L1",
  targetUri?: string,
  limit: number = 10,
): Promise<VikingSearchResult[]> {
  const resp = await callBridge({
    command: "search",
    args: { query, level, uri: targetUri, limit },
  });
  if (!resp.ok || !Array.isArray(resp.result)) {
    vikingDebug(`search returned empty for query: "${query.slice(0, 60)}"`);
    return [];
  }
  return resp.result as VikingSearchResult[];
}

// ─── Tier Reads ─────────────────────────────────────────────────────────────

/** L0 summary of a resource. */
export async function vikingAbstract(uri: string): Promise<string | undefined> {
  const resp = await callBridge({ command: "abstract", args: { uri } });
  return resp.ok ? resp.result?.content : undefined;
}

/** L1 overview of a resource (~2K tokens). */
export async function vikingOverview(uri: string): Promise<string | undefined> {
  const resp = await callBridge({ command: "overview", args: { uri } });
  return resp.ok ? resp.result?.content : undefined;
}

/** L2 full content of a resource. */
export async function vikingRead(uri: string): Promise<string | undefined> {
  const resp = await callBridge({ command: "read", args: { uri } });
  return resp.ok ? resp.result?.content : undefined;
}

// ─── Memory Session Lifecycle ───────────────────────────────────────────────

/** Create a new memory session. Returns session ID or undefined on failure. */
export async function vikingCreateSession(name?: string): Promise<string | undefined> {
  const resp = await callBridge({
    command: "create_session",
    args: { name: name || "orchestrator" },
  });
  return resp.ok ? resp.result?.session_id : undefined;
}

/** Append a message to an active session. */
export async function vikingAddMessage(
  sessionId: string,
  content: string,
  role: "user" | "assistant" = "assistant",
): Promise<boolean> {
  const resp = await callBridge({
    command: "add_message",
    args: { session_id: sessionId, role, content },
  });
  return resp.ok;
}

/** Commit session — triggers OpenViking's memory self-iteration. */
export async function vikingCommitSession(sessionId: string): Promise<boolean> {
  const resp = await callBridge({
    command: "commit_session",
    args: { session_id: sessionId },
  });
  return resp.ok;
}

// ─── Persistent Memory ─────────────────────────────────────────────────────

/**
 * Write content to a persistent Viking memory URI.
 * Uses the viking://memories/ namespace which is not overwritten by ingest.py re-indexing.
 * @param uri - Target URI (e.g. "viking://memories/delve/diagnoses/2026-03-12T...")
 * @param content - Content to persist
 */
export async function vikingWriteMemory(uri: string, content: string): Promise<boolean> {
  const resp = await callBridge({
    command: "write",
    args: { uri, content },
  });
  if (!resp.ok) {
    vikingDebug(`writeMemory failed for ${uri}: ${resp.error}`);
  }
  return resp.ok;
}

// ─── Path → URI Mapping ────────────────────────────────────────────────────

/**
 * Cached directory map loaded from .pi/viking_directory_map.json.
 * Written by ingest.py as the single source of truth for path→URI mappings.
 * Sorted longest-prefix-first so specific dirs (src/game/terrain) match before
 * broader ones (src/game).
 */
let _directoryMapCache: [string, string][] | null = null;

function loadDirectoryMap(): [string, string][] {
  if (_directoryMapCache) return _directoryMapCache;

  const { readFileSync } = require("node:fs");
  const mapPath = join(dirname(dirname(dirname(dirname(__filename)))), "viking_directory_map.json");

  try {
    const raw: Record<string, string> = JSON.parse(readFileSync(mapPath, "utf-8"));
    // Sort by key length descending so most-specific prefix wins
    _directoryMapCache = Object.entries(raw)
      .sort((a, b) => b[0].length - a[0].length)
      .map(([dir, uri]) => [
        dir.endsWith("/") ? dir : dir + "/",
        uri.endsWith("/") ? uri : uri + "/",
      ]);
    vikingDebug(`loaded directory map (${_directoryMapCache.length} entries) from ${mapPath}`);
  } catch (e: any) {
    vikingDebug(`failed to load directory map: ${e.message}`);
    _directoryMapCache = [];
  }

  return _directoryMapCache;
}

/**
 * Convert a project-relative file path to a viking:// URI.
 * Returns undefined if the path doesn't match any known directory.
 */
export function pathToVikingUri(filePath: string): string | undefined {
  // Normalize: strip leading ./ or /
  const normalized = filePath.replace(/^\.\//, "").replace(/^\//, "");
  for (const [prefix, vikingPrefix] of loadDirectoryMap()) {
    if (normalized.startsWith(prefix)) {
      return vikingPrefix + normalized.slice(prefix.length);
    }
  }
  return undefined;
}
