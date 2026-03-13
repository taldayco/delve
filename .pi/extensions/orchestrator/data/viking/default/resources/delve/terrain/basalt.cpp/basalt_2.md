for (auto [dq, dr] : neighbors) {
          HexCoord neighbor = {hc.q + dq, hc.r + dr};
          if (placed.find(neighbor) == placed.end()) {
            to_check.push(neighbor);
            placed[neighbor] = false;
          }
        }
      } else {
        placed[hc] = true;
      }
    }

    int added = columns.size() - columns_before;
    if (added > 0) {
      plateaus_with_columns_out.push_back(p);
      SDL_Log("  Added %d columns for plateau %zu", added, p);
    }
  }

  SDL_Log("Generated %zu columns for %zu plateaus", columns.size(),
          plateaus_with_columns_out.size());

  for (auto &col : columns) {
    for (int i = 0; i < 6; ++i) {
      col.visible_edges[i] = false;
      col.edge_drops[i] = 0.0f;
    }
  }

  compute_visible_edges(columns);
  return columns;
}

std::vector<HexColumn>
generate_basalt_columns_v2(MapData &data, float hex_size,
                           const WorleyBasaltParams &params) {
  int width = data.width;
  int height = data.height;
  std::vector<HexColumn> columns;




  HexCoord c0 = pixel_to_hex(0, 0, hex_size);
  HexCoord c1 = pixel_to_hex(width, 0, hex_size);
  HexCoord c2 = pixel_to_hex(0, height, hex_size);
  HexCoord c3 = pixel_to_hex(width, height, hex_size);

  int q_min = std::min({c0.q, c1.q, c2.q, c3.q}) - 2;
  int q_max = std::max({c0.q, c1.q, c2.q, c3.q}) + 2;
  int r_min = std::min({c0.r, c1.r, c2.r, c3.r}) - 2;
  int r_max = std::max({c0.r, c1.r, c2.r, c3.r}) + 2;

  for (int q = q_min; q <= q_max; ++q) {
    for (int r = r_min; r <= r_max; ++r) {
      float cx, cy;
      hex_to_pixel(q, r, hex_size, cx, cy);


      uint32_t hv = hash2d(q, r);
      float jx = ((hv & 0xFF) / 255.0f - 0.5f) * hex_size * 0.3f;
      float jy = (((hv >> 8) & 0xFF) / 255.0f - 0.5f) * hex_size * 0.3f;
      float sx = cx + jx;
      float sy = cy + jy;

      if (sx < 0 || sx >= width - 1 || sy < 0 || sy >= height - 1)
        continue;

      int px = (int)cx;
      int py = (int)cy;
      if (px < 0 || px >= width || py < 0 || py >= height)
        continue;


      float cell_val = sample_bilinear(data.worley_cell_value, width, height, sx, sy);
      if (cell_val < params.density_threshold)
        continue;


      int lx = std::clamp((int)sx, 0, width - 1);
      int ly = std::clamp((int)sy, 0, height - 1);
      if (data.liquid_mask[ly * width + lx])
        continue;

      float base_h = sample_bilinear(data.basalt_height, width, height, sx, sy);
      float h = base_h;


      h += cell_val * params.jitter_scale;

      columns.push_back({q, r, h, base_h});


      Vec2 corners[6];
      get_hex_corners(q, r, hex_size, corners);
      float fmin_x = 1e9f, fmax_x = -1e9f, fmin_y = 1e9f, fmax_y = -1e9f;
      for (int i = 0; i < 6; ++i) {
        fmin_x = std::min(fmin_x, corners[i].x);
        fmax_x = std::max(fmax_x, corners[i].x);
        fmin_y = std::min(fmin_y, corners[i].y);
        fmax_y = std::max(fmax_y, corners[i].y);
      }
      int x0 = std::max(0, (int)fmin_x - 1);
      int x1 = std::min(width - 1, (int)fmax_x + 1);
      int y0 = std::max(0, (int)fmin_y - 1);
      int y1 = std::min(height - 1, (int)fmax_y + 1);
      for (int ry = y0; ry <= y1; ++ry) {
        for (int rx = x0; rx <= x1; ++rx) {
          if (pixel_in_hex((float)rx, (float)ry, q, r, hex_size))
            data.terrain_map[ry * width + rx] = TERRAIN_BASALT;
        }
      }
    }
  }

  SDL_Log("generate_basalt_columns_v2: %zu columns", columns.size());

  for (auto &col : columns) {
    for (int i = 0; i < 6; ++i) {
      col.visible_edges[i] = false;
      col.edge_drops[i] = 0.0f;
    }
  }

  compute_visible_edges(columns);
  return columns;
}