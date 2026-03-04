// ─── Pixel Bridge Auto-Launcher ─────────────────────────────────────────────
// Checks if pixel-bridge is running and starts it if not.
// Called once at orchestrator startup.

import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";
import { PROJECT_ROOT } from "./agents/config.js";

const PIXEL_BRIDGE_URL = "http://localhost:7842/";
const PIXEL_BRIDGE_SERVER = join(
  "/home/neto/Projects/pixel-bridge/server/dist/server.js",
);

async function isPixelBridgeRunning(): Promise<boolean> {
  try {
    const res = await fetch(PIXEL_BRIDGE_URL, { signal: AbortSignal.timeout(1000) });
    return res.ok;
  } catch {
    return false;
  }
}

export async function ensurePixelBridgeRunning(): Promise<void> {
  if (!existsSync(PIXEL_BRIDGE_SERVER)) {
    console.log("[pixel-bridge] Server not found at", PIXEL_BRIDGE_SERVER, "— skipping auto-start");
    return;
  }

  if (await isPixelBridgeRunning()) {
    console.log("[pixel-bridge] Already running at http://localhost:7842");
    return;
  }

  console.log("[pixel-bridge] Starting pixel-bridge server...");

  const child = spawn("node", [PIXEL_BRIDGE_SERVER, "--cwd", PROJECT_ROOT], {
    detached: true,
    stdio: "ignore",
  });
  child.unref();

  // Wait briefly for startup then confirm
  await new Promise<void>((resolve) => setTimeout(resolve, 1500));

  if (await isPixelBridgeRunning()) {
    console.log("[pixel-bridge] Started — open http://localhost:7842 to view agents");
  } else {
    console.log("[pixel-bridge] Failed to start (check server logs)");
  }
}
