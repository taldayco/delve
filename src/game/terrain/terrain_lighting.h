#pragma once
#include <cstdint>
#include <vector>

struct MapData;

// Horizon angle written for pixels with no occluder along a sweep line.
inline constexpr float TERRAIN_HORIZON_NONE = -1.5707963f;

struct TerrainLightBake {
  int width = 0, height = 0;
  std::vector<uint8_t> rg;   // 2 bytes/px, row-major: [0]=sun visibility, [1]=sky visibility, 0..255
};

struct TerrainLightParams {
  float height_scale      = 12.5f;  // ISO_HS: bake in visually-exaggerated height space
  float sun_elevation_deg = 55.0f;  // sun altitude in that exaggerated space
  float penumbra_deg      = 6.0f;   // angular half-width of the soft shadow edge
  float pixels_per_unit   = 8.0f;   // HEX_SIZE: divide pixel distances by this before angle math
};

// Line-sweep horizon scan (Timonen convex-hull sweep) for one principal
// direction. Each line of pixels is walked stepping by (step_dx, step_dy);
// the occluders of a pixel are the previously visited samples, i.e. they lie
// toward (-step_dx, -step_dy). heights are exaggerated world z,
// step_world_units is the per-step distance in world units. out_angles gets
// the horizon angle in radians per pixel (TERRAIN_HORIZON_NONE where the line
// has no earlier sample). Exposed for the sweep-vs-bruteforce test.
void sweep_horizon(const std::vector<float> &heights, int width, int height,
                   int step_dx, int step_dy, float step_world_units,
                   std::vector<float> &out_angles);

TerrainLightBake bake_terrain_lighting(const MapData &map, const TerrainLightParams &params = {});
