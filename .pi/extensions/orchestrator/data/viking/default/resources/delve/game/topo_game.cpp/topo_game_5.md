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

  frame.render_pass = nullptr;
}