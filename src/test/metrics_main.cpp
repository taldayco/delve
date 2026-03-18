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
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

int main(int argc, char *argv[]) {
  int seed = 1337;
  int map_width = Config::MAP_WIDTH;
  int map_height = Config::MAP_HEIGHT;
  std::string config_path;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
      seed = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
      config_path = argv[++i];
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      map_width = map_height = std::atoi(argv[++i]);
    }
  }

  ElevationParams elev;
  RiverParams river;
  WorleyParams worley;
  CompositionParams comp;

  if (!config_path.empty()) {
    std::ifstream f(config_path);
    if (f.is_open()) {
      nlohmann::json j;
      f >> j;
      if (j.contains("elevation")) {
        auto &e = j["elevation"];
        if (e.contains("frequency")) elev.frequency = e["frequency"];
        if (e.contains("octaves")) elev.octaves = e["octaves"];
        if (e.contains("seed")) seed = e["seed"];
      }
      if (j.contains("worley")) {
        auto &w = j["worley"];
        if (w.contains("frequency")) worley.frequency = w["frequency"];
        if (w.contains("seed")) worley.seed = w["seed"];
      }
    }
  }

  elev.seed = seed;
  river.seed = seed + 1;
  worley.seed = seed + 2;

  MapData md;
  md.allocate(map_width, map_height);

  compose_layers(md, elev, river, worley, comp, nullptr);
  md.columns = generate_basalt_columns_v2(md, Config::HEX_SIZE);

  auto fill = generate_lava_and_void(md, comp.void_chance, worley.seed);
  md.lava_bodies = std::move(fill.lava_bodies);
  md.void_bodies = std::move(fill.void_bodies);

  float interval = 1.0f / comp.terrace_levels;
  extract_contours(md.basalt_height, map_width, map_height, interval,
                   md.contour_lines, md.band_map);
  simplify_contours(md.contour_lines, 0.5f);

  TerrainState ts;
  ts.current_palette = 0;
  ts.map_scale = 1.0f;

  ContourData cd;
  cd.heightmap.assign(md.basalt_height.begin(), md.basalt_height.end());
  cd.contour_lines = md.contour_lines;
  cd.band_map = md.band_map;

  TerrainMesh mesh = build_terrain_mesh(ts, md, cd);

  auto elev_stats = elevation_stats(md.final_elevation);
  auto col_stats = hex_column_height_stats(md.columns);

  nlohmann::json out;
  out["seed"] = seed;
  out["map_size"] = {{"width", map_width}, {"height", map_height}};
  out["elevation"] = {
    {"min", elev_stats.min}, {"max", elev_stats.max},
    {"mean", elev_stats.mean}, {"stddev", elev_stats.stddev}
  };
  out["columns"] = {
    {"count", md.columns.size()},
    {"height_min", col_stats.min}, {"height_max", col_stats.max}
  };
  out["lava"] = {
    {"body_count", md.lava_bodies.size()},
    {"coverage_pct", lava_region_ratio(md) * 100.0f}
  };
  out["void"] = {
    {"coverage_pct", void_region_ratio(md) * 100.0f}
  };
  out["basalt"] = {
    {"coverage_pct", basalt_coverage_ratio(md) * 100.0f}
  };
  out["contours"] = {
    {"line_count", md.contour_lines.size()}
  };
  out["mesh"] = {
    {"vertex_count", mesh_vertex_count(mesh)},
    {"index_count", mesh_index_count(mesh)},
    {"degenerate_triangles", mesh_degenerate_triangles(mesh)},
    {"normal_validity", normal_validity(mesh)},
    {"color_validity", vertex_color_validity(mesh)}
  };

  printf("%s\n", out.dump(2).c_str());
  return 0;
}
