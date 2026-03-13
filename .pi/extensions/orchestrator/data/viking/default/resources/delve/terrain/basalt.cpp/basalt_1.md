#include "terrain/basalt.h"
#include "terrain/map_data.h"
#include "terrain/palettes.h"
#include "core/types.h"
#include "terrain/util.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <vector>


static float sample_bilinear(const std::vector<float> &map, int width,
                             int height, float fx, float fy) {
  float x = std::max(0.0f, std::min(fx, (float)(width - 1)));
  float y = std::max(0.0f, std::min(fy, (float)(height - 1)));

  int x0 = (int)x;
  int y0 = (int)y;
  int x1 = std::min(x0 + 1, width - 1);
  int y1 = std::min(y0 + 1, height - 1);

  float tx = x - x0;
  float ty = y - y0;

  float v00 = map[y0 * width + x0];
  float v10 = map[y0 * width + x1];
  float v01 = map[y1 * width + x0];
  float v11 = map[y1 * width + x1];

  return v00 * (1 - tx) * (1 - ty) + v10 * tx * (1 - ty) +
         v01 * (1 - tx) * ty + v11 * tx * ty;
}

static bool hex_fits_in_plateau(int q, int r, float hex_size,
                                std::span<const int16_t> terrain_map,
                                int16_t plateau_id,
                                int width, int height) {
  float cx, cy;
  hex_to_pixel(q, r, hex_size, cx, cy);

  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      int px = (int)cx + dx;
      int py = (int)cy + dy;

      if (px >= 0 && px < width && py >= 0 && py < height) {
        int idx = py * width + px;
        if (terrain_map[idx] == plateau_id) {
          return true;
        }
      }
    }
  }

  return false;
}

std::vector<HexColumn>
generate_basalt_columns(std::span<const float> heightmap, int width, int height,
                        float hex_size,
                        const std::vector<Plateau> &plateaus,
                        std::vector<int> &plateaus_with_columns_out,
                        std::vector<int16_t> &terrain_map) {
  std::vector<HexColumn> columns;

  SDL_Log("Starting column generation with hex_size=%.2f", hex_size);

  plateaus_with_columns_out.clear();

  for (size_t p = 0; p < plateaus.size(); ++p) {
    const auto &plateau = plateaus[p];
    int16_t plateau_id = (int16_t)(p + 1);

    if (plateau.pixels.size() < 300) {
      SDL_Log("  Skipping small plateau %zu (size=%zu)", p,
              plateau.pixels.size());
      continue;
    }

    float w = plateau.max_x - plateau.min_x + 1;
    float h = plateau.max_y - plateau.min_y + 1;
    float aspect_ratio = std::max(w / h, h / w);

    if (aspect_ratio > 3.0f) {
      SDL_Log("  Skipping elongated plateau %zu (aspect=%.2f)", p,
              aspect_ratio);
      continue;
    }

    HexCoord center =
        pixel_to_hex(plateau.center_x, plateau.center_y, hex_size);

    float cx, cy;
    hex_to_pixel(center.q, center.r, hex_size, cx, cy);

    bool fits = hex_fits_in_plateau(center.q, center.r, hex_size, terrain_map,
                                    plateau_id, width, height);

    if (!fits) {
      continue;
    }

    int columns_before = columns.size();

    std::unordered_map<HexCoord, bool, HexHash> placed;
    std::queue<HexCoord> to_check;
    to_check.push(center);
    placed[center] = false;

    const int neighbors[6][2] = {{1, 0},  {0, 1},  {-1, 1},
                                 {-1, 0}, {0, -1}, {1, -1}};

    while (!to_check.empty()) {
      HexCoord hc = to_check.front();
      to_check.pop();

      if (placed[hc])
        continue;

      if (hex_fits_in_plateau(hc.q, hc.r, hex_size, terrain_map,
                              plateau_id, width, height)) {
        placed[hc] = true;

        uint32_t h = hash2d(hc.q, hc.r);
        float variation = ((h & 0xFF) / 255.0f - 0.5f) * 0.05f;

        columns.push_back(
            {hc.q, hc.r, plateau.height + variation, plateau.height});

        Vec2 corners[6];
        get_hex_corners(hc.q, hc.r, hex_size, corners);
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
            if (pixel_in_hex((float)rx, (float)ry, hc.q, hc.r, hex_size))
              terrain_map[ry * width + rx] = TERRAIN_BASALT;
          }
        }