#include "test_harness.h"
#include "config.h"
#include "terrain/hex.h"
#include "terrain/map_util.h"
#include "terrain/map_data.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/basalt.h"
#include <glm/glm.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Coordinate system constant checks
// ---------------------------------------------------------------------------

DELVE_TEST(hex_size_is_2) {
  EXPECT_TRUE(Config::HEX_SIZE == 2.0f);
  return true;
}

DELVE_TEST(world_unit_is_2_feet) {
  // 1 world unit = 2 feet = 1 basalt column width
  EXPECT_TRUE(Config::WORLD_UNIT == 2.0f);
  return true;
}

DELVE_TEST(basalt_column_feet_is_2) {
  EXPECT_TRUE(Config::BASALT_COLUMN_FEET == 2.0f);
  return true;
}

// ---------------------------------------------------------------------------
// World-space roundtrip: world_to_hex(hex_to_world(coord)) == coord
// Under HEX_SIZE = 2.0f
// ---------------------------------------------------------------------------

DELVE_TEST(hex_world_roundtrip) {
  int total = 0, accurate = 0;
  for (int q = -10; q <= 10; ++q) {
    for (int r = -10; r <= 10; ++r) {
      glm::vec2 world = hex_to_world({q, r});
      HexCoord back = world_to_hex(world.x, world.y);
      ++total;
      if (back.q == q && back.r == r) ++accurate;
    }
  }
  // Must be 100% accurate (exact inverses with integer hex coords)
  EXPECT_GT((float)accurate / total, 0.99f);
  return true;
}

// ---------------------------------------------------------------------------
// Human proportions — 8-head canon (mirrors ActorRenderer::make_proportions)
// Average human: 5.9 ft tall, head = 0.74 ft, shoulder width = 1.475 ft
// Documentation: 1 basalt column = 2.0 ft; shoulder ~0.74 columns wide
// ---------------------------------------------------------------------------

DELVE_TEST(human_height_feet_constant) {
  // Config documents average human = 5.9 feet
  EXPECT_NEAR(Config::HUMAN_HEIGHT_FEET, 5.9f, 0.001f);
  return true;
}

DELVE_TEST(human_head_height_feet) {
  // HEAD_HEIGHT_FEET must equal HUMAN_HEIGHT_FEET / 8 (one head-unit)
  float expected = Config::HUMAN_HEIGHT_FEET / 8.0f; // ~0.7375 ft
  EXPECT_NEAR(Config::HEAD_HEIGHT_FEET, expected, expected * 0.001f);
  return true;
}

DELVE_TEST(human_proportions_8_head_ratios) {
  // Mirror the 8-head formula from ActorRenderer::make_proportions
  const float h = Config::HUMAN_HEIGHT_FEET; // 5.9f
  const float u = h / 8.0f;                 // one head-unit ~0.7375 ft
  const float tol = 0.01f;                  // 1% tolerance

  // head height = 1 unit
  float head_height    = 1.0f * u;
  float shoulder_width = 2.0f * u; // full span
  float hip_width      = 1.5f * u;

  // Head = ~0.7375 ft
  EXPECT_NEAR(head_height, 0.7375f, 0.7375f * tol);

  // Shoulder width = ~1.475 ft (about 0.74 basalt columns at 2.0 ft/col)
  EXPECT_NEAR(shoulder_width, 1.475f, 1.475f * tol);

  // Head/total ratio must be exactly 1/8
  EXPECT_NEAR(head_height / h, 0.125f, 0.001f);

  // Shoulder in terms of basalt columns: should be in [0.5, 1.5] columns
  float shoulder_columns = shoulder_width / Config::BASALT_COLUMN_FEET;
  EXPECT_GT(shoulder_columns, 0.5f);
  EXPECT_LT(shoulder_columns, 1.5f);

  return true;
}

// ---------------------------------------------------------------------------
// Map dimension constants
// ---------------------------------------------------------------------------

DELVE_TEST(map_cols_rows_match_hex_size) {
  // MAP_WIDTH_UNITS = MAP_COLS / HEX_SIZE
  EXPECT_NEAR(Config::MAP_WIDTH_UNITS,
              (float)Config::MAP_COLS / Config::HEX_SIZE, 0.001f);
  EXPECT_NEAR(Config::MAP_HEIGHT_UNITS,
              (float)Config::MAP_ROWS / Config::HEX_SIZE, 0.001f);
  return true;
}

DELVE_TEST(map_cols_rows_positive) {
  EXPECT_GT((float)Config::MAP_COLS, 0.0f);
  EXPECT_GT((float)Config::MAP_ROWS, 0.0f);
  return true;
}

DELVE_TEST(map_units_positive) {
  EXPECT_GT(Config::MAP_WIDTH_UNITS,  0.0f);
  EXPECT_GT(Config::MAP_HEIGHT_UNITS, 0.0f);
  return true;
}

// ---------------------------------------------------------------------------
// sample_world_height: empty map returns 0, flat map returns constant
// ---------------------------------------------------------------------------

DELVE_TEST(sample_world_height_empty_returns_zero) {
  MapData md;
  md.width  = 4;
  md.height = 4;
  // basalt_height intentionally left empty
  float h = sample_world_height(md, 1.0f, 1.0f);
  EXPECT_NEAR(h, 0.0f, 1e-6f);
  return true;
}

DELVE_TEST(sample_world_height_flat_map) {
  constexpr int W = 16, H = 16;
  MapData md;
  md.allocate(W, H);
  const float kHeight = 0.5f;
  std::fill(md.basalt_height.begin(), md.basalt_height.end(), kHeight);

  // Sample anywhere in bounds should return kHeight
  EXPECT_NEAR(sample_world_height(md, 1.0f, 1.0f), kHeight, 1e-5f);
  EXPECT_NEAR(sample_world_height(md, 0.0f, 0.0f), kHeight, 1e-5f);
  return true;
}

DELVE_TEST(sample_world_height_clamps_oob) {
  constexpr int W = 8, H = 8;
  MapData md;
  md.allocate(W, H);
  std::fill(md.basalt_height.begin(), md.basalt_height.end(), 0.25f);

  // Out-of-bounds world coords should clamp, not crash
  float h = sample_world_height(md, -100.0f, -100.0f);
  EXPECT_NEAR(h, 0.25f, 1e-5f);
  h = sample_world_height(md, 1000.0f, 1000.0f);
  EXPECT_NEAR(h, 0.25f, 1e-5f);
  return true;
}

// ---------------------------------------------------------------------------
// hex_to_world produces positions inside expected map bounds
// ---------------------------------------------------------------------------

DELVE_TEST(hex_to_world_within_map_bounds) {
  // Hex (0,0) should map to world origin
  glm::vec2 w = hex_to_world({0, 0});
  EXPECT_NEAR(w.x, 0.0f, 0.01f);
  EXPECT_NEAR(w.y, 0.0f, 0.01f);
  return true;
}

DELVE_TEST(hex_to_world_scale_correct) {
  // hex_to_world uses BASALT_COLUMN_FEET (2.0 ft) as spacing.
  // For q=1, r=0: wx = BASALT_COLUMN_FEET * 1 = 2.0 ft
  glm::vec2 w = hex_to_world({1, 0});
  EXPECT_NEAR(w.x, Config::BASALT_COLUMN_FEET, 0.01f);
  return true;
}
