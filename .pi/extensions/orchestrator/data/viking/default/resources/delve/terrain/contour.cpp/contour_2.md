plateau.height = sum_height / count;
      plateau.center_x = sum_x / count;
      plateau.center_y = sum_y / count;

      if (count > 50) {
        int16_t plateau_id = (int16_t)(plateaus.size() + 1);
        for (int px_idx : plateau.pixels)
          terrain_map[px_idx] = plateau_id;
        plateaus.push_back(plateau);
      }
    }
  }

  SDL_Log("Detected %zu plateaus", plateaus.size());
  return plateaus;
}

void simplify_contours(std::vector<Line> &lines, float epsilon) {
  float eps2 = epsilon * epsilon;
  size_t before = lines.size();
  lines.erase(std::remove_if(lines.begin(), lines.end(), [eps2](const Line &l) {
    float dx = l.x2 - l.x1;
    float dy = l.y2 - l.y1;
    return (dx * dx + dy * dy) < eps2;
  }), lines.end());
  SDL_Log("simplify_contours: %zu -> %zu segments (epsilon=%.2f)",
          before, lines.size(), epsilon);
}