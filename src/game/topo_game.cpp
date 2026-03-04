#include "topo_game.h"
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
      {"seed",       elev.seed},      {"scurve_bias", elev.scurve_bias}
    }},
    {"worley", {
      {"frequency",      worley.frequency},    {"seed",           worley.seed},
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
      {"map_scale",      ts.map_scale}
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
    if (e.contains("seed"))        elev.seed        = e["seed"];
    if (e.contains("scurve_bias")) elev.scurve_bias = e["scurve_bias"];
  }
  if (j.contains("worley")) {
    auto &w = j["worley"];
    if (w.contains("frequency"))       worley.frequency       = w["frequency"];
    if (w.contains("seed"))            worley.seed            = w["seed"];
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
  ecs.set<NoiseCache>({});
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

        // Follow player if alive and no camera keys held.
        if (!cam_moved && player_entity.is_alive()) {
          const auto *t = player_entity.get<Transform>();
          if (t) camera_system.follow(camera, t->x, t->y);
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
      .set<SkeletonPose>({});

  // PlayerMovementSystem.
  ecs.system("PlayerMovementSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        auto *phase = ecs.get<GamePhase>();
        if (!phase || phase->current != GamePhase::Playing) return;
        if (!player_entity.is_alive()) return;

        auto *t    = player_entity.get_mut<Transform>();
        auto *vel  = player_entity.get_mut<Velocity>();
        auto *gait = player_entity.get_mut<ProceduralGait>();
        if (!t || !vel || !gait) return;

        auto &in = input.state();
        float dt = ecs.delta_time();

        vel->x = 0.0f;
        vel->y = 0.0f;
        if (in.held[(int)Action::MoveUp])    vel->y -= gait->move_speed;
        if (in.held[(int)Action::MoveDown])  vel->y += gait->move_speed;
        if (in.held[(int)Action::MoveLeft])  vel->x -= gait->move_speed;
        if (in.held[(int)Action::MoveRight]) vel->x += gait->move_speed;

        t->x += vel->x * dt;
        t->y += vel->y * dt;

        float spd = sqrtf(vel->x * vel->x + vel->y * vel->y);
        if (spd > 0.001f)
          t->facing = atan2f(vel->y, vel->x);
      });

  // ActorGroundingSystem.
  ecs.system("ActorGroundingSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        const auto *map_data = ecs.get<MapData>();
        if (!map_data || map_data->basalt_height.empty()) return;

        float dt = ecs.delta_time();
        ecs.each([&](ActorTag, Transform &t, const ActorConfig &cfg) {
          float target_z = sample_world_height(*map_data, t.x, t.y)
                           + cfg.leg_len + cfg.shin_len;
          t.z = t.z + (target_z - t.z) * (1.0f - expf(-8.0f * dt));
        });
      });

  // GaitSystem.
  ecs.system("GaitSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        const auto *map_data = ecs.get<MapData>();
        if (!map_data || map_data->basalt_height.empty()) return;

        float dt = ecs.delta_time();
        ecs.each([&](ActorTag,
                     const Transform &t,
                     const Velocity &vel,
                     ProceduralGait &gait,
                     LegState &legs,
                     const ActorConfig &cfg) {

          float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
          gait.phase += speed * dt * (glm::two_pi<float>() / (2.0f * gait.stride_len));

          float fwd_x = cosf(t.facing), fwd_y = sinf(t.facing);
          float vel_dx = speed > 0.001f ? vel.x / speed : fwd_x;
          float vel_dy = speed > 0.001f ? vel.y / speed : fwd_y;

          float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

          // Hip socket XY offsets (legs offset left/right of facing).
          float hip_sign[2] = { -1.0f, 1.0f }; // left, right

          for (int leg = 0; leg < 2; ++leg) {
            float hip_x = t.x + rght_x * hip_sign[leg] * cfg.hip_width;
            float hip_y = t.y + rght_y * hip_sign[leg] * cfg.hip_width;
            float hip_z = t.z;

            float stride_off_x = vel_dx * gait.stride_len * 0.5f;
            float stride_off_y = vel_dy * gait.stride_len * 0.5f;
            float pred_x = hip_x + stride_off_x;
            float pred_y = hip_y + stride_off_y;
            float pred_z = sample_world_height(*map_data, pred_x, pred_y);

            if (!legs.stepping[leg]) {
              float dx = legs.foot[leg].x - pred_x;
              float dy = legs.foot[leg].y - pred_y;
              float dist = sqrtf(dx * dx + dy * dy);
              if (dist > gait.stride_len * 0.5f) {
                legs.stepping[leg]  = true;
                legs.progress[leg]  = 0.0f;
                legs.prev_foot[leg] = legs.foot[leg];
                legs.target[leg]    = {pred_x, pred_y, pred_z};
              }
            }

            if (legs.stepping[leg]) {
              legs.progress[leg] += dt / gait.step_duration;
              float progress = std::min(legs.progress[leg], 1.0f);
              // Cubic smoothstep.
              float ts = progress * progress * (3.0f - 2.0f * progress);

              legs.foot[leg].x = legs.prev_foot[leg].x + (legs.target[leg].x - legs.prev_foot[leg].x) * ts;
              legs.foot[leg].y = legs.prev_foot[leg].y + (legs.target[leg].y - legs.prev_foot[leg].y) * ts;
              legs.foot[leg].z = legs.prev_foot[leg].z + (legs.target[leg].z - legs.prev_foot[leg].z) * ts
                                 + sinf(progress * glm::pi<float>()) * gait.step_height;

              if (legs.progress[leg] >= 1.0f) {
                legs.stepping[leg] = false;
                legs.foot[leg]     = legs.target[leg];
              }
            }
          }
        });
      });

  // IKSystem.
  ecs.system("IKSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        ecs.each([&](ActorTag,
                     const Transform &t,
                     const LegState &legs,
                     const ActorConfig &cfg,
                     SkeletonPose &pose) {

          using J = Joint;

          float facing = t.facing;
          float fwd_x  =  cosf(facing), fwd_y  = sinf(facing);
          float rght_x = -sinf(facing), rght_y = cosf(facing);

          // Root and spine chain.
          glm::vec3 root(t.x, t.y, t.z);
          glm::vec3 spine  = root  + glm::vec3(0, 0, cfg.torso_len * 0.4f);
          glm::vec3 chest  = root  + glm::vec3(0, 0, cfg.torso_len);
          glm::vec3 neck   = chest + glm::vec3(0, 0, cfg.neck_len);
          glm::vec3 head   = neck  + glm::vec3(0, 0, cfg.head_radius);

          pose.joints[(int)J::ROOT]  = root;
          pose.joints[(int)J::SPINE] = spine;
          pose.joints[(int)J::CHEST] = chest;
          pose.joints[(int)J::NECK]  = neck;
          pose.joints[(int)J::HEAD]  = head;

          // Hip sockets.
          glm::vec3 l_hip(t.x - rght_x * cfg.hip_width, t.y - rght_y * cfg.hip_width, t.z);
          glm::vec3 r_hip(t.x + rght_x * cfg.hip_width, t.y + rght_y * cfg.hip_width, t.z);
          pose.joints[(int)J::L_HIP] = l_hip;
          pose.joints[(int)J::R_HIP] = r_hip;

          // Shoulder sockets.
          glm::vec3 l_shoulder(chest.x - rght_x * cfg.shoulder_width, chest.y - rght_y * cfg.shoulder_width, chest.z);
          glm::vec3 r_shoulder(chest.x + rght_x * cfg.shoulder_width, chest.y + rght_y * cfg.shoulder_width, chest.z);
          pose.joints[(int)J::L_SHOULDER] = l_shoulder;
          pose.joints[(int)J::R_SHOULDER] = r_shoulder;

          // Arm joints (hang down with slight forward lean).
          pose.joints[(int)J::L_ELBOW] = l_shoulder + glm::vec3(0, 0, -cfg.arm_len     * 0.8f);
          pose.joints[(int)J::L_WRIST] = pose.joints[(int)J::L_ELBOW] + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);
          pose.joints[(int)J::R_ELBOW] = r_shoulder + glm::vec3(0, 0, -cfg.arm_len     * 0.8f);
          pose.joints[(int)J::R_WRIST] = pose.joints[(int)J::R_ELBOW] + glm::vec3(0, 0, -cfg.forearm_len * 0.8f);

          // Leg IK — two-bone analytical solver for each leg.
          auto solve_leg = [&](glm::vec3 H, glm::vec3 foot_target,
                               float a, float b,
                               glm::vec3 pole,
                               glm::vec3 &out_knee, glm::vec3 &out_ankle) {
            out_ankle = foot_target;

            glm::vec3 axis = foot_target - H;
            float D = glm::length(axis);
            float min_D = fabsf(a - b) + 0.001f;
            float max_D = a + b - 0.001f;
            D = std::max(min_D, std::min(D, max_D));

            if (glm::length(axis) > 1e-5f)
              axis = glm::normalize(axis) * D;
            else
              axis = glm::vec3(0, 0, -D);

            float cos_alpha = (a * a + D * D - b * b) / (2.0f * a * D);
            cos_alpha = std::max(-1.0f, std::min(1.0f, cos_alpha));
            float alpha = acosf(cos_alpha);

            glm::vec3 axis_n = glm::normalize(axis);
            glm::vec3 pole_off = pole - H;
            glm::vec3 perp = pole_off - glm::dot(pole_off, axis_n) * axis_n;
            if (glm::length(perp) > 1e-5f)
              perp = glm::normalize(perp);
            else
              perp = glm::vec3(rght_x, rght_y, 0.0f);

            glm::vec3 dir_to_knee = axis_n * cosf(alpha) + perp * sinf(alpha);
            out_knee = H + dir_to_knee * a;
          };

          // Left leg.
          glm::vec3 l_pole = l_hip + glm::vec3(-rght_x * 0.5f, -rght_y * 0.5f, 0.2f);
          glm::vec3 l_knee, l_ankle;
          solve_leg(l_hip, legs.foot[0], cfg.leg_len, cfg.shin_len, l_pole, l_knee, l_ankle);
          pose.joints[(int)J::L_KNEE]  = l_knee;
          pose.joints[(int)J::L_ANKLE] = l_ankle;

          // Right leg.
          glm::vec3 r_pole = r_hip + glm::vec3(rght_x * 0.5f, rght_y * 0.5f, 0.2f);
          glm::vec3 r_knee, r_ankle;
          solve_leg(r_hip, legs.foot[1], cfg.leg_len, cfg.shin_len, r_pole, r_knee, r_ankle);
          pose.joints[(int)J::R_KNEE]  = r_knee;
          pose.joints[(int)J::R_ANKLE] = r_ankle;
        });
      });

  // SkeletonFinaliseSystem.
  static float s_sway_phase = 0.0f;
  static float s_sway_amt   = 0.04f;
  static float s_lean_x     = 0.0f;
  static float s_lean_y     = 0.0f;

  ecs.system("SkeletonFinaliseSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        ecs.each([&](ActorTag,
                     const Transform &t,
                     const Velocity &vel,
                     const ActorConfig &cfg,
                     SkeletonPose &pose) {

          using J = Joint;
          float dt = ecs.delta_time();
          float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);

          float rght_x = -sinf(t.facing), rght_y = cosf(t.facing);

          // CoM hip sway.
          s_sway_phase += speed * dt * 6.0f;
          float sway = sinf(s_sway_phase) * s_sway_amt;
          glm::vec3 sway_vec(rght_x * sway, rght_y * sway, 0.0f);
          pose.joints[(int)J::ROOT]  += sway_vec;
          pose.joints[(int)J::SPINE] += sway_vec;
          pose.joints[(int)J::CHEST] += sway_vec;

          // Torso lean forward when moving.
          float lean = 0.03f;
          s_lean_x = 0.0f;
          s_lean_y = 0.0f;
          if (speed > 0.001f) {
            s_lean_x = vel.x / speed * lean * speed;
            s_lean_y = vel.y / speed * lean * speed;
            glm::vec3 lean_vec(s_lean_x, s_lean_y, 0.0f);
            pose.joints[(int)J::CHEST] += lean_vec;
            pose.joints[(int)J::NECK]  += lean_vec;
            pose.joints[(int)J::HEAD]  += lean_vec;
          }
        });
      });

  // AnimationLogSystem — runs after SkeletonFinaliseSystem.
  ecs.system("AnimationLogSystem")
      .kind(flecs::PostUpdate)
      .run([this, &ecs](flecs::iter &) {
        if (!anim_log.active) return;
        if (!player_entity.is_alive()) return;

        const auto *t    = player_entity.get<Transform>();
        const auto *vel  = player_entity.get<Velocity>();
        const auto *gait = player_entity.get<ProceduralGait>();
        const auto *legs = player_entity.get<LegState>();
        const auto *pose = player_entity.get<SkeletonPose>();
        const auto *cfg  = player_entity.get<ActorConfig>();
        if (!t || !vel || !gait || !legs || !pose || !cfg) return;

        float dt = ecs.delta_time();
        anim_log.begin_frame(dt);
        anim_log.log_transform(*t, *vel);
        anim_log.log_gait(*gait);
        anim_log.log_legs(*legs, *t, *cfg);
        anim_log.log_joints(*pose, *t);
        anim_log.log_finalize(s_sway_phase, s_sway_amt, s_lean_x, s_lean_y);
        anim_log.log_camera(camera);
        anim_log.end_frame();
      });
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

  // Upload pending mesh first — this may transition has_mesh() from false to true.
  if (ready_mesh_pending) {
    // No frame command buffer is open here, so SDL_WaitForGPUIdle inside
    // upload_mesh is safe.
    terrain_renderer.upload_mesh(gpu.device, *ready_mesh_pending);

    auto *map_data = ecs.get_mut<MapData>();
    auto *contours = ecs.get_mut<ContourData>();

    if (map_data && ready_map_pending) {
      // Destroy old data immediately on main thread (scope-based) rather than
      // deferring to the single worker thread. Deferred destruction caused old
      // MapData objects to pile up behind in-flight generations, compounding
      // memory usage across rapid seed changes.
      {
        MapData old_map = std::move(*map_data);
        ContourData old_contours = contours ? std::move(*contours) : ContourData{};
        // old_map and old_contours freed here at scope exit
      }

      *map_data = std::move(*ready_map_pending);
      if (contours && ready_contours_pending)
        *contours = std::move(*ready_contours_pending);
    }

    ready_mesh_pending.reset();
    ready_map_pending.reset();
    ready_contours_pending.reset();

    // Spawn player on new terrain.
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

          // Initialise feet directly below hip sockets.
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

  // Depth texture is rebuilt first (if needed), then clusters are sized to match.
  // desired_depth_w/h are set by begin_render_pass from the validated swapchain size.
  bool needs_depth_rebuild = terrain_renderer.depth_needs_rebuild();

  // Use the depth texture dimensions (post-rebuild) for cluster sizing.
  // desired_depth_w/h reflect the last confirmed swapchain size from begin_render_pass.
  uint32_t target_w = terrain_renderer.depth_needs_rebuild()
                      ? terrain_renderer.desired_depth_w  // will be built now
                      : terrain_renderer.depth_width();    // already correct
  uint32_t target_h = terrain_renderer.depth_needs_rebuild()
                      ? terrain_renderer.desired_depth_h
                      : terrain_renderer.depth_height();

  bool needs_cluster_rebuild = false;
  if (target_w > 0 && target_h > 0) {
    uint32_t tilesX = (uint32_t)std::ceil(target_w / 16.0f);
    uint32_t tilesY = (uint32_t)std::ceil(target_h / 16.0f);
    needs_cluster_rebuild = (tilesX != terrain_renderer.cluster_tiles_x() ||
                             tilesY != terrain_renderer.cluster_tiles_y());
  }

  if (needs_cluster_rebuild || needs_depth_rebuild) {
    // Single wait covers both operations.
    SDL_WaitForGPUIdle(gpu.device);

    // Rebuild depth texture first so target_w/h are correct for cluster sizing.
    terrain_renderer.prepare_frame_resources(gpu.device);

    if (needs_cluster_rebuild) {
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
    background_renderer.init(gpu.device,
                             SDL_GetGPUSwapchainTextureFormat(gpu.device, gpu.game_window),
                             terrain_renderer.get_depth_format(),
                             asset_manager);
    actor_renderer.init(gpu.device,
                        terrain_renderer.get_terrain_pipeline(),
                        terrain_renderer.get_dummy_ssbo(),
                        &asset_manager);
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
  auto *cache    = ecs.get_mut<NoiseCache>();
  auto *contours = ecs.get_mut<ContourData>();

  // Debounce: tick down cooldown each frame (~16ms at 60 FPS).
  constexpr float REGEN_COOLDOWN_SEC = 0.2f;
  if (regen_cooldown > 0.0f) regen_cooldown -= 1.0f / 60.0f;

  // If a regeneration is requested while one is in-flight, signal cancellation.
  if (ts && ts->need_regenerate && async_terrain.is_generating) {
    async_terrain.cancel_requested.store(true, std::memory_order_relaxed);
  }

  // Kick off async generation when needed, not already running, and cooldown elapsed.
  if (ts && ts->need_regenerate && !async_terrain.is_generating) {
    if (regen_cooldown > 0.0f) {
      // Keep need_regenerate true; wait for cooldown to expire.
    } else {
    ts->need_regenerate = false;
    async_terrain.is_generating = true;
    async_terrain.cancel_requested.store(false, std::memory_order_relaxed);
    regen_cooldown = REGEN_COOLDOWN_SEC;

    // Snapshot all ECS params by value — worker thread must not touch ECS.
    elev->map_scale = ts->map_scale;
    auto elev_snap   = *elev;
    auto river_snap  = *river;
    auto worley_snap = *worley;
    auto comp_snap   = *comp;

    // Plain-data snapshot of TerrainState for use inside build_terrain_mesh.
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

      // Helper: check if we should bail out early.
      auto should_abort = [this]() {
        return g_emergency_shutdown.load(std::memory_order_relaxed)
            || async_terrain.cancel_requested.load(std::memory_order_relaxed);
      };

      auto md = std::make_shared<MapData>();
      md->allocate(Config::MAP_WIDTH, Config::MAP_HEIGHT);

      // NoiseCache is ECS-owned and not thread-safe — pass nullptr.
      compose_layers(*md, elev_snap, river_snap, worley_snap, comp_snap, nullptr);
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

      // Reconstruct a TerrainState for build_terrain_mesh (reads only current_palette).
      TerrainState ts_for_build;
      ts_for_build.use_isometric   = ts_snap.use_isometric;
      ts_for_build.current_palette = ts_snap.current_palette;
      ts_for_build.map_scale       = ts_snap.map_scale;
      ts_for_build.contour_opacity = ts_snap.contour_opacity;
      ts_for_build.need_regenerate = false;

      auto mesh = std::make_shared<TerrainMesh>(build_terrain_mesh(ts_for_build, *md, *cd));
      if (should_abort()) { async_terrain.is_generating = false; return; }

      // Hand off results to main thread under the pending_mtx lock.
      {
        std::lock_guard<std::mutex> lk(async_terrain.pending_mtx);
        async_terrain.pending_mesh     = std::move(mesh);
        async_terrain.pending_map      = std::move(md);
        async_terrain.pending_contours = std::move(cd);
      }
      async_terrain.is_generating = false;

      SDL_Log("Async regen: done in %llu ms", (unsigned long long)(SDL_GetTicks() - t0));
    });
    } // else (cooldown expired)
  }

  // Main thread: pull completed async results out from under the mutex.
  // Do NOT call upload_mesh here — it calls SDL_WaitForGPUIdle which must not
  // run while a frame command buffer is open. The actual upload happens in
  // on_pre_frame_game, which is called before gpu_acquire_game_frame.
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

    // Actor pass — upload geometry before opening the render pass.
    if (actor_renderer.is_initialized()) {
      uint32_t actor_vert_count = actor_renderer.prepare(frame.cmd, ecs);
      if (actor_vert_count > 0) {
        SDL_GPURenderPass *actor_pass =
            terrain_renderer.begin_render_pass_load_preserve_depth(
                frame.cmd, frame.swapchain,
                frame.swapchain_w, frame.swapchain_h);
        if (actor_pass) {
          actor_renderer.draw(actor_pass, frame.cmd, uniforms,
                              terrain_renderer.get_point_light_ssbo(),
                              actor_vert_count);
          SDL_EndGPURenderPass(actor_pass);
        }
      }
    }
  }

  frame.render_pass = nullptr;
}

void TopoGame::on_cleanup(flecs::world &ecs) {
  anim_log.close();
  task_system.shutdown();
  terrain_renderer.cleanup(gpu_ctx.device);
  background_renderer.cleanup();
  actor_renderer.cleanup(gpu_ctx.device);
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
  auto *cache  = ecs.get_mut<NoiseCache>();
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
  ImGui::Text("Elevation");
  ImGui::SliderFloat("Frequency",   &elev->frequency,   0.001f, 0.05f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Octaves",     &elev->octaves,     1, 8);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Lacunarity",  &elev->lacunarity,  1.0f, 4.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderFloat("Gain",        &elev->gain,        0.1f, 1.0f);
  ts->need_regenerate |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(  "Seed",        &elev->seed,        0, 10000);
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
  ImGui::SliderInt(  "Worley Seed",  &worley->seed,           0, 10000);
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
  if (ImGui::Button("Regenerate", {-1, 40})) ts->need_regenerate = true;
  if (ImGui::Button("Reset", {-1, 40})) {
    *elev   = ElevationParams{};
    *worley = WorleyParams{};
    *comp   = CompositionParams{};
    ts->use_isometric   = DEFAULT_ISOMETRIC;
    ts->current_palette = 0;
    ts->map_scale       = Config::DEFAULT_MAP_SCALE;
    cache->invalidate_all();
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
        cache->invalidate_all();
        ts->need_regenerate = true;
        save_status_timer = -60;
      } catch (...) { save_status_timer = -1; }
    }
  }
  if (save_status_timer > 0)    { ImGui::SameLine(); ImGui::Text("Saved!");  save_status_timer--; }
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
