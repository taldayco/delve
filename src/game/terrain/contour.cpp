#include "terrain/contour.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <queue>

void extract_contours(std::span<const float> heightmap, int width, int height,
                      float interval, std::vector<Line> &out_lines,
                      std::vector<int> &out_band_map) {
  out_lines.clear();

  constexpr size_t MAX_CONTOUR_LINES = 500'000;

  int total = width * height;
  out_band_map.resize(total);
  for (int i = 0; i < total; ++i) {
    out_band_map[i] = (int)(heightmap[i] / interval);
  }

  for (float level = interval * 0.5f; level < 1.0f; level += interval) {
    for (int y = 0; y < height - 1; ++y) {
      for (int x = 0; x < width - 1; ++x) {
        if (out_lines.size() >= MAX_CONTOUR_LINES) {
          SDL_Log("extract_contours: hit %zu line cap, truncating",
                  MAX_CONTOUR_LINES);
          return;
        }

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

void simplify_contours(std::vector<Line> &lines, float epsilon) {
  float eps2 = epsilon * epsilon;
  size_t before = lines.size();
  lines.erase(std::remove_if(lines.begin(), lines.end(),
                             [eps2](const Line &l) {
                               float dx = l.x2 - l.x1;
                               float dy = l.y2 - l.y1;
                               return (dx * dx + dy * dy) < eps2;
                             }),
              lines.end());
  SDL_Log("simplify_contours: %zu -> %zu segments (epsilon=%.2f)", before,
          lines.size(), epsilon);
}
