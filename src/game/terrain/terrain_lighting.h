#pragma once
#include <cstdint>
#include <vector>

struct MapData;

inline constexpr float TERRAIN_HORIZON_NONE = -1.5707963f;

struct TerrainLightBake {
  int width = 0, height = 0;
  std::vector<uint8_t> rg;
};

struct TerrainLightParams {
  float height_scale      = 12.5f;
  float sun_elevation_deg = 55.0f;
  float penumbra_deg      = 6.0f;
  float pixels_per_unit   = 8.0f;
};

void sweep_horizon(const std::vector<float> &heights, int width, int height,
                   int step_dx, int step_dy, float step_world_units,
                   std::vector<float> &out_angles);

TerrainLightBake bake_terrain_lighting(const MapData &map, const TerrainLightParams &params = {});
