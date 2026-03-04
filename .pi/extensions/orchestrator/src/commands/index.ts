import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { registerMinionCommand } from "./minion.js";
import { registerMinionVariantCommands } from "./minion-variants.js";
import { registerSubsystemCommands } from "./minion-subsystem.js";
import { registerSystemCommands } from "./system.js";

export { registerTools } from "./tools.js";

export function registerCommands(pi: ExtensionAPI) {
  registerMinionCommand(pi);
  registerMinionVariantCommands(pi);
  registerSubsystemCommands(pi);
  registerSystemCommands(pi);

  // ── Safety: block dangerous operations ──────────────────────────────────
  pi.on("tool_call", async (event, _ctx) => {
    if (event.toolName === "bash") {
      const cmd = (event.input as any)?.command || "";
      const dangerous =
        cmd.includes("rm -rf /") ||
        cmd.includes("git push --force") ||
        cmd.includes("git push -f") ||
        (cmd.includes("git reset --hard") && cmd.includes("main"));
      if (dangerous) {
        return { block: true, reason: "Blocked dangerous operation: " + cmd };
      }
    }
    return undefined;
  });
}
