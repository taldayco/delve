#pragma once
#include "gpu/gpu.h"
#include "ui/imgui_ui.h"
#include "core/asset_manager.h"
#include "agent_mode.h"
#include "ipc/agent_server.h"
#include <SDL3/SDL.h>
#include <flecs.h>
#include <memory>

class Application {
public:
  virtual ~Application() = default;

  int run();
  int run(const AgentModeConfig &agent_config);


  virtual void on_init(GpuContext &gpu, flecs::world &ecs) = 0;
  virtual void on_event(const SDL_Event &event, flecs::world &ecs) = 0;
  virtual void on_render_tool(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) = 0;
  virtual void on_render_game(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) = 0;
  virtual void on_pre_frame_game(GpuContext &gpu, flecs::world &ecs) {}
  virtual void on_cleanup(flecs::world &ecs) = 0;


  virtual bool wants_game_window_open(flecs::world &ecs) { return false; }
  virtual bool wants_game_window_close(flecs::world &ecs) { return false; }

  GpuContext &gpu() { return gpu_ctx; }
  void request_quit() { running = false; }

protected:
  GpuContext gpu_ctx;
  flecs::world ecs_world;
  AssetManager asset_manager;
  bool running = true;

private:
  std::unique_ptr<AgentServer> agent_server;
  void setup_agent_commands();
  int run_internal();
};
