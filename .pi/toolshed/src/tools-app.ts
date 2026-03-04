import { z } from "zod";
import { existsSync, unlinkSync } from "node:fs";
import { join } from "node:path";
import { spawn } from "node:child_process";
import { createConnection, type Socket } from "node:net";
import { compareFrames } from "./frame_diff.js";
import { PROJECT_ROOT } from "./shell.js";

// ─── App Process State ───────────────────────────────────────────────────────

export let appProcess: ReturnType<typeof spawn> | null = null;
export let appSocketPath = "/tmp/delve-agent.sock";

// ─── sendAgentCommand ────────────────────────────────────────────────────────

export function sendAgentCommand(
  cmd: string,
  params: Record<string, any> = {}
): Promise<{ ok: boolean; data?: any; error?: string }> {
  return new Promise((resolve, reject) => {
    const socket: Socket = createConnection({ path: appSocketPath }, () => {
      const msg = JSON.stringify({ cmd, params }) + "\n";
      socket.write(msg);
    });

    let buffer = "";
    socket.on("data", (data) => {
      buffer += data.toString();
      const newline = buffer.indexOf("\n");
      if (newline >= 0) {
        const line = buffer.substring(0, newline);
        socket.end();
        try {
          resolve(JSON.parse(line));
        } catch {
          resolve({ ok: false, error: "Failed to parse response" });
        }
      }
    });

    socket.on("error", (err) => {
      resolve({ ok: false, error: err.message });
    });

    socket.setTimeout(10000, () => {
      socket.end();
      resolve({ ok: false, error: "Connection timed out" });
    });
  });
}

// ─── app_launch ──────────────────────────────────────────────────────────────

export const appLaunchSchema = {
  socket_path: z
    .string()
    .default("/tmp/delve-agent.sock")
    .describe("Unix socket path for IPC"),
  extra_args: z
    .array(z.string())
    .default([])
    .describe("Extra command-line arguments"),
};

export async function appLaunch({ socket_path, extra_args }: { socket_path: string; extra_args: string[] }) {
  if (appProcess) {
    return {
      content: [
        {
          type: "text" as const,
          text: JSON.stringify({ success: false, error: "App already running" }),
        },
      ],
    };
  }

  appSocketPath = socket_path;

  // Clean up stale socket
  try { unlinkSync(socket_path); } catch { /* ignore */ }

  const args = ["--agent-mode", "--agent-socket", socket_path, ...extra_args];
  appProcess = spawn(join(PROJECT_ROOT, "build/topogen"), args, {
    cwd: PROJECT_ROOT,
    stdio: ["ignore", "pipe", "pipe"],
    detached: false,
  });

  appProcess.on("exit", () => {
    appProcess = null;
  });

  // Wait for socket to appear (up to 10s)
  const startTime = Date.now();
  while (Date.now() - startTime < 10000) {
    if (existsSync(socket_path)) break;
    await new Promise((r) => setTimeout(r, 200));
  }

  const launched = existsSync(socket_path);
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify({
          success: launched,
          pid: appProcess?.pid,
          socket_path,
        }),
      },
    ],
  };
}

// ─── app_get_state ───────────────────────────────────────────────────────────

export const appGetStateSchema = {};

export async function appGetState(_args: Record<string, never>) {
  const result = await sendAgentCommand("get_state");
  return {
    content: [
      { type: "text" as const, text: JSON.stringify(result) },
    ],
  };
}

// ─── app_capture_frame ───────────────────────────────────────────────────────

export const appCaptureFrameSchema = {
  path: z
    .string()
    .default("/tmp/delve-capture.png")
    .describe("Output path for the PNG file"),
};

export async function appCaptureFrame({ path }: { path: string }) {
  const result = await sendAgentCommand("capture_frame", { path });
  return {
    content: [
      { type: "text" as const, text: JSON.stringify(result) },
    ],
  };
}

// ─── app_send_input ──────────────────────────────────────────────────────────

export const appSendInputSchema = {
  key: z
    .string()
    .describe("Key name (e.g., 'Space', 'W', 'A', 'S', 'D', 'Escape')"),
};

export async function appSendInput({ key }: { key: string }) {
  const result = await sendAgentCommand("send_input", { key });
  return {
    content: [
      { type: "text" as const, text: JSON.stringify(result) },
    ],
  };
}

// ─── app_stop ────────────────────────────────────────────────────────────────

export const appStopSchema = {};

export async function appStop(_args: Record<string, never>) {
  const result = await sendAgentCommand("quit");
  // Wait a moment for process to exit
  await new Promise((r) => setTimeout(r, 500));
  if (appProcess && !appProcess.killed) {
    appProcess.kill("SIGTERM");
  }
  appProcess = null;
  return {
    content: [
      { type: "text" as const, text: JSON.stringify(result) },
    ],
  };
}

// ─── app_compare_frames ──────────────────────────────────────────────────────

export const appCompareFramesSchema = {
  path_a: z.string().describe("Path to the first PNG file"),
  path_b: z.string().describe("Path to the second PNG file"),
};

export async function appCompareFrames({ path_a, path_b }: { path_a: string; path_b: string }) {
  try {
    const result = compareFrames(path_a, path_b);
    return {
      content: [
        { type: "text" as const, text: JSON.stringify({ success: true, ...result }) },
      ],
    };
  } catch (e: any) {
    return {
      content: [
        { type: "text" as const, text: JSON.stringify({ success: false, error: e.message }) },
      ],
    };
  }
}
