#pragma once
#include "config.h"
#include "terrain/contour.h"
#include "terrain/map_data.h"
#include "terrain/terrain_mesh.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations for async state
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

// Plain ECS component — must remain copyable/movable for flecs.
struct TerrainState {
  bool  use_isometric   = DEFAULT_ISOMETRIC;
  int   current_palette = 0;
  float map_scale       = Config::DEFAULT_MAP_SCALE;
  float contour_opacity = Config::DEFAULT_CONTOUR_OPACITY;
  bool  need_regenerate = true;
};

// Async generation state — NOT a flecs component; owned by TopoGame.
struct AsyncTerrainState {
  std::atomic<bool>            is_generating{false};
  std::shared_ptr<TerrainMesh> pending_mesh;
  std::shared_ptr<MapData>     pending_map;
  std::shared_ptr<ContourData> pending_contours;
  std::mutex                   pending_mtx;
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
