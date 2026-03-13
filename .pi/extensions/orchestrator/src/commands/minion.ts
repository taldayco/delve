import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { guardArgs, runBlueprint } from "./minion-variants.js";

export function registerMinionCommand(pi: ExtensionAPI) {
  pi.registerCommand("minion", {
    description:
      "Run the meta-agentic development pipeline: branch → plan → implement → build → test → review → PR",
    handler: async (args, ctx) => {
      const g = guardArgs("minion", args, ctx);
      if (!g) return;
      await runBlueprint("Minion", "minion", "branching-full", g.prompt, ctx, true);
    },
  });
}
