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