import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";

import {
  buildConfigureSchema, buildConfigure,
  buildCompileSchema, buildCompile,
  buildCleanSchema, buildClean,
  testRunSchema, testRun,
  testListSchema, testList,
} from "./tools-build.js";

import {
  terrainListPalettesSchema, terrainListPalettes,
  terrainGetConfigSchema, terrainGetConfig,
  terrainSetConfigSchema, terrainSetConfig,
  terrainPipelineInfoSchema, terrainPipelineInfo,
} from "./tools-terrain.js";

import {
  codeListSubsystemSchema, codeListSubsystem,
  codeFindDefinitionSchema, codeFindDefinition,
  codeFindUsagesSchema, codeFindUsages,
} from "./tools-code.js";

import {
  gitStatusSchema, gitStatus,
  gitDiffFromMainSchema, gitDiffFromMain,
} from "./tools-git.js";

import {
  metricsRunSchema, metricsRun,
  metricsReadDomainSchema, metricsReadDomain,
  projectOverviewSchema, projectOverview,
} from "./tools-metrics.js";

import {
  appLaunchSchema, appLaunch,
  appGetStateSchema, appGetState,
  appCaptureFrameSchema, appCaptureFrame,
  appSendInputSchema, appSendInput,
  appStopSchema, appStop,
  appCompareFramesSchema, appCompareFrames,
} from "./tools-app.js";

import {
  suggestToolsSchema, suggestTools,
  composeToolchainSchema, composeToolchain,
  toolDependenciesSchema, toolDependencies,
} from "./tools-meta.js";

// ─── Server Setup ───────────────────────────────────────────────────────────

const server = new McpServer({
  name: "delve-toolshed",
  version: "1.0.0",
});

// ─── Build Tools ────────────────────────────────────────────────────────────

server.tool("build_configure", "Run cmake configure step. Call before build_compile if CMakeLists.txt changed.", buildConfigureSchema, buildConfigure);
server.tool("build_compile", "Compile the project with cmake. Returns compiler output including any errors or warnings.", buildCompileSchema, buildCompile);
server.tool("build_clean", "Clean the build directory. Use when build state is corrupted.", buildCleanSchema, buildClean);

// ─── Test Tools ─────────────────────────────────────────────────────────────

server.tool("test_run", "Build and run the delve_tests binary. Returns JSON test results with pass/fail for each test.", testRunSchema, testRun);
server.tool("test_list", "List all registered test cases by scanning test source files.", testListSchema, testList);

// ─── Terrain Tools ──────────────────────────────────────────────────────────

server.tool("terrain_list_palettes", "List all available terrain palettes defined in palettes.h.", terrainListPalettesSchema, terrainListPalettes);
server.tool("terrain_get_config", "Read the current terrain configuration from config.json (noise params, composition, display settings).", terrainGetConfigSchema, terrainGetConfig);
server.tool("terrain_set_config", "Update terrain configuration in config.json. Provide the full JSON object.", terrainSetConfigSchema, terrainSetConfig);
server.tool("terrain_pipeline_info", "Get information about a specific terrain pipeline stage. Useful for understanding data flow.", terrainPipelineInfoSchema, terrainPipelineInfo);

// ─── Code Intelligence Tools ─────────────────────────────────────────────────

server.tool("code_list_subsystem", "List all source files in a subsystem directory.", codeListSubsystemSchema, codeListSubsystem);
server.tool("code_find_definition", "Find where a struct, class, function, or enum is defined in the codebase.", codeFindDefinitionSchema, codeFindDefinition);
server.tool("code_find_usages", "Find where a symbol is used across the codebase.", codeFindUsagesSchema, codeFindUsages);

// ─── Git Tools ──────────────────────────────────────────────────────────────

server.tool("git_status", "Get current git status: branch, staged files, modified files, untracked files.", gitStatusSchema, gitStatus);
server.tool("git_diff_from_main", "Show files changed compared to main branch.", gitDiffFromMainSchema, gitDiffFromMain);

// ─── Metrics Tools ──────────────────────────────────────────────────────────

server.tool("metrics_run", "Build and run delve_metrics to produce per-domain JSON metric files. Returns manifest with available domains.", metricsRunSchema, metricsRun);
server.tool("metrics_read_domain", "Read a specific domain's metric JSON file from the metrics output directory.", metricsReadDomainSchema, metricsReadDomain);
server.tool("project_overview", "Get a high-level overview of the Delve project structure, build targets, and key entry points.", projectOverviewSchema, projectOverview);

// ─── Agent-App Interaction Tools ─────────────────────────────────────────────

server.tool("app_launch", "Launch the Delve application in agent mode. Starts the app with an IPC socket for programmatic control.", appLaunchSchema, appLaunch);
server.tool("app_get_state", "Get the current state of the running Delve application via IPC.", appGetStateSchema, appGetState);
server.tool("app_capture_frame", "Capture the current frame from the running Delve application as a PNG file.", appCaptureFrameSchema, appCaptureFrame);
server.tool("app_send_input", "Send a synthetic keyboard input to the running Delve application.", appSendInputSchema, appSendInput);
server.tool("app_stop", "Stop the running Delve application gracefully via IPC quit command.", appStopSchema, appStop);
server.tool("app_compare_frames", "Compare two PNG frame captures and return a similarity percentage.", appCompareFramesSchema, appCompareFrames);

// ─── Meta Tools ─────────────────────────────────────────────────────────────

server.tool("suggest_tools", "Given a task description, suggest which tools are most relevant. Meta-tool: tools that select tools.", suggestToolsSchema, suggestTools);
server.tool("compose_toolchain", "Given a workflow, return an ordered sequence of tools to call with dependency info.", composeToolchainSchema, composeToolchain);
server.tool("tool_dependencies", "Get prerequisite and downstream tools for a specific tool.", toolDependenciesSchema, toolDependencies);

// ─── Start Server ───────────────────────────────────────────────────────────

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("Delve tool shed MCP server running on stdio");
}

main().catch(console.error);
