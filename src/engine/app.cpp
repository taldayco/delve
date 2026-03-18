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

  agent_server->register_command("get_state", [this](const std::string &) -> std::string {
    return "{\"ok\":true,\"data\":{\"running\":" +
           std::string(running ? "true" : "false") + "}}\n";
  });

  agent_server->register_command("quit", [this](const std::string &) -> std::string {
    running = false;
    return "{\"ok\":true,\"data\":{\"message\":\"shutdown requested\"}}\n";
  });

  agent_server->register_command("capture_frame", [this](const std::string &params) -> std::string {
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

  agent_server->register_command("send_input", [this](const std::string &params) -> std::string {
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

    SDL_Scancode scancode = SDL_GetScancodeFromName(key_name.c_str());
    if (scancode == SDL_SCANCODE_UNKNOWN) {
      return "{\"ok\":false,\"error\":\"unknown key: " + key_name + "\"}\n";
    }

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

  SDL_Log("Entering main loop");
  uint64_t freq = SDL_GetPerformanceFrequency();
  uint64_t prev_time = SDL_GetPerformanceCounter();
  float accumulator = 0.0f;

  while (running) {
    if (g_emergency_shutdown.load(std::memory_order_relaxed)) {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                      "Emergency shutdown flag detected — exiting main loop");
      break;
    }

    if (agent_server) {
      agent_server->poll();
    }

    uint64_t current_time = SDL_GetPerformanceCounter();
    float frame_time = (float)(current_time - prev_time) / (float)freq;
    prev_time = current_time;

    if (frame_time > MAX_FRAME_TIME)
      frame_time = MAX_FRAME_TIME;

    accumulator += frame_time;


    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ui_process_event(event);

      if (event.type == SDL_EVENT_QUIT)
        running = false;

      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        SDL_WindowID tool_id = SDL_GetWindowID(gpu_ctx.window);
        if (event.window.windowID == tool_id) {
          running = false;
        } else {
          gpu_destroy_game_window(gpu_ctx);
        }
      }

      on_event(event, ecs_world);
    }


    if (wants_game_window_open(ecs_world)) {
      gpu_create_game_window(gpu_ctx);
    }
    if (wants_game_window_close(ecs_world)) {
      gpu_destroy_game_window(gpu_ctx);
    }


    while (accumulator >= FIXED_DT) {
      ecs_world.progress(FIXED_DT);
      accumulator -= FIXED_DT;
    }


    asset_manager.check_for_updates();

    FrameContext tool_frame;
    if (gpu_acquire_frame(gpu_ctx, tool_frame)) {
      on_render_tool(gpu_ctx, tool_frame, ecs_world);
      gpu_end_frame(tool_frame);
    }


    if (gpu_ctx.game_window) {
      on_pre_frame_game(gpu_ctx, ecs_world);
      FrameContext game_frame;
      if (gpu_acquire_game_frame(gpu_ctx, game_frame)) {
        on_render_game(gpu_ctx, game_frame, ecs_world);
        gpu_end_frame(game_frame);
      }
    }
  }

  stop_watchdog();
  on_cleanup(ecs_world);
  asset_manager.clear();
  ui_shutdown();
  gpu_cleanup(gpu_ctx);
  return 0;
}
