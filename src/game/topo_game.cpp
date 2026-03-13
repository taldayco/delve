#include "topo_game.h"
#include "core/gltf_loader.h"
#include "render/rig_animation.h"
#include "terrain/basalt.h"
#include "terrain/lava.h"
#include "terrain/noise_composer.h"
#include "terrain/contour.h"
#include "terrain/palettes.h"
#include "terrain/map_util.h"
#include "terrain/hex.h"
#include "ui/imgui_ui.h"
#include "core/watchdog.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

static json params_to_json(const ElevationParams &elev,
                           const WorleyParams &worley, const CompositionParams &comp,
                           const TerrainState &ts) {
  return {
    {"elevation", {
      {"frequency",  elev.frequency}, {"octaves",    elev.octaves},
      {"lacunarity", elev.lacunarity},{"gain",        elev.gain},
      {"scurve_bias", elev.scurve_bias}
    }},
    {"worley", {
      {"frequency",      worley.frequency},
      {"jitter",         worley.jitter},       {"warp_amp",       worley.warp_amp},
      {"warp_frequency", worley.warp_frequency},{"warp_octaves",  worley.warp_octaves}
    }},
    {"composition", {
      {"void_chance",    comp.void_chance},
      {"terrace_levels", comp.terrace_levels},
      {"min_region_size",comp.min_region_size}
    }},
    {"terrain", {
      {"use_isometric",  ts.use_isometric},
      {"current_palette",ts.current_palette},
      {"map_scale",      ts.map_scale},
      {"master_seed",    ts.master_seed}
    }}
  };
}

static void json_to_params(const json &j, ElevationParams &elev,
                            WorleyParams &worley, CompositionParams &comp,
                            TerrainState &ts) {
  if (j.contains("elevation")) {
    auto &e = j["elevation"];
    if (e.contains("frequency"))   elev.frequency   = e["frequency"];
    if (e.contains("octaves"))     elev.octaves     = e["octaves"];
    if (e.contains("lacunarity"))  elev.lacunarity  = e["lacunarity"];
    if (e.contains("gain"))        elev.gain        = e["gain"];
    if (e.contains("scurve_bias")) elev.scurve_bias = e["scurve_bias"];
  }
  if (j.contains("worley")) {
    auto &w = j["worley"];
    if (w.contains("frequency"))       worley.frequency       = w["frequency"];
    if (w.contains("jitter"))          worley.jitter          = w["jitter"];
    if (w.contains("warp_amp"))        worley.warp_amp        = w["warp_amp"];
    if (w.contains("warp_frequency"))  worley.warp_frequency  = w["warp_frequency"];
    if (w.contains("warp_octaves"))    worley.warp_octaves    = w["warp_octaves"];
  }
  if (j.contains("composition")) {
    auto &c = j["composition"];
    if (c.contains("void_chance"))     comp.void_chance     = c["void_chance"];
    if (c.contains("terrace_levels"))  comp.terrace_levels  = c["terrace_levels"];
    if (c.contains("min_region_size")) comp.min_region_size = c["min_region_size"];
  }
  if (j.contains("terrain")) {
    auto &t = j["terrain"];
    if (t.contains("use_isometric"))   ts.use_isometric   = t["use_isometric"];
    if (t.contains("current_palette")) ts.current_palette = t["current_palette"];
    if (t.contains("map_scale"))       ts.map_scale       = t["map_scale"];
    if (t.contains("master_seed"))     ts.master_seed     = t["master_seed"];
  }

  // Backward compatibility: old configs with individual seeds but no master_seed
  if (!j.contains("terrain") || !j["terrain"].contains("master_seed")) {
    if (j.contains("elevation") && j["elevation"].contains("seed")) {
      ts.master_seed = j["elevation"]["seed"];
    }
  }
}

void TopoGame::on_init(GpuContext &gpu, flecs::world &ecs) {
  ecs.set<GamePhase>({});
  ecs.set<TerrainState>({});
  ecs.set<WindowState>({true, false});
  ecs.set<ElevationParams>({});
  ecs.set<RiverParams>({});
  ecs.set<WorleyParams>({});
  ecs.set<CompositionParams>({});
  ecs.set<MapData>({});
  ecs.set<ContourData>({});

  task_system.init(1);

  input.init();

  const float half = Config::MAP_WIDTH_UNITS * 0.5f;
  camera.world_x = half;
  camera.world_y = half;
  camera.follow_x = half;
  camera.follow_y = half;
  camera.following = true;
  camera.min_x = 0.0f;
  camera.max_x = Config::MAP_WIDTH_UNITS;
  camera.min_y = 0.0f;
  camera.max_y = Config::MAP_HEIGHT_UNITS;
  camera.base_frustum_half_w = half;
  camera.base_frustum_half_h = half;

  ecs.system("InputBeginFrame")
      .kind(flecs::PreUpdate)
      .run([this](flecs::iter &) { input.begin_frame(); });

  ecs.system("CameraUpdate")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        auto *phase = ecs.get<GamePhase>();
        if (phase && phase->current != GamePhase::Playing) return;

        auto &in  = input.state();
        float dt  = ecs.delta_time();
        float spd = (camera.base_frustum_half_w / camera.zoom) * 1.5f * dt;

        if (in.held[(int)Action::CameraUp])    camera.world_y -= spd;
        if (in.held[(int)Action::CameraDown])  camera.world_y += spd;
        if (in.held[(int)Action::CameraLeft])  camera.world_x -= spd;
        if (in.held[(int)Action::CameraRight]) camera.world_x += spd;

        bool cam_moved = in.held[(int)Action::CameraUp]    || in.held[(int)Action::CameraDown] ||
                         in.held[(int)Action::CameraLeft]  || in.held[(int)Action::CameraRight];
        if (cam_moved) camera.following = false;

        if (in.held[(int)Action::ZoomIn])
          camera_system.set_zoom(camera, camera.target_zoom + dt * 2.0f);
        if (in.held[(int)Action::ZoomOut])
          camera_system.set_zoom(camera, camera.target_zoom - dt * 2.0f);

        if (!cam_moved && player_entity.is_alive()) {
          const auto *t = player_entity.get<Transform>();
          if (t) {
            camera_system.follow(camera, t->x, t->y);
            const auto *pose = player_entity.get<RigPose>();
            float chest_z = pose ? pose->joints[(int)Joint::CHEST].z : t->z;
            camera.follow_z = chest_z;
          }
        }

        camera_system.update(camera, dt);
      });

  // Create player entity.
  player_entity = ecs.entity("player")
      .add<Player>()
      .add<ActorTag>()
      .set<Transform>({})
      .set<Velocity>({})
      .set<ActorConfig>({})
      .set<ProceduralGait>({})
      .set<LegState>({})
      .set<RigPose>({})
      .set<RigState>({})
      .set<LookAtTarget>({})
      .set<ArmIKGoal>({})
      .set<AnimationOverlay>({})
      .set<GrabState>({})
      .set<RigTransforms>({})
      .set<ProceduralMesh>({});

  // Register all animation ECS systems.
  register_rig_systems(ecs, input, camera, anim_log, player_entity);
}

void TopoGame::on_event(const SDL_Event &event, flecs::world &ecs) {
  input.handle_event(event);

  if (event.type == SDL_EVENT_KEY_DOWN) {
    auto *phase = ecs.get_mut<GamePhase>();
    if (!phase) return;

    if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
      if (phase->current == GamePhase::Playing)  phase->current = GamePhase::Paused;
      else if (phase->current == GamePhase::Paused) phase->current = GamePhase::Playing;
    }
    if (event.key.scancode == SDL_SCANCODE_RETURN) {
      if (phase->current == GamePhase::Menu) phase->current = GamePhase::Playing;
    }
  }
}

void TopoGame::on_render_tool(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) {
  render_ui(ecs, gpu.game_window != nullptr);
  ui_prepare_draw(frame.cmd);
  gpu_begin_render_pass(gpu, frame);
  ui_draw(frame.cmd, frame.render_pass);
}

void TopoGame::on_pre_frame_game(GpuContext &gpu, flecs::world &ecs) {
  if (!terrain_renderer.is_initialized()) return;

  bool needs_depth_rebuild_early = terrain_renderer.depth_needs_rebuild();
  uint32_t target_w_early = needs_depth_rebuild_early
                      ? terrain_renderer.desired_depth_w
                      : terrain_renderer.depth_width();
  uint32_t target_h_early = needs_depth_rebuild_early
                      ? terrain_renderer.desired_depth_h
                      : terrain_renderer.depth_height();
  bool needs_cluster_rebuild_early = false;
  if (target_w_early > 0 && target_h_early > 0) {
    uint32_t tilesX = (uint32_t)std::ceil(target_w_early / 16.0f);
    uint32_t tilesY = (uint32_t)std::ceil(target_h_early / 16.0f);
    needs_cluster_rebuild_early = (tilesX != terrain_renderer.cluster_tiles_x() ||
                                   tilesY != terrain_renderer.cluster_tiles_y());
  }

  // Single GPU idle wait if any operation needs it.
  if (ready_mesh_pending || needs_depth_rebuild_early || needs_cluster_rebuild_early) {
    SDL_WaitForGPUIdle(gpu.device);
  }

  if (ready_mesh_pending) {
    terrain_renderer.upload_mesh(gpu.device, *ready_mesh_pending);

    // Build instanced terrain data from columns
    if (ready_map_pending && !ready_map_pending->columns.empty()) {
      auto *ts = ecs.get<TerrainState>();
      if (ts) {
        instanced_terrain.build_instances(ready_map_pending->columns, *ts);
        instanced_terrain.upload(gpu.device);
      }
    }

    auto *map_data = ecs.get_mut<MapData>();
    auto *contours = ecs.get_mut<ContourData>();

    if (map_data && ready_map_pending) {
      {
        MapData old_map = std::move(*map_data);
        ContourData old_contours = contours ? std::move(*contours) : ContourData{};
      }

      *map_data = std::move(*ready_map_pending);
      if (contours && ready_contours_pending)
        *contours = std::move(*ready_contours_pending);
    }

    ready_mesh_pending.reset();
    ready_map_pending.reset();
    ready_contours_pending.reset();

    player_spawned = false;
    {
      const auto *md = ecs.get<MapData>();
      if (md && !md->columns.empty()) {
        const auto *worley = ecs.get<WorleyParams>();
        uint32_t seed = worley ? (uint32_t)worley->seed : 0u;
        HexColumn col = find_spawn_column(*md, seed);
        float wx, wy;
        hex_to_pixel(col.q, col.r, Config::HEX_SIZE, wx, wy);
        wx /= Config::HEX_SIZE;
        wy /= Config::HEX_SIZE;
        const ActorConfig default_cfg{};
        float wz = col.height + default_cfg.leg_len + default_cfg.shin_len;

        if (player_entity.is_alive()) {
          player_entity.set<Transform>({wx, wy, wz, 0.0f});

          LegState ls{};
          ls.foot[0] = {wx - 0.25f, wy, col.height};
          ls.foot[1] = {wx + 0.25f, wy, col.height};
          ls.prev_foot[0] = ls.foot[0];
          ls.prev_foot[1] = ls.foot[1];
          ls.target[0]    = ls.foot[0];
          ls.target[1]    = ls.foot[1];
          player_entity.set<LegState>(ls);
        }

        camera.world_x = camera.follow_x = wx;
        camera.world_y = camera.follow_y = wy;
        player_spawned = true;
      }
    }
  }

  if (needs_depth_rebuild_early || needs_cluster_rebuild_early) {
    terrain_renderer.prepare_frame_resources(gpu.device);

    if (needs_cluster_rebuild_early) {
      uint32_t w = terrain_renderer.depth_width();
      uint32_t h = terrain_renderer.depth_height();
      if (w > 0 && h > 0) {
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
        if (cmd) {
          terrain_renderer.rebuild_clusters_if_needed(cmd, w, h, 16.0f, 24, 1.0f, 1000.0f);
          SDL_SubmitGPUCommandBuffer(cmd);
          SDL_WaitForGPUIdle(gpu.device);
        }
      }
    }
  }
}

void TopoGame::on_render_game(GpuContext &gpu, FrameContext &frame, flecs::world &ecs) {
  if (!terrain_renderer.is_initialized()) {
    terrain_renderer.init(gpu.device, gpu.game_window, asset_manager);
    terrain_renderer.instanced_terrain = &instanced_terrain;

    // Load glTF column mesh for instanced terrain
    if (!gltf_column_loaded) {
      GltfAsset column_asset = load_gltf(std::string(ASSET_DIR) + "/meshes/basalt_column.glb");
      if (column_asset.ok && !column_asset.meshes.empty()) {
        auto &mesh = column_asset.meshes[0];
        terrain_renderer.upload_gltf_column_mesh(gpu.device,
            mesh.vertices.data(), mesh.vertices.size() * sizeof(GltfVertex),
            mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
            mesh.indices.size());
        gltf_column_loaded = true;
        SDL_Log("Loaded basalt column mesh: %zu verts, %zu indices",
                mesh.vertices.size(), mesh.indices.size());
      } else {
        SDL_Log("Failed to load basalt column: %s", column_asset.error.c_str());
      }
    }

    background_renderer.init(gpu.device,
                             SDL_GetGPUSwapchainTextureFormat(gpu.device, gpu.game_window),
                             terrain_renderer.get_depth_format(),
                             asset_manager);
    rig_renderer.init(gpu.device,
                        terrain_renderer.get_terrain_pipeline(),
                        terrain_renderer.get_dummy_ssbo(),
                        &asset_manager);

    skinned_renderer.init(gpu.device, gpu.game_window, &asset_manager);

  }

  // One-time load of skinned character and animation clips.
  if (skinned_renderer.is_initialized() && !skinned_char_loaded) {
    skinned_renderer.load_character(std::string(ASSET_DIR) + "/characters/wireframe_character.glb");
    skinned_renderer.load_animation("idle",     std::string(ASSET_DIR) + "/characters/anim_idle.glb");
    skinned_renderer.load_animation("walk",     std::string(ASSET_DIR) + "/characters/anim_walk.glb");
    skinned_renderer.load_animation("run",      std::string(ASSET_DIR) + "/characters/anim_run.glb");
    skinned_renderer.load_animation("turn_180", std::string(ASSET_DIR) + "/characters/anim_turn_180.glb");
    skinned_renderer.set_animation("idle");
    skinned_char_loaded = true;
  }

  terrain_renderer.rebuild_dirty_pipelines(gpu.game_window);
  background_renderer.rebuild_if_dirty(
      SDL_GetGPUSwapchainTextureFormat(gpu.device, gpu.game_window),
      terrain_renderer.get_depth_format());

  auto *ts       = ecs.get_mut<TerrainState>();
  auto *elev     = ecs.get_mut<ElevationParams>();
  auto *river    = ecs.get_mut<RiverParams>();
  auto *worley   = ecs.get_mut<WorleyParams>();
  auto *comp     = ecs.get_mut<CompositionParams>();
  auto *map_data = ecs.get_mut<MapData>();
  auto *contours = ecs.get_mut<ContourData>();

  constexpr float REGEN_COOLDOWN_SEC = 0.2f;
  if (regen_cooldown > 0.0f) regen_cooldown -= 1.0f / 60.0f;

  if (ts && ts->need_regenerate && async_terrain.is_generating) {
    async_terrain.cancel_requested.store(true, std::memory_order_relaxed);
  }

  if (ts && ts->need_regenerate && !async_terrain.is_generating) {
    if (regen_cooldown > 0.0f) {
      // Keep need_regenerate true; wait for cooldown to expire.
    } else {
    ts->need_regenerate = false;
    async_terrain.is_generating = true;
    async_terrain.cancel_requested.store(false, std::memory_order_relaxed);
    regen_cooldown = REGEN_COOLDOWN_SEC;

    elev->map_scale = ts->map_scale;

    // Derive per-layer seeds from master seed
    elev->seed   = ts->master_seed;
    river->seed  = ts->master_seed * 7 + 1;
    worley->seed = ts->master_seed * 13 + 3;

    auto elev_snap   = *elev;
    auto river_snap  = *river;
    auto worley_snap = *worley;
    auto comp_snap   = *comp;

    struct TsSnap {
      bool use_isometric;
      int  current_palette;
      float map_scale;
      float contour_opacity;
      bool need_regenerate;
    };
    TsSnap ts_snap { ts->use_isometric, ts->current_palette,
                     ts->map_scale, ts->contour_opacity, false };

    task_system.enqueue([this, elev_snap, river_snap, worley_snap, comp_snap, ts_snap]() {
      SDL_Log("Async regen: started");
      auto t0 = SDL_GetTicks();

      auto should_abort = [this]() {
        return g_emergency_shutdown.load(std::memory_order_relaxed)
            || async_terrain.cancel_requested.load(std::memory_order_relaxed);
      };

      auto md = std::make_shared<MapData>();
      md->allocate(Config::MAP_WIDTH, Config::MAP_HEIGHT);

      compose_layers(*md, elev_snap, river_snap, worley_snap, comp_snap, &async_terrain.async_cache);
      if (should_abort()) { async_terrain.is_generating = false; return; }

      md->columns = generate_basalt_columns_v2(*md, Config::HEX_SIZE);
      if (should_abort()) { async_terrain.is_generating = false; return; }

      auto fill = generate_lava_and_void(*md, comp_snap.void_chance, worley_snap.seed);
      if (should_abort()) { async_terrain.is_generating = false; return; }
      md->lava_bodies = std::move(fill.lava_bodies);
      md->void_bodies = std::move(fill.void_bodies);

      auto cd = std::make_shared<ContourData>();
      int n = Config::MAP_WIDTH * Config::MAP_HEIGHT;
      cd->heightmap.resize(n);
      std::copy(md->basalt_height.begin(), md->basalt_height.end(), cd->heightmap.begin());
      float interval = 1.0f / comp_snap.terrace_levels;
      extract_contours(cd->heightmap, Config::MAP_WIDTH, Config::MAP_HEIGHT,
                       interval, cd->contour_lines, cd->band_map);
      simplify_contours(cd->contour_lines, 0.5f);
      if (should_abort()) { async_terrain.is_generating = false; return; }

      TerrainState ts_for_build;
      ts_for_build.use_isometric   = ts_snap.use_isometric;
      ts_for_build.current_palette = ts_snap.current_palette;
      ts_for_build.map_scale       = ts_snap.map_scale;
      ts_for_build.contour_opacity = ts_snap.contour_opacity;
      ts_for_build.need_regenerate = false;

      auto mesh = std::make_shared<TerrainMesh>(build_terrain_mesh(ts_for_build, *md, *cd));
      if (should_abort()) { async_terrain.is_generating = false; return; }

      {
        std::lock_guard<std::mutex> lk(async_terrain.pending_mtx);
        async_terrain.pending_mesh     = std::move(mesh);
        async_terrain.pending_map      = std::move(md);
        async_terrain.pending_contours = std::move(cd);
      }
      async_terrain.is_generating = false;

      SDL_Log("Async regen: done in %llu ms", (unsigned long long)(SDL_GetTicks() - t0));
    });
    }
  }

  if (ts && !async_terrain.is_generating && !ready_mesh_pending) {
    std::lock_guard<std::mutex> lk(async_terrain.pending_mtx);
    ready_mesh_pending     = std::move(async_terrain.pending_mesh);
    ready_map_pending      = std::move(async_terrain.pending_map);
    ready_contours_pending = std::move(async_terrain.pending_contours);
  }

  float time = SDL_GetTicks() / 1000.0f;

  float aspect = (frame.swapchain_w > 0 && frame.swapchain_h > 0)
                 ? (float)frame.swapchain_w / (float)frame.swapchain_h
                 : 1.0f;

  CameraMatrices cam_mats = camera_system.build_matrices(camera, aspect);

  point_lights.clear();
  if (map_data) {
    const float inv = 1.0f / Config::HEX_SIZE;
    for (const auto &lava : map_data->lava_bodies) {
      float cx = (lava.min_x + lava.max_x) * 0.5f * inv;
      float cy = (lava.min_y + lava.max_y) * 0.5f * inv;
      GpuPointLight pl;
      pl.pos_x     = cx;
      pl.pos_y     = cy;
      pl.pos_z     = lava.height + 1.0f;
      pl.radius    = 40.0f;
      pl.color_r   = 1.0f;
      pl.color_g   = 0.35f;
      pl.color_b   = 0.05f;
      pl.intensity = 3.0f;
      point_lights.push_back(pl);
    }
  }

  SDL_GPURenderPass *bg_pass = terrain_renderer.begin_render_pass(
      frame.cmd, frame.swapchain, frame.swapchain_w, frame.swapchain_h);
  if (!bg_pass) return;

  background_renderer.draw(frame.cmd, bg_pass, time, camera.world_x, camera.world_y);
  SDL_EndGPURenderPass(bg_pass);

  if (terrain_renderer.has_mesh() && ts) {
    const auto *md = ecs.get<MapData>();
    static const MapData empty_map_data;

    SceneUniforms uniforms = compute_uniforms(
        md ? *md : empty_map_data,
        cam_mats.view, cam_mats.projection,
        terrain_renderer.cluster_tiles_x(), terrain_renderer.cluster_tiles_y(),
        time, ts->contour_opacity,
        (uint32_t)point_lights.size());

    terrain_renderer.draw(frame.cmd, frame.swapchain,
                          frame.swapchain_w, frame.swapchain_h,
                          uniforms, point_lights,
                          gpu.upload_manager);

    // Animate skinned renderer and select clip based on player speed.
    if (skinned_renderer.is_initialized() && skinned_renderer.has_character()) {
      float speed = 0.0f;
      glm::vec3 player_pos(0.f);
      float player_facing = 0.f;
      if (player_entity.is_alive()) {
        const auto *vel = player_entity.get<Velocity>();
        if (vel) speed = glm::length(glm::vec2(vel->x, vel->y));
        const auto *t = player_entity.get<Transform>();
        if (t) { player_pos = glm::vec3(t->x, t->y, t->z); player_facing = t->facing; }
      }
      skinned_renderer.update(1.0f / 60.0f, player_pos, player_facing, speed);
      skinned_renderer.prepare(frame.cmd);
    }

    if (use_skinned) {
      // Draw skinned character renderer.
      if (skinned_renderer.is_initialized() && skinned_renderer.has_character() && player_entity.is_alive()) {
        const auto *t = player_entity.get<Transform>();
        if (t) {
          SDL_GPURenderPass *actor_pass =
              terrain_renderer.begin_render_pass_load_preserve_depth(
                  frame.cmd, frame.swapchain,
                  frame.swapchain_w, frame.swapchain_h);
          if (actor_pass) {
            skinned_renderer.draw(actor_pass, frame.cmd, uniforms,
                                  terrain_renderer.get_point_light_ssbo(),
                                  terrain_renderer.get_light_grid_ssbo(),
                                  terrain_renderer.get_global_index_ssbo());
            SDL_EndGPURenderPass(actor_pass);
          }
        }
      }
    } else {
      if (rig_renderer.is_initialized()) {
        uint32_t actor_vert_count = rig_renderer.prepare(frame.cmd, ecs);
        if (actor_vert_count > 0) {
          SDL_GPURenderPass *actor_pass =
              terrain_renderer.begin_render_pass_load_preserve_depth(
                  frame.cmd, frame.swapchain,
                  frame.swapchain_w, frame.swapchain_h);
          if (actor_pass) {
            rig_renderer.draw(actor_pass, frame.cmd, uniforms,
                                terrain_renderer.get_point_light_ssbo(),
                                actor_vert_count);
            SDL_EndGPURenderPass(actor_pass);
          }
        }
      }
    }
  }

  frame.render_pass = nullptr;
}

void TopoGame::on_cleanup(flecs::world &ecs) {
  anim_log.close();
  task_system.shutdown();
  instanced_terrain.cleanup(gpu_ctx.device);
  terrain_renderer.cleanup(gpu_ctx.device);
  background_renderer.cleanup();
  rig_renderer.cleanup(gpu_ctx.device);
  skinned_renderer.cleanup();
}

bool TopoGame::wants_game_window_open(flecs::world &ecs) {
  auto *ws = ecs.get_mut<WindowState>();
  if (ws && ws->launch_game_requested) {
    ws->launch_game_requested = false;
    return true;
  }
  return false;
}

bool TopoGame::wants_game_window_close(flecs::world &ecs) {
  auto *ws = ecs.get_mut<WindowState>();
  if (ws && ws->close_game_requested) {
    ws->close_game_requested = false;
    return true;
  }
  return false;
}

void TopoGame::render_ui(flecs::world &ecs, bool game_window_open) {
  ui_begin_frame();

  auto *ts     = ecs.get_mut<TerrainState>();
  auto *elev   = ecs.get_mut<ElevationParams>();
  auto *river  = ecs.get_mut<RiverParams>();
  auto *worley = ecs.get_mut<WorleyParams>();
  auto *comp   = ecs.get_mut<CompositionParams>();
  auto *ws     = ecs.get_mut<WindowState>();
  auto *contours = ecs.get<ContourData>();

  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({io.DisplaySize.x, io.DisplaySize.y}, ImGuiCond_Always);
  ImGui::Begin("Controls", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

  if (!game_window_open) {
    if (ImGui::Button("Launch Game", {-1, 40})) ws->launch_game_requested = true;
  } else {
    if (ImGui::Button("Close Game",  {-1, 40})) ws->close_game_requested  = true;
  }

  ImGui::Separator();
  ImGui::SliderInt("Master Seed", &ts->master_seed, 0, 10000);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::Text("Elevation");
  ImGui::SliderFloat("Frequency",   &elev->frequency,   0.001f, 0.05f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Octaves",     &elev->octaves,     1, 8);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Lacunarity",  &elev->lacunarity,  1.0f, 4.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Gain",        &elev->gain,        0.1f, 1.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Terrace Levels", &comp->terrace_levels, 3, 20);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Min Region Size",&comp->min_region_size,50, 2000);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("S-Curve Bias",&elev->scurve_bias, 0.0f, 1.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::Text("Worley Noise");
  ImGui::SliderFloat("Worley Freq",  &worley->frequency,      0.001f, 0.1f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Worley Jitter",&worley->jitter,         0.0f, 2.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Warp Amp",     &worley->warp_amp,       0.0f, 100.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Warp Freq",    &worley->warp_frequency, 0.001f, 0.02f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Warp Octaves", &worley->warp_octaves,   1, 6);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::Text("Composition");
  ImGui::SliderFloat("Void Chance", &comp->void_chance, 0.0f, 1.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::Text("Contour Lines");
  ImGui::Text("Interval: %.4f (from %d terrace levels)",
              1.0f / comp->terrace_levels, comp->terrace_levels);

  ImGui::Separator();
  ImGui::Text("Color Palette");
  if (ImGui::BeginCombo("##palette", PALETTES[ts->current_palette].name)) {
    for (int i = 0; i < PALETTE_COUNT; ++i) {
      bool sel = (ts->current_palette == i);
      if (ImGui::Selectable(PALETTES[i].name, sel)) {
        ts->current_palette = i;
        ts->need_regenerate = true;
      }
      if (sel) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();
  ImGui::Text("Map Scale");
  ImGui::SliderFloat("Map Scale", &ts->map_scale, 0.25f, 4.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Separator();
  ImGui::Text("Rendering Mode");
  ImGui::Checkbox("Use Instanced Terrain", &terrain_renderer.use_instanced);
  ImGui::Checkbox("Use PBR Shading", &terrain_renderer.use_pbr);
  ImGui::Checkbox("Use Skinned Character", &use_skinned);
  ImGui::SliderFloat("Skinned Scale", &skinned_renderer.debug_uniform_scale, 0.01f, 100.0f);
  ImGui::Checkbox("Raw Transform", &skinned_renderer.debug_raw_transform);
  ImGui::Separator();
  if (ImGui::Button("Regenerate", {-1, 40})) ts->need_regenerate = true;
  if (ImGui::Button("Reset", {-1, 40})) {
    *elev   = ElevationParams{};
    *worley = WorleyParams{};
    *comp   = CompositionParams{};
    ts->use_isometric   = DEFAULT_ISOMETRIC;
    ts->current_palette = 0;
    ts->map_scale       = Config::DEFAULT_MAP_SCALE;
    ts->master_seed     = 1337;
    ts->need_regenerate = true;
  }

  float half_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button("Save Config", {half_w, 30})) {
    std::ofstream f("config.json");
    if (f.is_open()) {
      f << params_to_json(*elev, *worley, *comp, *ts).dump(2);
      save_status_timer = 60;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Config", {half_w, 30})) {
    std::ifstream f("config.json");
    if (f.is_open()) {
      try {
        json j = json::parse(f);
        json_to_params(j, *elev, *worley, *comp, *ts);
        ts->need_regenerate = true;
        save_status_timer = -60;
      } catch (...) { save_status_timer = -1; }
    }
  }
  if (save_status_timer > 0)       { ImGui::SameLine(); ImGui::Text("Saved!");  save_status_timer--; }
  else if (save_status_timer < -1) { ImGui::SameLine(); ImGui::Text("Loaded!"); save_status_timer++; }

  ImGui::Separator();
  ImGui::Text("Stats");
  ImGui::Text("Contour Lines: %zu", contours ? contours->contour_lines.size() : 0u);
  ImGui::Text("Resolution: %dx%d", Config::MAP_WIDTH, Config::MAP_HEIGHT);
  ImGui::Text("Camera: (%.1f, %.1f) zoom %.2fx", camera.world_x, camera.world_y, camera.zoom);
  if (anim_log.active) {
    ImGui::TextColored({1.0f, 0.2f, 0.2f, 1.0f}, "REC");
    ImGui::SameLine();
    if (ImGui::Button("Stop Animation Log", {-1, 0})) anim_log.toggle();
  } else {
    if (ImGui::Button("Record Animation Log", {-1, 0})) anim_log.toggle();
  }

  ImGui::Separator();
  if (ImGui::CollapsingHeader("Resources")) {
    asset_manager.render_debug_ui();
  }

  ImGui::End();
  ui_end_frame();
}
