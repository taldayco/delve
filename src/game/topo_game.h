#pragma once
#include "app.h"
#include "game_state.h"
#include "rig.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/map_data.h"
#include "terrain/terrain_renderer.h"
#include "terrain/terrain_mesh.h"
#include "terrain/instanced_terrain.h"
#include "core/task_system.h"
#include "input/input.h"
#include "camera/camera.h"
#include "render/background.h"
#include "render/skinned_renderer.h"
#include "animation_log.h"
#include <glm/glm.hpp>
#include <vector>

class TopoGame : public Application {
public:
  TerrainRenderer    terrain_renderer;
  InstancedTerrain   instanced_terrain;
  BackgroundRenderer background_renderer;
  InputSystem        input;
  CameraState        camera;
  CameraSystem       camera_system;
  std::vector<GpuPointLight> point_lights;
  TaskSystem          task_system;
  AsyncTerrainState   async_terrain;
  flecs::entity       player_entity;
  bool                player_spawned = false;
  AnimationLogger     anim_log;
  SkinnedRenderer     skinned_renderer;
  bool                skinned_char_loaded = false;

  void on_init(GpuContext &gpu, flecs::world &ecs) override;
  void on_event(const SDL_Event &event, flecs::world &ecs) override;
  void on_render_tool(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) override;
  void on_pre_frame_game(GpuContext &gpu, flecs::world &ecs) override;
  void on_render_game(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) override;
  void on_cleanup(flecs::world &ecs) override;

  bool wants_game_window_open(flecs::world &ecs) override;
  bool wants_game_window_close(flecs::world &ecs) override;

private:
  void render_ui(flecs::world &ecs, bool game_window_open);
  int save_status_timer = 0;

  std::shared_ptr<TerrainMesh> ready_mesh_pending;
  std::shared_ptr<MapData>     ready_map_pending;
  std::shared_ptr<ContourData> ready_contours_pending;

  float regen_cooldown = 0.0f;
  bool  gltf_column_loaded = false;
};
