import type { ExtensionAPI } from "@mariozechner/pi-coding-agent";
import { registerCommands, registerTools } from "./commands/index.js";

export default function (pi: ExtensionAPI) {
  registerCommands(pi);
  registerTools(pi);
}
