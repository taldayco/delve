#include "test_harness.h"
#include "terrain_metrics.h"
#include "geometry_metrics.h"
#include "terrain/map_data.h"
#include "terrain/noise_layers.h"
#include "terrain/noise_composer.h"
#include "terrain/basalt.h"
#include "terrain/lava.h"
#include "terrain/contour.h"
#include "terrain/terrain_mesh.h"
#include "game_state.h"
#include "config.h"
#include <cmath>

// Small map for fast test runs
static constexpr int TW = 256;
static constexpr int TH = 256;

static MapData run_pipeline(int seed = 42) {
  MapData md;
  md.allocate(TW, TH);

  ElevationParams elev;
  elev.seed = seed;
  RiverParams river;
  river.seed = seed + 1;
  WorleyParams worley;
  worley.seed = seed + 2;
  CompositionParams comp;

  compose_layers(md, elev, river, worley, comp, nullptr);
  md.columns = generate_basalt_columns_v2(md, Config::HEX_SIZE);

  auto fill = generate_lava_and_void(md, comp.void_chance, worley.seed);
  md.lava_bodies = std::move(fill.lava_bodies);
  md.void_bodies = std::move(fill.void_bodies);

  float interval = 1.0f / comp.terrace_levels;
  extract_contours(md.basalt_height, TW, TH, interval, md.contour_lines, md.band_map);
  simplify_contours(md.contour_lines, 0.5f);

  return md;
}

DELVE_TEST(pipeline_produces_columns) {
  auto md = run_pipeline();
  EXPECT_GT((float)md.columns.size(), 0.0f);
  return true;
}

DELVE_TEST(pipeline_no_nan_inf_elevation) {
  auto md = run_pipeline();
  for (float v : md.final_elevation) {
    EXPECT_FALSE(std::isnan(v));
    EXPECT_FALSE(std::isinf(v));
  }
  return true;
}

DELVE_TEST(pipeline_valid_terrain_map) {
  auto md = run_pipeline();
  for (auto v : md.terrain_map) {
    // Valid values: TERRAIN_EMPTY(0), TERRAIN_BASALT(-1), TERRAIN_LAVA(-2), TERRAIN_VOID(-3), or positive plateau index
    EXPECT_TRUE(v >= TERRAIN_VOID);
  }
  return true;
}

DELVE_TEST(pipeline_contour_lines_generated) {
  auto md = run_pipeline();
  EXPECT_GT((float)md.contour_lines.size(), 0.0f);
  return true;
}

DELVE_TEST(pipeline_mesh_no_degenerate_triangles) {
  auto md = run_pipeline();

  TerrainState ts;
  ts.current_palette = 0;
  ts.map_scale = 1.0f;

  ContourData cd;
  cd.heightmap.assign(md.basalt_height.begin(), md.basalt_height.end());
  cd.contour_lines = md.contour_lines;
  cd.band_map = md.band_map;

  TerrainMesh mesh = build_terrain_mesh(ts, md, cd);
  int degen = mesh_degenerate_triangles(mesh);
  EXPECT_EQ(degen, 0);
  return true;
}

DELVE_TEST(pipeline_mesh_valid_normals) {
  auto md = run_pipeline();

  TerrainState ts;
  ts.current_palette = 0;
  ts.map_scale = 1.0f;

  ContourData cd;
  cd.heightmap.assign(md.basalt_height.begin(), md.basalt_height.end());
  cd.contour_lines = md.contour_lines;
  cd.band_map = md.band_map;

  TerrainMesh mesh = build_terrain_mesh(ts, md, cd);
  float validity = normal_validity(mesh);
  EXPECT_GT(validity, 0.99f);
  return true;
}
