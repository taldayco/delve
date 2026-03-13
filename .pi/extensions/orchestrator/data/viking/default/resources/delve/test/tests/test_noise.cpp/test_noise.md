#include "test_harness.h"
#include "terrain_metrics.h"
#include "terrain/noise.h"
#include "terrain/noise_layers.h"
#include <vector>

static constexpr int W = 128;
static constexpr int H = 128;

DELVE_TEST(elevation_output_in_valid_range) {
  std::vector<float> out(W * H);
  NoiseParams params;
  params.seed = 42;
  generate_heightmap(out, W, H, params);
  for (float v : out) {
    EXPECT_RANGE(v, -0.1f, 1.1f);
  }
  return true;
}

DELVE_TEST(elevation_deterministic_with_same_seed) {
  std::vector<float> a(W * H), b(W * H);
  NoiseParams params;
  params.seed = 42;
  generate_heightmap(a, W, H, params);
  generate_heightmap(b, W, H, params);
  for (int i = 0; i < W * H; ++i) {
    EXPECT_NEAR(a[i], b[i], 1e-6f);
  }
  return true;
}

DELVE_TEST(different_seeds_produce_different_output) {
  std::vector<float> a(W * H), b(W * H);
  NoiseParams pa, pb;
  pa.seed = 42; pb.seed = 999;
  generate_heightmap(a, W, H, pa);
  generate_heightmap(b, W, H, pb);
  int differ = 0;
  for (int i = 0; i < W * H; ++i)
    if (std::abs(a[i] - b[i]) > 0.001f) ++differ;
  EXPECT_GT((float)differ, (float)(W * H) * 0.1f);
  return true;
}

DELVE_TEST(elevation_has_variance) {
  std::vector<float> out(W * H);
  NoiseParams params;
  params.seed = 42;
  generate_heightmap(out, W, H, params);
  auto stats = elevation_stats(out);
  EXPECT_GT(stats.stddev, 0.01f);
  EXPECT_GT(stats.max - stats.min, 0.1f);
  return true;
}

DELVE_TEST(worley_layer_produces_three_channels) {
  std::vector<float> val(W * H), edge(W * H), cell(W * H);
  WorleyParams wp;
  wp.seed = 42;
  generate_worley_layer(val, edge, cell, W, H, wp);

  auto vs = elevation_stats(val);
  auto es = elevation_stats(edge);
  auto cs = elevation_stats(cell);
  EXPECT_GT(vs.stddev, 0.001f);
  EXPECT_GT(es.stddev, 0.001f);
  EXPECT_GT(cs.stddev, 0.001f);
  return true;
}

DELVE_TEST(river_mask_in_binary_range) {
  std::vector<float> out(W * H);
  RiverParams rp;
  rp.seed = 42;
  generate_river_mask(out, W, H, rp);
  for (float v : out) {
    EXPECT_RANGE(v, 0.0f, 1.0f);
  }
  return true;
}
