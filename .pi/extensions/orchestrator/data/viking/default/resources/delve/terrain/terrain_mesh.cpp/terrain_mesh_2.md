const float inv_unit = 1.0f / Config::HEX_SIZE;
  for (const auto &lava : lava_bodies) {
    uint32_t base_idx = (uint32_t)mesh.lava_vertices.size();
    for (const auto &v : lava.mesh.vertices) {
      mesh.lava_vertices.push_back({v.x * inv_unit, v.y * inv_unit,
                                    v.base_z, lava.time_offset});
    }
    for (uint32_t idx : lava.mesh.indices) {
      mesh.lava_indices.push_back(base_idx + idx);
    }
  }

  SDL_Log("TerrainMesh: %zu lava vertices, %zu lava indices",
          mesh.lava_vertices.size(), mesh.lava_indices.size());

  for (const auto &line : contours.contour_lines) {
    float wx0 = line.x1 * inv_unit;
    float wy0 = line.y1 * inv_unit;
    float wx1 = line.x2 * inv_unit;
    float wy1 = line.y2 * inv_unit;
    float z   = line.elevation;
    mesh.contour_vertices.push_back({wx0, wy0, z});
    mesh.contour_vertices.push_back({wx1, wy1, z});
  }

  SDL_Log("TerrainMesh: %zu contour vertices (%zu lines)",
          mesh.contour_vertices.size(), contours.contour_lines.size());

  return mesh;
}

SceneUniforms compute_uniforms(const MapData &map_data,
                               const glm::mat4 &view, const glm::mat4 &projection,
                               uint32_t cluster_tiles_x, uint32_t cluster_tiles_y,
                               float time, float contour_opacity,
                               uint32_t light_count) {
  SceneUniforms u = {};

  u.view       = view;
  u.projection = projection;

  u.time             = time;
  u.contour_opacity  = contour_opacity;
  u.hex_border_width = 0.05f;

  color_to_float(Config::LAVA_COLOR, u.lava_color_r, u.lava_color_g, u.lava_color_b);

  u.star_light_r         = 0.55f;
  u.star_light_g         = 0.70f;
  u.star_light_b         = 1.00f;
  u.star_light_intensity = 0.12f;

  {
    float lx = -1.0f, ly = -1.0f, lz = 2.0f;
    float llen = std::sqrt(lx*lx + ly*ly + lz*lz);
    u.light_dir_x = lx / llen;
    u.light_dir_y = ly / llen;
    u.light_dir_z = lz / llen;
  }
  u.ambient     = 0.25f;
  u.light_col_r = 1.00f;
  u.light_col_g = 0.95f;
  u.light_col_b = 0.85f;

  u.tile_px       = 16.0f;
  u.grid_size_x   = (float)cluster_tiles_x;
  u.grid_size_y   = (float)cluster_tiles_y;
  u.num_slices    = 24.0f;
  u.near_plane    = -500.0f;
  u.far_plane     =  500.0f;
  u.light_count_f = (float)light_count;

  return u;
}