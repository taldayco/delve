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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static void write_domain_file(const std::string &dir, const std::string &name,
                              const nlohmann::json &data) {
  std::ofstream f(dir + "/" + name);
  f << data.dump(2) << "\n";
}

int main(int argc, char *argv[]) {
  int seed = 1337;
  int map_width = Config::MAP_WIDTH;
  int map_height = Config::MAP_HEIGHT;
  std::string config_path;
  std::string output_dir; // --output-dir: write per-domain JSON files

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
      seed = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
      config_path = argv[++i];
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      map_width = map_height = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
      output_dir = argv[++i];
    }
  }

  ElevationParams elev;
  RiverParams river;
  WorleyParams worley;
  CompositionParams comp;

  // Load from config.json if provided
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

  // Build mesh
  TerrainState ts;
  ts.current_palette = 0;
  ts.map_scale = 1.0f;

  ContourData cd;
  cd.heightmap.assign(md.basalt_height.begin(), md.basalt_height.end());
  cd.contour_lines = md.contour_lines;
  cd.band_map = md.band_map;

  TerrainMesh mesh = build_terrain_mesh(ts, md, cd);

  // Compute metrics
  auto t_start = std::chrono::steady_clock::now();

  auto elev_stats = elevation_stats(md.final_elevation);
  auto col_stats = hex_column_height_stats(md.columns);
  auto elev_hist = elevation_distribution(md.final_elevation, 10);

  auto t_end = std::chrono::steady_clock::now();
  double metrics_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

  // ── Per-domain JSON objects ──────────────────────────────────────────────

  nlohmann::json terrain_json = {
    {"domain", "terrain"},
    {"seed", seed},
    {"map_size", {{"width", map_width}, {"height", map_height}}},
    {"elevation", {
      {"min", elev_stats.min}, {"max", elev_stats.max},
      {"mean", elev_stats.mean}, {"stddev", elev_stats.stddev},
      {"histogram_10", elev_hist}
    }},
    {"columns", {
      {"count", md.columns.size()},
      {"height_min", col_stats.min}, {"height_max", col_stats.max},
      {"height_mean", col_stats.mean}, {"height_stddev", col_stats.stddev}
    }},
    {"lava", {
      {"body_count", md.lava_bodies.size()},
      {"coverage_pct", lava_region_ratio(md) * 100.0f}
    }},
    {"void", {
      {"coverage_pct", void_region_ratio(md) * 100.0f}
    }},
    {"basalt", {
      {"coverage_pct", basalt_coverage_ratio(md) * 100.0f}
    }},
    {"water", {
      {"coverage_pct", water_coverage_percent(md.final_elevation)}
    }},
    {"contours", {
      {"line_count", md.contour_lines.size()},
      {"band_count", plateau_count(md.band_map, map_width, map_height)}
    }}
  };

  nlohmann::json mesh_json = {
    {"domain", "mesh"},
    {"vertex_count", mesh_vertex_count(mesh)},
    {"index_count", mesh_index_count(mesh)},
    {"basalt_layers", mesh.basalt_layers.size()},
    {"lava_vertices", mesh.lava_vertices.size()},
    {"contour_vertices", mesh.contour_vertices.size()},
    {"degenerate_triangles", mesh_degenerate_triangles(mesh)},
    {"normal_validity", normal_validity(mesh)},
    {"color_validity", vertex_color_validity(mesh)},
    {"hex_roundtrip_accuracy", hex_roundtrip_accuracy(Config::HEX_SIZE, 10)}
  };

  nlohmann::json performance_json = {
    {"domain", "performance"},
    {"metrics_computation_ms", metrics_ms},
    {"map_pixels", map_width * map_height},
    {"vertices_per_pixel", (double)mesh_vertex_count(mesh) / (map_width * map_height)},
    {"indices_per_vertex", mesh_vertex_count(mesh) > 0
      ? (double)mesh_index_count(mesh) / mesh_vertex_count(mesh) : 0.0}
  };

  // ── Output mode ──────────────────────────────────────────────────────────

  if (!output_dir.empty()) {
    // Per-domain JSON files mode
    std::filesystem::create_directories(output_dir);

    write_domain_file(output_dir, "terrain.json", terrain_json);
    write_domain_file(output_dir, "mesh.json", mesh_json);
    write_domain_file(output_dir, "performance.json", performance_json);

    // Write manifest listing all emitted domains
    nlohmann::json manifest = {
      {"domains", {"terrain", "mesh", "performance"}},
      {"seed", seed},
      {"map_size", {{"width", map_width}, {"height", map_height}}}
    };
    write_domain_file(output_dir, "manifest.json", manifest);

    printf("{\"ok\":true,\"output_dir\":\"%s\",\"domains\":[\"terrain\",\"mesh\",\"performance\"]}\n",
           output_dir.c_str());
  } else {
    // Legacy monolithic JSON output (backwards compatible)
    nlohmann::json out;
    out["seed"] = seed;
    out["map_size"] = {{"width", map_width}, {"height", map_height}};
    out["elevation"] = terrain_json["elevation"];
    out["columns"] = terrain_json["columns"];
    out["lava"] = terrain_json["lava"];
    out["void"] = terrain_json["void"];
    out["basalt"] = terrain_json["basalt"];
    out["contours"] = terrain_json["contours"];
    out["mesh"] = {
      {"vertex_count", mesh_json["vertex_count"]},
      {"index_count", mesh_json["index_count"]},
      {"degenerate_triangles", mesh_json["degenerate_triangles"]},
      {"normal_validity", mesh_json["normal_validity"]},
      {"color_validity", mesh_json["color_validity"]}
    };
    printf("%s\n", out.dump(2).c_str());
  }

  return 0;
}
