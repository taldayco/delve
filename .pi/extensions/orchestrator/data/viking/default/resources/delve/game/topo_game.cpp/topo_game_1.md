#include "topo_game.h"
#include "core/gltf_loader.h"
#include "render/rig_animation.h"
#include "terrain/basalt.h"
#include "terrain/lava.h"
#include "terrain/noise_composer.h"
#include "terrain/contour.h"
#include "terrain/palettes.h"
#include "terrain/map_util.h"
#include "terrain/hex.h"
#include "ui/imgui_ui.h"
#include "core/watchdog.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

static json params_to_json(const ElevationParams &elev,
                           const WorleyParams &worley, const CompositionParams &comp,
                           const TerrainState &ts) {
  return {
    {"elevation", {
      {"frequency",  elev.frequency}, {"octaves",    elev.octaves},
      {"lacunarity", elev.lacunarity},{"gain",        elev.gain},
      {"scurve_bias", elev.scurve_bias}
    }},
    {"worley", {
      {"frequency",      worley.frequency},
      {"jitter",         worley.jitter},       {"warp_amp",       worley.warp_amp},
      {"warp_frequency", worley.warp_frequency},{"warp_octaves",  worley.warp_octaves}
    }},
    {"composition", {
      {"void_chance",    comp.void_chance},
      {"terrace_levels", comp.terrace_levels},
      {"min_region_size",comp.min_region_size}
    }},
    {"terrain", {
      {"use_isometric",  ts.use_isometric},
      {"current_palette",ts.current_palette},
      {"map_scale",      ts.map_scale},
      {"master_seed",    ts.master_seed}
    }}
  };
}

static void json_to_params(const json &j, ElevationParams &elev,
                            WorleyParams &worley, CompositionParams &comp,
                            TerrainState &ts) {
  if (j.contains("elevation")) {
    auto &e = j["elevation"];
    if (e.contains("frequency"))   elev.frequency   = e["frequency"];
    if (e.contains("octaves"))     elev.octaves     = e["octaves"];
    if (e.contains("lacunarity"))  elev.lacunarity  = e["lacunarity"];
    if (e.contains("gain"))        elev.gain        = e["gain"];
    if (e.contains("scurve_bias")) elev.scurve_bias = e["scurve_bias"];
  }
  if (j.contains("worley")) {
    auto &w = j["worley"];
    if (w.contains("frequency"))       worley.frequency       = w["frequency"];
    if (w.contains("jitter"))          worley.jitter          = w["jitter"];
    if (w.contains("warp_amp"))        worley.warp_amp        = w["warp_amp"];
    if (w.contains("warp_frequency"))  worley.warp_frequency  = w["warp_frequency"];
    if (w.contains("warp_octaves"))    worley.warp_octaves    = w["warp_octaves"];
  }
  if (j.contains("composition")) {
    auto &c = j["composition"];
    if (c.contains("void_chance"))     comp.void_chance     = c["void_chance"];
    if (c.contains("terrace_levels"))  comp.terrace_levels  = c["terrace_levels"];
    if (c.contains("min_region_size")) comp.min_region_size = c["min_region_size"];
  }
  if (j.contains("terrain")) {
    auto &t = j["terrain"];
    if (t.contains("use_isometric"))   ts.use_isometric   = t["use_isometric"];
    if (t.contains("current_palette")) ts.current_palette = t["current_palette"];
    if (t.contains("map_scale"))       ts.map_scale       = t["map_scale"];
    if (t.contains("master_seed"))     ts.master_seed     = t["master_seed"];
  }

  // Backward compatibility: old configs with individual seeds but no master_seed
  if (!j.contains("terrain") || !j["terrain"].contains("master_seed")) {
    if (j.contains("elevation") && j["elevation"].contains("seed")) {
      ts.master_seed = j["elevation"]["seed"];
    }
  }
}

void TopoGame::on_init(GpuContext &gpu, flecs::world &ecs) {
  ecs.set<GamePhase>({});
  ecs.set<TerrainState>({});
  ecs.set<WindowState>({true, false});
  ecs.set<ElevationParams>({});
  ecs.set<RiverParams>({});
  ecs.set<WorleyParams>({});
  ecs.set<CompositionParams>({});
  ecs.set<MapData>({});
  ecs.set<ContourData>({});

  task_system.init(1);

  input.init();