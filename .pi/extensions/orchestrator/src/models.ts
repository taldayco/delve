import Anthropic from "@anthropic-ai/sdk";
import { readFileSync, existsSync } from "node:fs";
import { join } from "node:path";

// ─── Model Tier Routing ────────────────────────────────────────────────────

export type ModelTier = "opus" | "sonnet" | "haiku";

const MODEL_MAP: Record<ModelTier, string> = {
  opus: "claude-opus-4-6",
  sonnet: "claude-sonnet-4-6",
  haiku: "claude-haiku-4-5-20251001",
};

// ─── API Key Resolution ────────────────────────────────────────────────────

/**
 * Resolve Anthropic credentials from pi's auth storage.
 * Priority:
 *   1. ANTHROPIC_API_KEY environment variable (regular API key)
 *   2. OAuth access token from pi's auth.json
 */
interface AuthCredentials {
  type: "apiKey" | "oauth";
  key: string;
}

function resolveCredentials(): AuthCredentials {
  // 1. Environment variable (standard Anthropic SDK flow)
  if (process.env.ANTHROPIC_API_KEY) {
    return { type: "apiKey", key: process.env.ANTHROPIC_API_KEY };
  }

  // 2. Pi's auth storage (OAuth access token)
  const authPath = join(
    process.env.HOME || process.env.USERPROFILE || "~",
    ".pi",
    "agent",
    "auth.json"
  );

  if (existsSync(authPath)) {
    try {
      const auth = JSON.parse(readFileSync(authPath, "utf-8"));
      if (auth.anthropic?.access) {
        return { type: "oauth", key: auth.anthropic.access };
      }
      if (auth.anthropic?.apiKey) {
        return { type: "apiKey", key: auth.anthropic.apiKey };
      }
    } catch {
      // Fall through
    }
  }

  throw new Error(
    "Could not resolve Anthropic API key. Set ANTHROPIC_API_KEY env var or log in via `pi /login`."
  );
}

function isOAuthToken(key: string): boolean {
  return key.includes("sk-ant-oat");
}

// Lazy-init client
let client: Anthropic | null = null;

function getClient(): Anthropic {
  if (!client) {
    const creds = resolveCredentials();

    if (isOAuthToken(creds.key)) {
      // OAuth tokens use authToken (sends Authorization: Bearer header)
      // and require specific beta/identity headers
      client = new Anthropic({
        authToken: creds.key,
        defaultHeaders: {
          "anthropic-beta": "oauth-2025-04-20",
        },
      });
    } else {
      // Regular API keys use apiKey (sends x-api-key header)
      client = new Anthropic({ apiKey: creds.key });
    }
  }
  return client;
}

/**
 * Reset the client (e.g., if the token expired and was refreshed).
 */
export function resetClient(): void {
  client = null;
}

// ─── Stateless Sub-Agent Call ──────────────────────────────────────────────

export interface AskOptions {
  system: string;
  user: string;
  tier: ModelTier;
  maxTokens?: number;
}

/**
 * Stateless model call — the core primitive for agent-as-tool pattern.
 * Each call starts fresh with no memory of prior calls.
 * Only the returned text enters the orchestrator's context.
 *
 * On auth failure, resets the client and retries once (handles token refresh).
 */
export async function ask(opts: AskOptions): Promise<string> {
  try {
    return await doAsk(opts);
  } catch (e: any) {
    // If auth error, reset client and retry once (token may have been refreshed)
    if (e?.status === 401 || e?.message?.includes("authentication")) {
      resetClient();
      return doAsk(opts);
    }
    throw e;
  }
}

async function doAsk(opts: AskOptions): Promise<string> {
  const response = await getClient().messages.create({
    model: MODEL_MAP[opts.tier],
    max_tokens: opts.maxTokens ?? 4096,
    system: opts.system,
    messages: [{ role: "user", content: opts.user }],
  });

  return response.content
    .filter((b): b is Anthropic.TextBlock => b.type === "text")
    .map((b) => b.text)
    .join("\n");
}

/**
 * Silent shell execution helper — returns truncated output for token efficiency.
 * Full output is captured but only the tail enters agent context.
 */
export function silentShell(
  cmd: string,
  tailLines = 20,
  cwd?: string
): { ok: boolean; summary: string; full: string } {
  const { execSync } = require("node:child_process");
  try {
    const stdout: string = execSync(cmd, {
      cwd: cwd || process.cwd(),
      encoding: "utf-8",
      timeout: 300_000,
      maxBuffer: 10 * 1024 * 1024,
      stdio: ["pipe", "pipe", "pipe"],
    });
    const lines = (stdout || "").split("\n");
    return {
      ok: true,
      summary: lines.slice(-tailLines).join("\n"),
      full: stdout || "",
    };
  } catch (e: any) {
    const output = ((e.stdout || "") + "\n" + (e.stderr || e.message || "")).toString();
    const lines = output.split("\n");
    return {
      ok: false,
      summary: lines.slice(-tailLines).join("\n"),
      full: output,
    };
  }
}
