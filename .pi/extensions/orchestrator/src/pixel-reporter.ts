// ─── Pixel Bridge Reporter ──────────────────────────────────────────────────
// Hooks into agentEvents and POSTs agent lifecycle events to the pixel-bridge
// server running at http://localhost:7842/events.
//
// Fire-and-forget: if pixel-bridge is not running, events are silently dropped.

import { agentEvents } from "./agents/events.js";

const PIXEL_BRIDGE_URL = "http://localhost:7842/events";

function post(payload: unknown): void {
  fetch(PIXEL_BRIDGE_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  }).catch(() => {
    // pixel-bridge may not be running — ignore silently
  });
}

export function activatePixelReporter(): void {
  agentEvents.on(
    "agent:start",
    ({ name, model }: { name: string; model: string }) => {
      post({ type: "agent:start", name, model });
    },
  );

  agentEvents.on(
    "agent:end",
    ({ name, model }: { name: string; model: string }) => {
      post({ type: "agent:end", name, model });
    },
  );
}
