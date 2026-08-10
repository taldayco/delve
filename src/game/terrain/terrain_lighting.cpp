#include "terrain/terrain_lighting.h"
#include "terrain/hex.h"
#include "terrain/map_data.h"
#include <algorithm>
#include <cmath>

static std::vector<float> rasterize_heights(const MapData &map, float hex_size,
                                            float height_scale) {
  const int w = map.width, h = map.height;
  std::vector<float> H(map.basalt_height.begin(), map.basalt_height.end());

  for (const auto &col : map.columns) {
    Vec2 corners[6];
    get_hex_corners(col.q, col.r, hex_size, corners);
    float fmin_x = 1e9f, fmax_x = -1e9f, fmin_y = 1e9f, fmax_y = -1e9f;
    for (int i = 0; i < 6; ++i) {
      fmin_x = std::min(fmin_x, corners[i].x);
      fmax_x = std::max(fmax_x, corners[i].x);
      fmin_y = std::min(fmin_y, corners[i].y);
      fmax_y = std::max(fmax_y, corners[i].y);
    }
    int x0 = std::max(0, (int)fmin_x - 1);
    int x1 = std::min(w - 1, (int)fmax_x + 1);
    int y0 = std::max(0, (int)fmin_y - 1);
    int y1 = std::min(h - 1, (int)fmax_y + 1);
    for (int ry = y0; ry <= y1; ++ry) {
      for (int rx = x0; rx <= x1; ++rx) {
        if (pixel_in_hex((float)rx, (float)ry, col.q, col.r, hex_size))
          H[ry * w + rx] = col.height;
      }
    }
  }

  for (auto &v : H)
    v *= height_scale;
  return H;
}

void sweep_horizon(const std::vector<float> &heights, int width, int height,
                   int step_dx, int step_dy, float step_world_units,
                   std::vector<float> &out_angles) {
  out_angles.assign((size_t)width * height, TERRAIN_HORIZON_NONE);

  std::vector<float> hull_t, hull_h;

  auto run_line = [&](int sx, int sy) {
    hull_t.clear();
    hull_h.clear();
    float t = 0.0f;
    for (int x = sx, y = sy; x >= 0 && x < width && y >= 0 && y < height;
         x += step_dx, y += step_dy, t += step_world_units) {
      float hz = heights[y * width + x];

      while (hull_t.size() >= 2) {
        size_t n = hull_t.size();
        float cross =
            (hull_t[n - 1] - hull_t[n - 2]) * (hz - hull_h[n - 2]) -
            (hull_h[n - 1] - hull_h[n - 2]) * (t - hull_t[n - 2]);
        if (cross < 0.0f)
          break;
        hull_t.pop_back();
        hull_h.pop_back();
      }

      if (!hull_t.empty())
        out_angles[y * width + x] =
            std::atan2(hull_h.back() - hz, t - hull_t.back());

      hull_t.push_back(t);
      hull_h.push_back(hz);
    }
  };

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int px = x - step_dx, py = y - step_dy;
      if (px < 0 || px >= width || py < 0 || py >= height)
        run_line(x, y);
    }
  }
}

TerrainLightBake bake_terrain_lighting(const MapData &map,
                                       const TerrainLightParams &params) {
  TerrainLightBake bake;
  bake.width = map.width;
  bake.height = map.height;
  const int w = map.width, h = map.height;
  const size_t n = (size_t)w * h;
  bake.rg.assign(n * 2, 255);
  if (n == 0)
    return bake;

  const float hex_size = params.pixels_per_unit;
  std::vector<float> H = rasterize_heights(map, hex_size, params.height_scale);

  const float axis_step = 1.0f / params.pixels_per_unit;
  const float diag_step = std::sqrt(2.0f) / params.pixels_per_unit;
  const float deg_to_rad = 3.14159265f / 180.0f;
  const float sun_lo = (params.sun_elevation_deg - params.penumbra_deg) * deg_to_rad;
  const float sun_hi = (params.sun_elevation_deg + params.penumbra_deg) * deg_to_rad;

  const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                          {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};

  std::vector<float> angles;
  std::vector<float> sky(n, 0.0f);
  for (const auto &d : dirs) {
    bool diagonal = d[0] != 0 && d[1] != 0;
    sweep_horizon(H, w, h, d[0], d[1], diagonal ? diag_step : axis_step, angles);

    for (size_t i = 0; i < n; ++i)
      sky[i] += std::clamp(1.0f - std::sin(angles[i]), 0.0f, 1.0f);

    if (d[0] == 1 && d[1] == 1) {

      for (size_t i = 0; i < n; ++i) {
        float vis;
        if (sun_hi > sun_lo) {
          float s = std::clamp((angles[i] - sun_lo) / (sun_hi - sun_lo), 0.0f, 1.0f);
          vis = 1.0f - s * s * (3.0f - 2.0f * s);
        } else {
          vis = angles[i] < sun_lo ? 1.0f : 0.0f;
        }
        bake.rg[i * 2 + 0] = (uint8_t)std::lround(vis * 255.0f);
      }
    }
  }

  for (size_t i = 0; i < n; ++i) {
    float sky_vis = std::clamp(sky[i] * (1.0f / 8.0f), 0.0f, 1.0f);
    bake.rg[i * 2 + 1] = (uint8_t)std::lround(sky_vis * 255.0f);
  }
  return bake;
}
