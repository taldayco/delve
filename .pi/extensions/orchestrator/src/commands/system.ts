import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  elapsed,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
  isVikingAvailable,
} from "../tools.js";
import { state } from "./display.js";

export function registerSystemCommands(pi: ExtensionAPI) {
  // ── /minion-status command ──────────────────────────────────────────────

  pi.registerCommand("minion-status", {
    description: "Show current minion run status",
    handler: async (_args, ctx) => {
      if (!state) {
        ctx.ui.notify("No minion run in progress", "info");
        return;
      }
      ctx.ui.notify(
        `Phase: ${state.phase} | Build rounds: ${state.buildFixRound}/${MAX_BUILD_FIX_ROUNDS} | Test rounds: ${state.testFixRound}/${MAX_TEST_FIX_ROUNDS} | Elapsed: ${elapsed(state.startTime)}`,
        "info"
      );
    },
  });

  // ── /doctor command ─────────────────────────────────────────────────────

  pi.registerCommand("doctor", {
    description: "Health check: LiteLLM, OpenViking, and API key status",
    handler: async (_args, ctx) => {
      const results: string[] = [];

      // 1. LiteLLM (port 4000)
      try {
        const { execSync } = require("node:child_process");
        execSync("curl -sf http://localhost:4000/health --max-time 3", { stdio: "pipe" });
        results.push("- **LiteLLM** (port 4000): online");
      } catch {
        results.push("- **LiteLLM** (port 4000): OFFLINE");
      }

      // 2. OpenViking (port 1933)
      const vikingOk = await isVikingAvailable();
      results.push(`- **OpenViking** (port 1933): ${vikingOk ? "online" : "OFFLINE"}`);

      // 3. ANTHROPIC_API_KEY
      const hasKey = !!process.env.ANTHROPIC_API_KEY;
      results.push(`- **ANTHROPIC_API_KEY**: ${hasKey ? "set" : "MISSING"}`);

      const allOk = results.every((r) => !r.includes("OFFLINE") && !r.includes("MISSING"));
      ctx.ui.notify(
        `## Doctor Report\n\n${results.join("\n")}`,
        allOk ? "success" : "error",
      );
    },
  });
}
