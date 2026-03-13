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