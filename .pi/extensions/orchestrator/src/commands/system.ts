import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import {
  elapsed,
  MAX_BUILD_FIX_ROUNDS,
  MAX_TEST_FIX_ROUNDS,
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
}
