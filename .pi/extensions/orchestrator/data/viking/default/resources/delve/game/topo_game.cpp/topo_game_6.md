void TopoGame::on_cleanup(flecs::world &ecs) {
  anim_log.close();
  task_system.shutdown();
  instanced_terrain.cleanup(gpu_ctx.device);
  terrain_renderer.cleanup(gpu_ctx.device);
  background_renderer.cleanup();
  rig_renderer.cleanup(gpu_ctx.device);
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