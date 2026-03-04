#include "test_harness.h"
#include "terrain/palettes.h"

DELVE_TEST(palette_count_positive) {
  EXPECT_GT((float)PALETTE_COUNT, 0.0f);
  return true;
}

DELVE_TEST(palette_has_name) {
  for (int i = 0; i < PALETTE_COUNT; ++i) {
    EXPECT_TRUE(PALETTES[i].name != nullptr);
    EXPECT_TRUE(PALETTES[i].name[0] != '\0');
  }
  return true;
}

DELVE_TEST(elevation_color_smooth_at_boundaries) {
  const Palette &p = PALETTES[0];
  uint32_t at_0 = get_elevation_color_smooth(0.0f, p);
  uint32_t at_1 = get_elevation_color_smooth(1.0f, p);
  EXPECT_EQ(at_0, p.colors[0]);
  EXPECT_EQ(at_1, p.colors[4]);
  return true;
}

DELVE_TEST(elevation_color_smooth_clamped) {
  const Palette &p = PALETTES[0];
  uint32_t below = get_elevation_color_smooth(-1.0f, p);
  uint32_t above = get_elevation_color_smooth(2.0f, p);
  EXPECT_EQ(below, get_elevation_color_smooth(0.0f, p));
  EXPECT_EQ(above, get_elevation_color_smooth(1.0f, p));
  return true;
}

DELVE_TEST(stepped_color_returns_valid_palette_color) {
  const Palette &p = PALETTES[0];
  float bands[] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
  uint32_t expected[] = {p.colors[0], p.colors[1], p.colors[2], p.colors[3], p.colors[4]};
  for (int i = 0; i < 5; ++i) {
    uint32_t c = get_elevation_color(bands[i], p);
    EXPECT_EQ(c, expected[i]);
  }
  return true;
}

DELVE_TEST(organic_color_adds_variation) {
  const Palette &p = PALETTES[0];
  uint32_t c = organic_color(0.5f, 100, 200, p);
  EXPECT_EQ(c & 0xFF000000, (uint32_t)0xFF000000); // alpha = 0xFF
  return true;
}
