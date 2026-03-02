#include "test_harness.h"
#include "terrain_metrics.h"
#include "terrain/noise_layers.h"

DELVE_TEST(elevation_output_in_valid_range) {
  const int W = 256, H = 256;
  std::vector<float> elev(W * H);
  ElevationParams params;
  params.seed = 42;
  generate_elevation_layer(elev, W, H, params);

  for (size_t i = 0; i < elev.size(); i++) {
    EXPECT_RANGE(elev[i], -0.1f, 1.1f);
  }
}

DELVE_TEST(elevation_deterministic_with_same_seed) {
  const int W = 128, H = 128;
  std::vector<float> a(W * H), b(W * H);
  ElevationParams params;
  params.seed = 123;

  generate_elevation_layer(a, W, H, params);
  generate_elevation_layer(b, W, H, params);

  for (size_t i = 0; i < a.size(); i++) {
    EXPECT_NEAR(a[i], b[i], 1e-6f);
  }
}

DELVE_TEST(different_seeds_produce_different_output) {
  const int W = 128, H = 128;
  std::vector<float> a(W * H), b(W * H);
  ElevationParams params;

  params.seed = 100;
  generate_elevation_layer(a, W, H, params);
  params.seed = 200;
  generate_elevation_layer(b, W, H, params);

  int diffs = 0;
  for (size_t i = 0; i < a.size(); i++) {
    if (std::abs(a[i] - b[i]) > 0.001f) diffs++;
  }
  // Vast majority should differ
  EXPECT_GT(diffs, (int)(a.size() * 0.5));
}

DELVE_TEST(elevation_has_variance) {
  const int W = 256, H = 256;
  std::vector<float> elev(W * H);
  ElevationParams params;
  params.seed = 42;
  generate_elevation_layer(elev, W, H, params);

  auto stats = elevation_stats(elev);
  // Should have meaningful variance, not all the same value
  EXPECT_GT(stats.stddev, 0.01f);
  // Should span a reasonable range
  EXPECT_GT(stats.max - stats.min, 0.1f);
}

DELVE_TEST(worley_layer_produces_three_channels) {
  const int W = 128, H = 128;
  int n = W * H;
  std::vector<float> value(n), edge(n), cell_value(n);
  WorleyParams params;
  params.seed = 42;

  generate_worley_layer(value, edge, cell_value, W, H, params);

  // All three channels should have been filled with non-trivial data
  int nonzero_v = 0, nonzero_e = 0, nonzero_c = 0;
  for (int i = 0; i < n; i++) {
    if (std::abs(value[i]) > 1e-6f) nonzero_v++;
    if (std::abs(edge[i]) > 1e-6f) nonzero_e++;
    if (std::abs(cell_value[i]) > 1e-6f) nonzero_c++;
  }
  EXPECT_GT(nonzero_v, n / 2);
  EXPECT_GT(nonzero_e, n / 4);
  EXPECT_GT(nonzero_c, n / 4);
}

DELVE_TEST(river_mask_in_binary_range) {
  const int W = 128, H = 128;
  std::vector<float> mask(W * H);
  RiverParams params;
  params.seed = 42;

  generate_river_mask(mask, W, H, params);

  for (float v : mask) {
    EXPECT_RANGE(v, 0.0f, 1.0f);
  }
}
