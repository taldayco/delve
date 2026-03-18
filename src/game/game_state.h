#pragma once
#include "config.h"
#include "terrain/contour.h"
#include "terrain/map_data.h"
#include "terrain/terrain_mesh.h"
#include "terrain/noise_cache.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct TerrainMesh;
struct MapData;
struct ContourData;

struct PointLightComponent {
    float pos_x, pos_y, pos_z;
    float radius;
    float color_r, color_g, color_b;
    float intensity;
};

constexpr bool DEFAULT_ISOMETRIC = true;

struct GamePhase {
  enum Phase { Menu, Playing, Paused };
  Phase current = Playing;
};

struct TerrainState {
  bool  use_isometric   = DEFAULT_ISOMETRIC;
  int   current_palette = 0;
  int   master_seed     = 1337;
  float map_scale       = Config::DEFAULT_MAP_SCALE;
  float contour_opacity = Config::DEFAULT_CONTOUR_OPACITY;
  bool  need_regenerate = true;
};

struct AsyncTerrainState {
  std::atomic<bool>            is_generating{false};
  std::atomic<bool>            cancel_requested{false};
  std::shared_ptr<TerrainMesh> pending_mesh;
  std::shared_ptr<MapData>     pending_map;
  std::shared_ptr<ContourData> pending_contours;
  std::mutex                   pending_mtx;
  NoiseCache                   async_cache;
};

struct WindowState {
  bool launch_game_requested = false;
  bool close_game_requested = false;
};

struct ContourData {
  std::vector<float> heightmap;
  std::vector<int> band_map;
  std::vector<Line> contour_lines;
};
