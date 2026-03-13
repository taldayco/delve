if ((int)component_pixels.size() >= MAX_COMPONENT_PIXELS) {
          capped = true;
          break;
        }

        int cx = idx % width, cy = idx / width;

        mn_x = std::min(mn_x, (float)cx);
        mx_x = std::max(mx_x, (float)cx);
        mn_y = std::min(mn_y, (float)cy);
        mx_y = std::max(mx_y, (float)cy);

        for (auto [dx, dy] : dirs) {
          int nx = cx + dx, ny = cy + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] && data.terrain_map[nidx] != TERRAIN_BASALT) {
              visited[nidx] = true;
              q.push(nidx);
            }
          }
        }
      }

      // If capped, drain remaining BFS queue (mark visited) and skip body.
      if (capped) {
        while (!q.empty()) {
          visited[q.front()] = true;
          q.pop();
        }
        continue;
      }

      if (component_pixels.size() < 50)
        continue;

      // Check global pixel budget before creating a new body.
      if (total_pixels_used + (int)component_pixels.size() > TOTAL_PIXEL_BUDGET) {
        SDL_Log("generate_lava_and_void: pixel budget exhausted (%d used, component %zu would exceed %d)",
                total_pixels_used, component_pixels.size(), TOTAL_PIXEL_BUDGET);
        budget_exceeded = true;
        continue;
      }

      bool is_void = dist(rng) < void_chance;
      int16_t terrain_type = is_void ? TERRAIN_VOID : TERRAIN_LAVA;

      LavaBody body;
      body.plateau_index = -1;
      body.height = 0.0f;
      body.min_x = mn_x;
      body.max_x = mx_x;
      body.min_y = mn_y;
      body.max_y = mx_y;
      float bw = mx_x - mn_x + 1.f, bh = mx_y - mn_y + 1.f;
      body.aspect_ratio = std::max(bw, bh) / std::max(1.0f, std::min(bw, bh));
      body.pixels = std::move(component_pixels);
      body.time_offset =
          (hash1d(body_index++) % 1000) / 1000.0f * 6.283185f;

      if (!is_void) {
        generate_lava_grid_mesh(body, width, height, 2.0f);
      }

      for (int idx : body.pixels) {
        if (data.terrain_map[idx] != TERRAIN_BASALT)
          data.terrain_map[idx] = terrain_type;
      }

      total_pixels_used += (int)body.pixels.size();

      // Pixels are no longer needed after terrain_map stamping — free them.
      body.pixels.clear();
      body.pixels.shrink_to_fit();

      if (is_void)
        result.void_bodies.push_back(std::move(body));
      else
        result.lava_bodies.push_back(std::move(body));
    }
  }

  SDL_Log("generate_lava_and_void: %zu lava bodies, %zu void bodies, %d total pixels used",
          result.lava_bodies.size(), result.void_bodies.size(), total_pixels_used);
  return result;
}