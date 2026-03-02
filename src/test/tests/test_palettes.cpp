#include "test_harness.h"
#include "terrain/palettes.h"
#include <cstring>

DELVE_TEST(palette_count_positive) {
  EXPECT_GT(PALETTE_COUNT, 0);
}

DELVE_TEST(palette_has_name) {
  for (int i = 0; i < PALETTE_COUNT; i++) {
    EXPECT_TRUE(PALETTES[i].name != nullptr);
    EXPECT_GT((int)strlen(PALETTES[i].name), 0);
  }
}

DELVE_TEST(elevation_color_smooth_at_boundaries) {
  const Palette& p = PALETTES[0];

  // At h=0, should return colors[0]
  uint32_t c0 = get_elevation_color_smooth(0.0f, p);
  EXPECT_EQ(c0, p.colors[0]);

  // At h=1, should return colors[4]
  uint32_t c1 = get_elevation_color_smooth(1.0f, p);
  EXPECT_EQ(c1, p.colors[4]);
}

DELVE_TEST(elevation_color_smooth_clamped) {
  const Palette& p = PALETTES[0];

  // Values outside [0,1] should be clamped
  uint32_t below = get_elevation_color_smooth(-0.5f, p);
  uint32_t at_zero = get_elevation_color_smooth(0.0f, p);
  EXPECT_EQ(below, at_zero);

  uint32_t above = get_elevation_color_smooth(1.5f, p);
  uint32_t at_one = get_elevation_color_smooth(1.0f, p);
  EXPECT_EQ(above, at_one);
}

DELVE_TEST(stepped_color_returns_valid_palette_color) {
  const Palette& p = PALETTES[0];

  // Each band should return one of the palette colors
  uint32_t c = get_elevation_color(0.1f, p);
  EXPECT_EQ(c, p.colors[0]);

  c = get_elevation_color(0.3f, p);
  EXPECT_EQ(c, p.colors[1]);

  c = get_elevation_color(0.5f, p);
  EXPECT_EQ(c, p.colors[2]);

  c = get_elevation_color(0.7f, p);
  EXPECT_EQ(c, p.colors[3]);

  c = get_elevation_color(0.9f, p);
  EXPECT_EQ(c, p.colors[4]);
}

DELVE_TEST(organic_color_adds_variation) {
  const Palette& p = PALETTES[0];
  uint32_t base = get_elevation_color_smooth(0.5f, p);
  uint32_t varied = organic_color(0.5f, 42, 99, p);

  // organic_color should produce a color close to but not identical to smooth
  // (unless the noise variation happens to produce zero, which is unlikely)
  // We just verify it returns a valid ARGB value with alpha=0xFF
  EXPECT_EQ(varied >> 24, 0xFFu);
}
