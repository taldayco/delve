#include "test_harness.h"
#include "terrain/color.h"
#include <cstdint>
#include <vector>

// Helper: extract RGBA channels
static uint8_t rch(uint32_t c) { return (c >> 16) & 0xFF; }
static uint8_t gch(uint32_t c) { return (c >>  8) & 0xFF; }
static uint8_t bch(uint32_t c) { return  c        & 0xFF; }

DELVE_TEST(lerp_color_at_zero_returns_c1) {
  uint32_t c1 = 0xFF112233;
  uint32_t c2 = 0xFF445566;
  uint32_t out = lerp_color(c1, c2, 0.0f);
  EXPECT_EQ((int)rch(out), (int)rch(c1));
  EXPECT_EQ((int)gch(out), (int)gch(c1));
  EXPECT_EQ((int)bch(out), (int)bch(c1));
  return true;
}

DELVE_TEST(lerp_color_at_one_returns_c2) {
  uint32_t c1 = 0xFF112233;
  uint32_t c2 = 0xFF445566;
  uint32_t out = lerp_color(c1, c2, 1.0f);
  EXPECT_EQ((int)rch(out), (int)rch(c2));
  EXPECT_EQ((int)gch(out), (int)gch(c2));
  EXPECT_EQ((int)bch(out), (int)bch(c2));
  return true;
}

DELVE_TEST(lerp_color_midpoint_is_between) {
  uint32_t c1 = 0xFF000000;
  uint32_t c2 = 0xFF888888;
  uint32_t out = lerp_color(c1, c2, 0.5f);
  EXPECT_RANGE((float)rch(out), 60.0f, 80.0f);
  return true;
}

DELVE_TEST(darken_color_at_zero_unchanged) {
  uint32_t c = 0xFFAABBCC;
  uint32_t out = darken_color(c, 0.0f);
  EXPECT_EQ((int)rch(out), (int)rch(c));
  EXPECT_EQ((int)gch(out), (int)gch(c));
  EXPECT_EQ((int)bch(out), (int)bch(c));
  return true;
}

DELVE_TEST(darken_color_at_one_is_black) {
  uint32_t c = 0xFFAABBCC;
  uint32_t out = darken_color(c, 1.0f);
  EXPECT_EQ((int)rch(out), 0);
  EXPECT_EQ((int)gch(out), 0);
  EXPECT_EQ((int)bch(out), 0);
  return true;
}

DELVE_TEST(darken_color_at_half_reduces_brightness) {
  uint32_t c = 0xFF808080;
  uint32_t out = darken_color(c, 0.5f);
  EXPECT_RANGE((float)rch(out), 60.0f, 70.0f);
  return true;
}

DELVE_TEST(alpha_blend_full_alpha_returns_src) {
  uint32_t src = 0xFF112233;
  uint32_t dst = 0xFF445566;
  uint32_t out = alpha_blend(src, dst, 1.0f);
  EXPECT_EQ((int)rch(out), (int)rch(src));
  EXPECT_EQ((int)gch(out), (int)gch(src));
  EXPECT_EQ((int)bch(out), (int)bch(src));
  return true;
}

DELVE_TEST(alpha_blend_zero_alpha_returns_dst) {
  uint32_t src = 0xFF112233;
  uint32_t dst = 0xFF445566;
  uint32_t out = alpha_blend(src, dst, 0.0f);
  EXPECT_EQ((int)rch(out), (int)rch(dst));
  EXPECT_EQ((int)gch(out), (int)gch(dst));
  EXPECT_EQ((int)bch(out), (int)bch(dst));
  return true;
}

DELVE_TEST(modulate_color_at_one_unchanged) {
  uint32_t c = 0xFF406080;
  uint32_t out = modulate_color(c, 1.0f);
  EXPECT_EQ((int)rch(out), (int)rch(c));
  EXPECT_EQ((int)gch(out), (int)gch(c));
  EXPECT_EQ((int)bch(out), (int)bch(c));
  return true;
}

DELVE_TEST(modulate_color_at_zero_is_black) {
  uint32_t c = 0xFF406080;
  uint32_t out = modulate_color(c, 0.0f);
  EXPECT_EQ((int)rch(out), 0);
  EXPECT_EQ((int)gch(out), 0);
  EXPECT_EQ((int)bch(out), 0);
  return true;
}

DELVE_TEST(add_noise_variation_preserves_alpha) {
  uint32_t c = 0xFF808080;
  uint32_t out = add_noise_variation(c, 5, 10);
  EXPECT_EQ((int)((out >> 24) & 0xFF), 0xFF);
  return true;
}

DELVE_TEST(add_noise_variation_different_positions_differ) {
  uint32_t c = 0xFF808080;
  uint32_t a = add_noise_variation(c, 0, 0);
  uint32_t b = add_noise_variation(c, 100, 200);
  // At least one channel should differ
  bool differs = (rch(a) != rch(b)) || (gch(a) != gch(b)) || (bch(a) != bch(b));
  EXPECT_TRUE(differs);
  return true;
}

DELVE_TEST(apply_hex_dither_does_not_change_size) {
  int w = 32, h = 32;
  std::vector<uint32_t> pixels(w * h, 0xFF808080);
  apply_hex_dither(pixels, w, h, 0.1f);
  EXPECT_EQ((int)pixels.size(), w * h);
  return true;
}

DELVE_TEST(apply_hex_dither_skips_skip_color) {
  int w = 4, h = 4;
  uint32_t skip = 0xFF000000;
  std::vector<uint32_t> pixels(w * h, skip);
  apply_hex_dither(pixels, w, h, 1.0f, skip);
  for (auto p : pixels)
    EXPECT_EQ((int)p, (int)skip);
  return true;
}
