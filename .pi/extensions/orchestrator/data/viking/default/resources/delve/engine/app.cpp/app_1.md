#include "app.h"
#include "core/watchdog.h"
#include "ipc/agent_server.h"
#include "gpu/frame_capture.h"

static constexpr float FIXED_DT = 1.0f / 60.0f;
static constexpr float MAX_FRAME_TIME = 0.25f;

int Application::run() {
  AgentModeConfig no_agent;
  return run(no_agent);
}

int Application::run(const AgentModeConfig &agent_config) {
  if (agent_config.enabled) {
    agent_server = std::make_unique<AgentServer>(agent_config.socket_path);
    setup_agent_commands();
    if (!agent_server->start()) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to start agent server");
      // Continue without agent mode
      agent_server.reset();
    }
  }

  int result = run_internal();

  if (agent_server) {
    agent_server->stop();
    agent_server.reset();
  }

  return result;
}

void Application::setup_agent_commands() {
  if (!agent_server) return;

  // get_state: return current frame count and camera/terrain info
  agent_server->register_command("get_state", [this](const std::string &) -> std::string {
    return "{\"ok\":true,\"data\":{\"running\":" +
           std::string(running ? "true" : "false") + "}}\n";
  });

  // quit: request application shutdown
  agent_server->register_command("quit", [this](const std::string &) -> std::string {
    running = false;
    return "{\"ok\":true,\"data\":{\"message\":\"shutdown requested\"}}\n";
  });

  // capture_frame: save current frame to PNG
  agent_server->register_command("capture_frame", [this](const std::string &params) -> std::string {
    // Extract path from params JSON: {"path":"/tmp/test.png"}
    std::string path = "/tmp/delve-capture.png";
    auto path_pos = params.find("\"path\"");
    if (path_pos != std::string::npos) {
      auto colon = params.find(':', path_pos + 6);
      auto q1 = params.find('"', colon + 1);
      auto q2 = params.find('"', q1 + 1);
      if (q1 != std::string::npos && q2 != std::string::npos) {
        path = params.substr(q1 + 1, q2 - q1 - 1);
      }
    }

    SDL_Window *target = gpu_ctx.game_window ? gpu_ctx.game_window : gpu_ctx.window;
    bool ok = capture_frame_to_png(gpu_ctx.device, target, path);
    if (ok) {
      return "{\"ok\":true,\"data\":{\"path\":\"" + path + "\"}}\n";
    } else {
      return "{\"ok\":false,\"error\":\"frame capture failed\"}\n";
    }
  });

  // send_input: inject synthetic keyboard event
  agent_server->register_command("send_input", [this](const std::string &params) -> std::string {
    // Extract key from params: {"key":"space"} or {"key":"w"}
    auto key_pos = params.find("\"key\"");
    if (key_pos == std::string::npos) {
      return "{\"ok\":false,\"error\":\"missing key param\"}\n";
    }

    auto colon = params.find(':', key_pos + 5);
    auto q1 = params.find('"', colon + 1);
    auto q2 = params.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) {
      return "{\"ok\":false,\"error\":\"malformed key param\"}\n";
    }
    std::string key_name = params.substr(q1 + 1, q2 - q1 - 1);

    // Map key name to SDL scancode
    SDL_Scancode scancode = SDL_GetScancodeFromName(key_name.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
      return "{\"ok\":false,\"error\":\"unknown key: " + key_name + "\"}\n";
    }

    // Push synthetic key down + key up events
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.scancode = scancode;
    down.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    down.key.down = true;
    down.key.repeat = false;
    SDL_PushEvent(&down);

    SDL_Event up{};
    up.type = SDL_EVENT_KEY_UP;
    up.key.scancode = scancode;
    up.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    up.key.down = false;
    up.key.repeat = false;
    SDL_PushEvent(&up);

    return "{\"ok\":true,\"data\":{\"key\":\"" + key_name + "\"}}\n";
  });
}

int Application::run_internal() {
  SDL_Log("Application starting...");

  start_watchdog();

  if (!gpu_init(gpu_ctx))
    return 1;

  asset_manager.init(gpu_ctx.device);
  ui_init(gpu_ctx.window, gpu_ctx.device);

  on_init(gpu_ctx, ecs_world);