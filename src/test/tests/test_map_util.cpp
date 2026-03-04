#include "test_harness.h"
#include "terrain/map_util.h"
#include "terrain/map_data.h"
#include "terrain/hex.h"
#include "config.h"
#include <cmath>

static MapData make_flat_map(int w, int h, float height = 0.5f) {
  MapData md;
  md.allocate(w, h);
  for (auto &v : md.basalt_height) v = height;
  return md;
}

DELVE_TEST(sample_world_height_flat_map) {
  auto md = make_flat_map(64, 64, 0.5f);
  float v = sample_world_height(md, 4.0f, 4.0f);
  EXPECT_NEAR(v, 0.5f, 1e-4f);
  return true;
}

DELVE_TEST(sample_world_height_empty_returns_zero) {
  MapData md;
  md.allocate(64, 64);
  md.basalt_height.clear(); // empty
  float v = sample_world_height(md, 4.0f, 4.0f);
  EXPECT_NEAR(v, 0.0f, 1e-6f);
  return true;
}

DELVE_TEST(sample_world_height_out_of_bounds_clamped) {
  auto md = make_flat_map(64, 64, 0.75f);
  // Out-of-bounds world coords should clamp, not crash
  float v1 = sample_world_height(md, -999.0f, -999.0f);
  float v2 = sample_world_height(md, 9999.0f, 9999.0f);
  EXPECT_NEAR(v1, 0.75f, 1e-4f);
  EXPECT_NEAR(v2, 0.75f, 1e-4f);
  return true;
}

DELVE_TEST(sample_world_height_gradient) {
  // Map with linear gradient: height = x / (w-1)
  int w = 64, h = 64;
  MapData md;
  md.allocate(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      md.basalt_height[y * w + x] = (float)x / (w - 1);

  float left  = sample_world_height(md, 0.0f, 4.0f);
  float right = sample_world_height(md, (float)(w - 1) / Config::HEX_SIZE, 4.0f);
  EXPECT_RANGE(left,  0.0f, 0.1f);
  EXPECT_RANGE(right, 0.9f, 1.0f);
  return true;
}

DELVE_TEST(find_spawn_column_empty_returns_default) {
  MapData md;
  md.allocate(64, 64);
  // columns is empty
  HexColumn col = find_spawn_column(md, 42);
  EXPECT_EQ(col.q, 0);
  EXPECT_EQ(col.r, 0);
  return true;
}

DELVE_TEST(find_spawn_column_single_returns_it) {
  MapData md;
  md.allocate(64, 64);
  md.columns.push_back({3, 5, 0.5f, 0.5f, {}, {}});
  HexColumn col = find_spawn_column(md, 42);
  EXPECT_EQ(col.q, 3);
  EXPECT_EQ(col.r, 5);
  return true;
}

DELVE_TEST(find_spawn_column_returns_valid_from_set) {
  MapData md;
  md.allocate(64, 64);
  // Add a cluster of connected hex columns
  const int dx[6] = {1, 0, -1, -1, 0, 1};
  const int dy[6] = {0, 1, 1, 0, -1, -1};
  md.columns.push_back({0, 0, 0.5f, 0.5f, {}, {}});
  for (int d = 0; d < 6; ++d)
    md.columns.push_back({dx[d], dy[d], 0.5f, 0.5f, {}, {}});

  // Should return one of these columns
  HexColumn col = find_spawn_column(md, 123);
  bool found = false;
  for (const auto &c : md.columns)
    if (c.q == col.q && c.r == col.r) { found = true; break; }
  EXPECT_TRUE(found);
  return true;
}

DELVE_TEST(find_spawn_column_deterministic_with_same_seed) {
  MapData md;
  md.allocate(64, 64);
  for (int q = -3; q <= 3; ++q)
    for (int r = -3; r <= 3; ++r)
      md.columns.push_back({q, r, 0.5f, 0.5f, {}, {}});

  HexColumn a = find_spawn_column(md, 77);
  HexColumn b = find_spawn_column(md, 77);
  EXPECT_EQ(a.q, b.q);
  EXPECT_EQ(a.r, b.r);
  return true;
}
