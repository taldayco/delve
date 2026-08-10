#include "test_harness.h"
#include "terrain/map_data.h"
#include "terrain/terrain_lighting.h"

#include <cmath>
#include <cstdint>
#include <vector>

static constexpr float TL_PI = 3.14159265f;

static MapData make_ground_map(int w, int h, float ground) {
  MapData map;
  map.width = w;
  map.height = h;
  map.basalt_height.assign(w * h, ground);
  return map;
}

static uint8_t sun_at(const TerrainLightBake &b, int x, int y) {
  return b.rg[(size_t)(y * b.width + x) * 2 + 0];
}

static uint8_t sky_at(const TerrainLightBake &b, int x, int y) {
  return b.rg[(size_t)(y * b.width + x) * 2 + 1];
}

// ---- flat field -----------------------------------------------------------

DELVE_TEST(terrain_light_flat_field_fully_lit) {
  auto map = make_ground_map(64, 64, 0.3f);
  auto bake = bake_terrain_lighting(map);
  EXPECT_EQ(bake.width, 64);
  EXPECT_EQ(bake.height, 64);
  EXPECT_EQ(bake.rg.size(), (size_t)(64 * 64 * 2));
  for (int y = 4; y < 60; ++y) {
    for (int x = 4; x < 60; ++x) {
      EXPECT_EQ((int)sun_at(bake, x, y), 255);
      EXPECT_EQ((int)sky_at(bake, x, y), 255);
    }
  }
  return true;
}

// ---- wall: shadow orientation + analytic shadow length --------------------

DELVE_TEST(terrain_light_wall_shadows_toward_plus_xy) {
  const int W = 192, Hm = 192;
  const float wall_h = 0.9f;
  auto map = make_ground_map(W, Hm, 0.0f);
  for (int y = 0; y < Hm; ++y)
    for (int x = 20; x <= 22; ++x)
      map.basalt_height[y * W + x] = wall_h;

  TerrainLightParams P;
  auto bake = bake_terrain_lighting(map, P);

  const float elev = P.sun_elevation_deg * TL_PI / 180.0f;
  const float h_vis = wall_h * P.height_scale;   // exaggerated wall height
  const float L = h_vis / std::tan(elev);        // analytic shadow length, world units
  auto diag_px = [&](float t_world) {
    return (int)std::round(t_world * P.pixels_per_unit / std::sqrt(2.0f));
  };

  // Sunward (-x-y) side of the wall: fully lit.
  EXPECT_EQ((int)sun_at(bake, 10, 96), 255);

  // Deep inside the shadow, 0.5 * L behind the wall on the +x+y side.
  int k_mid = diag_px(0.5f * L);
  EXPECT_LT((int)sun_at(bake, 22 + k_mid, 96 + k_mid), 30);

  // Beyond 1.5 * L the shadow has ended.
  int k_out = diag_px(1.5f * L);
  EXPECT_GT((int)sun_at(bake, 22 + k_out, 96 + k_out), 220);
  return true;
}

// ---- hex column stamp feeds the bake --------------------------------------

DELVE_TEST(terrain_light_column_stamp_casts_shadow) {
  const int W = 192, Hm = 192;
  auto map = make_ground_map(W, Hm, 0.0f);
  HexColumn col{};
  col.q = 8;
  col.r = 4;
  col.height = 0.9f;
  map.columns.push_back(col);

  auto bake = bake_terrain_lighting(map);

  // hex_to_pixel(8, 4, 8) -> center (96, ~110.8).
  EXPECT_LT((int)sun_at(bake, 116, 131), 30);  // +x+y side: shadowed
  EXPECT_EQ((int)sun_at(bake, 76, 91), 255);   // -x-y side: lit
  return true;
}

// ---- pit: sky visibility --------------------------------------------------

DELVE_TEST(terrain_light_pit_floor_darker_sky) {
  auto map = make_ground_map(64, 64, 0.5f);
  for (int y = 24; y < 40; ++y)
    for (int x = 24; x < 40; ++x)
      map.basalt_height[y * 64 + x] = 0.1f;

  auto bake = bake_terrain_lighting(map);
  EXPECT_LT((int)sky_at(bake, 32, 32), 240);  // pit floor sees less sky
  EXPECT_GE((int)sky_at(bake, 8, 8), 250);    // open flat ground ~fully open
  return true;
}

// ---- sweep vs brute force -------------------------------------------------

static void brute_horizon(const std::vector<float> &H, int w, int h,
                          int dx, int dy, float step,
                          std::vector<float> &out) {
  out.assign((size_t)w * h, TERRAIN_HORIZON_NONE);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float best = TERRAIN_HORIZON_NONE;
      for (int k = 1;; ++k) {
        int px = x - k * dx, py = y - k * dy;
        if (px < 0 || px >= w || py < 0 || py >= h)
          break;
        float ang = std::atan2(H[py * w + px] - H[y * w + x], (float)k * step);
        best = std::max(best, ang);
      }
      out[y * w + x] = best;
    }
  }
}

DELVE_TEST(terrain_light_sweep_matches_bruteforce) {
  const int W = 64, Hm = 64;
  std::vector<float> H((size_t)W * Hm);
  uint32_t s = 12345u;
  for (auto &v : H) {
    s = s * 1664525u + 1013904223u;
    v = (float)(s >> 8) / 16777216.0f * 12.5f;
  }

  const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                          {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
  std::vector<float> fast, ref;
  for (const auto &d : dirs) {
    bool diagonal = d[0] != 0 && d[1] != 0;
    float step = (diagonal ? std::sqrt(2.0f) : 1.0f) / 8.0f;
    sweep_horizon(H, W, Hm, d[0], d[1], step, fast);
    brute_horizon(H, W, Hm, d[0], d[1], step, ref);
    for (size_t i = 0; i < fast.size(); ++i)
      EXPECT_NEAR(fast[i], ref[i], 1e-4f);
  }
  return true;
}

// ---- determinism ----------------------------------------------------------

DELVE_TEST(terrain_light_bake_deterministic) {
  auto map = make_ground_map(96, 96, 0.2f);
  uint32_t s = 777u;
  for (auto &v : map.basalt_height) {
    s = s * 1664525u + 1013904223u;
    v = (float)(s >> 8) / 16777216.0f * 0.9f;
  }
  HexColumn col{};
  col.q = 4;
  col.r = 3;
  col.height = 0.8f;
  map.columns.push_back(col);

  auto a = bake_terrain_lighting(map);
  auto b = bake_terrain_lighting(map);
  EXPECT_EQ(a.width, b.width);
  EXPECT_EQ(a.height, b.height);
  EXPECT_TRUE(a.rg == b.rg);
  return true;
}
