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