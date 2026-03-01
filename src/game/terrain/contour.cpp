#include "terrain/contour.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <queue>

void extract_contours(std::span<const float> heightmap, int width, int height,
                      float interval, std::vector<Line> &out_lines,
                      std::vector<int> &out_band_map) {
  out_lines.clear();

  int total = width * height;
  out_band_map.resize(total);
  for (int i = 0; i < total; ++i) {
    out_band_map[i] = (int)(heightmap[i] / interval);
  }

  for (float level = interval * 0.5f; level < 1.0f; level += interval) {
    for (int y = 0; y < height - 1; ++y) {
      for (int x = 0; x < width - 1; ++x) {
        float h00 = heightmap[y * width + x];
        float h10 = heightmap[y * width + x + 1];
        float h01 = heightmap[(y + 1) * width + x];
        float h11 = heightmap[(y + 1) * width + x + 1];

        int config = ((h00 >= level) << 0) | ((h10 >= level) << 1) |
                     ((h11 >= level) << 2) | ((h01 >= level) << 3);

        if (config == 0 || config == 15)
          continue;

        float fx = (float)x, fy = (float)y;
        float points[4][2];
        int point_count = 0;

        if ((h00 < level && h10 >= level) || (h00 >= level && h10 < level)) {
          float t = (level - h00) / (h10 - h00);
          points[point_count][0] = fx + t;
          points[point_count][1] = fy;
          point_count++;
        }

        if ((h10 < level && h11 >= level) || (h10 >= level && h11 < level)) {
          float t = (level - h10) / (h11 - h10);
          points[point_count][0] = fx + 1;
          points[point_count][1] = fy + t;
          point_count++;
        }

        if ((h11 < level && h01 >= level) || (h11 >= level && h01 < level)) {
          float t = (level - h11) / (h01 - h11);
          points[point_count][0] = fx + 1 - t;
          points[point_count][1] = fy + 1;
          point_count++;
        }

        if ((h01 < level && h00 >= level) || (h01 >= level && h00 < level)) {
          float t = (level - h01) / (h00 - h01);
          points[point_count][0] = fx;
          points[point_count][1] = fy + 1 - t;
          point_count++;
        }

        if (point_count == 2) {
          out_lines.push_back(
              {points[0][0], points[0][1], points[1][0], points[1][1], level});
        } else if (point_count == 4) {
          float center = (h00 + h10 + h11 + h01) * 0.25f;
          if (center >= level) {
            out_lines.push_back({points[0][0], points[0][1], points[1][0],
                                 points[1][1], level});
            out_lines.push_back({points[2][0], points[2][1], points[3][0],
                                 points[3][1], level});
          } else {
            out_lines.push_back({points[0][0], points[0][1], points[3][0],
                                 points[3][1], level});
            out_lines.push_back({points[1][0], points[1][1], points[2][0],
                                 points[2][1], level});
          }
        }
      }
    }
  }
}

std::vector<Plateau> detect_plateaus(std::span<const int> band_map,
                                     std::span<const float> heightmap,
                                     int width, int height,
                                     std::vector<int16_t>& terrain_map) {

  std::vector<bool> visited(width * height, false);
  std::vector<Plateau> plateaus;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = y * width + x;
      if (visited[idx])
        continue;

      int band = band_map[idx];
      Plateau plateau;
      plateau.min_x = (float)x;
      plateau.max_x = (float)x;
      plateau.min_y = (float)y;
      plateau.max_y = (float)y;

      std::queue<int> queue;
      queue.push(idx);
      visited[idx] = true;

      float sum_x = 0, sum_y = 0;
      float sum_height = 0;
      int count = 0;

      while (!queue.empty()) {
        int current = queue.front();
        queue.pop();

        plateau.pixels.push_back(current);

        int cx = current % width;
        int cy = current / width;
        sum_x += cx;
        sum_y += cy;
        sum_height += heightmap[current];
        count++;

        plateau.min_x = std::min(plateau.min_x, (float)cx);
        plateau.max_x = std::max(plateau.max_x, (float)cx);
        plateau.min_y = std::min(plateau.min_y, (float)cy);
        plateau.max_y = std::max(plateau.max_y, (float)cy);

        int neighbors[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        for (auto [dx, dy] : neighbors) {
          int nx = cx + dx;
          int ny = cy + dy;

          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] && band_map[nidx] == band) {
              visited[nidx] = true;
              queue.push(nidx);
            }
          }
        }
      }

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
