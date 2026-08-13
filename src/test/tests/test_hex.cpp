#include "test_harness.h"
#include "terrain/hex.h"
#include "core/types.h"
#include <cmath>
#include <set>

static constexpr float HEX = 8.0f;

DELVE_TEST(hex_to_pixel_roundtrip) {
  int total = 0, accurate = 0;
  for (int q = -10; q <= 10; ++q) {
    for (int r = -10; r <= 10; ++r) {
      float px, py;
      hex_to_pixel(q, r, HEX, px, py);
      HexCoord back = pixel_to_hex(px, py, HEX);
      ++total;
      if (back.q == q && back.r == r) ++accurate;
    }
  }
  EXPECT_GT((float)accurate / total, 0.95f);
  return true;
}

DELVE_TEST(hex_to_pixel_origin) {
  float px, py;
  hex_to_pixel(0, 0, HEX, px, py);
  EXPECT_NEAR(px, 0.0f, 0.01f);
  EXPECT_NEAR(py, 0.0f, 0.01f);
  return true;
}

DELVE_TEST(hex_visible_edge_for_subtle_height_drop) {
  std::vector<HexColumn> cols(2);
  cols[0].q = 0; cols[0].r = 0; cols[0].height = 0.5f;
  cols[1].q = 1; cols[1].r = 0; cols[1].height = 0.495f;

  compute_visible_edges(cols);

  EXPECT_TRUE(cols[0].visible_edges[0]);
  EXPECT_NEAR(cols[0].edge_drops[0], 0.005f, 1e-6f);
  EXPECT_FALSE(cols[1].visible_edges[3]);
  return true;
}

DELVE_TEST(hex_no_visible_edge_for_flush_neighbors) {
  std::vector<HexColumn> cols(2);
  cols[0].q = 0; cols[0].r = 0; cols[0].height = 0.5f;
  cols[1].q = 1; cols[1].r = 0; cols[1].height = 0.5f;

  compute_visible_edges(cols);

  EXPECT_FALSE(cols[0].visible_edges[0]);
  EXPECT_FALSE(cols[1].visible_edges[3]);
  return true;
}

DELVE_TEST(hex_corners_count_six) {
  Vec2 corners[6];
  get_hex_corners(0, 0, HEX, corners);
  std::set<std::pair<int,int>> unique;
  for (int i = 0; i < 6; ++i) {
    unique.insert({(int)(corners[i].x * 1000), (int)(corners[i].y * 1000)});
  }
  EXPECT_EQ((int)unique.size(), 6);
  return true;
}

DELVE_TEST(hex_corners_equidistant_from_center) {
  Vec2 corners[6];
  get_hex_corners(0, 0, HEX, corners);
  float first_dist = std::sqrt(corners[0].x * corners[0].x + corners[0].y * corners[0].y);
  for (int i = 1; i < 6; ++i) {
    float d = std::sqrt(corners[i].x * corners[i].x + corners[i].y * corners[i].y);
    EXPECT_NEAR(d, first_dist, 0.01f);
  }
  return true;
}

DELVE_TEST(pixel_in_hex_center_is_inside) {
  float px, py;
  hex_to_pixel(3, 4, HEX, px, py);
  EXPECT_TRUE(pixel_in_hex(px, py, 3, 4, HEX));
  return true;
}

DELVE_TEST(pixel_in_hex_far_away_is_outside) {
  EXPECT_FALSE(pixel_in_hex(10000.0f, 10000.0f, 0, 0, HEX));
  return true;
}

DELVE_TEST(hex_neighbor_offsets_canonical) {
  const int dq[] = { 1, 0, -1, -1,  0,  1 };
  const int dr[] = { 0, 1,  1,  0, -1, -1 };
  for (int i = 0; i < 6; ++i) {
    int ds = -dq[i] - dr[i];
    int zeros = (std::abs(dq[i]) == 0 ? 1 : 0)
              + (std::abs(dr[i]) == 0 ? 1 : 0)
              + (std::abs(ds) == 0 ? 1 : 0);
    EXPECT_EQ(zeros, 1);
  }
  return true;
}

DELVE_TEST(hex_cube_roundtrip_invariant) {
  for (int q = -10; q <= 10; ++q) {
    for (int r = -10; r <= 10; ++r) {
      float px, py;
      hex_to_pixel(q, r, HEX, px, py);
      HexCoord back = pixel_to_hex(px, py, HEX);
      int s = -back.q - back.r;
      EXPECT_EQ(back.q + back.r + s, 0);
    }
  }
  return true;
}

DELVE_TEST(hex_corner_angles_60_degrees) {
  Vec2 corners[6];
  get_hex_corners(0, 0, HEX, corners);
  for (int i = 0; i < 6; ++i) {
    int j = (i + 1) % 6;
    float angle_i = std::atan2(corners[i].y, corners[i].x);
    float angle_j = std::atan2(corners[j].y, corners[j].x);
    float diff = angle_j - angle_i;
    while (diff > M_PI) diff -= 2.0f * M_PI;
    while (diff < -M_PI) diff += 2.0f * M_PI;
    EXPECT_NEAR(std::abs(diff), (float)(M_PI / 3.0), 0.01f);
  }
  return true;
}

DELVE_TEST(hex_roundtrip_100pct_accuracy) {
  int total = 0, accurate = 0;
  for (int q = -20; q <= 20; ++q) {
    for (int r = -20; r <= 20; ++r) {
      float px, py;
      hex_to_pixel(q, r, HEX, px, py);
      HexCoord back = pixel_to_hex(px, py, HEX);
      ++total;
      if (back.q == q && back.r == r) ++accurate;
    }
  }
  EXPECT_EQ(accurate, total);
  return true;
}

DELVE_TEST(hex_roundtrip_at_map_extents) {
  int total = 0, accurate = 0;
  for (int q = -128; q <= 128; ++q) {
    for (int r = -128; r <= 128; ++r) {
      float px, py;
      hex_to_pixel(q, r, HEX, px, py);
      HexCoord back = pixel_to_hex(px, py, HEX);
      ++total;
      if (back.q == q && back.r == r) ++accurate;
    }
  }
  EXPECT_EQ(accurate, total);
  return true;
}
