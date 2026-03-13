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