#include "test_harness.h"
#include "geometry_metrics.h"
#include "terrain/hex.h"
#include "config.h"

DELVE_TEST(hex_to_pixel_roundtrip) {
  float accuracy = hex_roundtrip_accuracy(20, Config::HEX_SIZE);
  // Roundtrip conversion should be at least 95% accurate
  EXPECT_GT(accuracy, 95.0f);
}

DELVE_TEST(hex_to_pixel_origin) {
  float x, y;
  hex_to_pixel(0, 0, Config::HEX_SIZE, x, y);
  EXPECT_NEAR(x, 0.0f, 0.01f);
  EXPECT_NEAR(y, 0.0f, 0.01f);
}

DELVE_TEST(hex_corners_count_six) {
  Vec2 corners[6];
  get_hex_corners(0, 0, Config::HEX_SIZE, corners);

  // All 6 corners should be distinct
  for (int i = 0; i < 6; i++) {
    for (int j = i + 1; j < 6; j++) {
      float dist = std::sqrt(
        (corners[i].x - corners[j].x) * (corners[i].x - corners[j].x) +
        (corners[i].y - corners[j].y) * (corners[i].y - corners[j].y)
      );
      EXPECT_GT(dist, 0.01f);
    }
  }
}

DELVE_TEST(hex_corners_equidistant_from_center) {
  float cx, cy;
  hex_to_pixel(0, 0, Config::HEX_SIZE, cx, cy);

  Vec2 corners[6];
  get_hex_corners(0, 0, Config::HEX_SIZE, corners);

  float first_dist = std::sqrt(
    (corners[0].x - cx) * (corners[0].x - cx) +
    (corners[0].y - cy) * (corners[0].y - cy)
  );

  for (int i = 1; i < 6; i++) {
    float dist = std::sqrt(
      (corners[i].x - cx) * (corners[i].x - cx) +
      (corners[i].y - cy) * (corners[i].y - cy)
    );
    EXPECT_NEAR(dist, first_dist, 0.01f);
  }
}

DELVE_TEST(pixel_in_hex_center_is_inside) {
  float cx, cy;
  hex_to_pixel(3, 4, Config::HEX_SIZE, cx, cy);
  EXPECT_TRUE(pixel_in_hex(cx, cy, 3, 4, Config::HEX_SIZE));
}

DELVE_TEST(pixel_in_hex_far_away_is_outside) {
  EXPECT_FALSE(pixel_in_hex(10000.0f, 10000.0f, 0, 0, Config::HEX_SIZE));
}
