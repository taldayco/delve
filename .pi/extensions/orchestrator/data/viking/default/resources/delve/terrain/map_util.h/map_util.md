#pragma once
#include "terrain/map_data.h"
#include "terrain/hex.h"
#include <glm/glm.hpp>
#include <cstdint>

// Bilinear sample of basalt_height at world-space (wx, wy).
// wx, wy are in world units (pixels / HEX_SIZE).
// Returns 0.0f if basalt_height is empty.
float sample_world_height(const MapData &map, float wx, float wy);

// Sample terrain height over a small disc of `radius` (center + 8 perimeter points).
// Returns the maximum height found — approximates a sphere resting on terrain.
float sphere_trace_height(const MapData &map, float wx, float wy, float radius);

// Central-difference terrain normal at world-space (wx, wy).
// Returns normalized surface normal (pointing upward).
glm::vec3 sample_terrain_normal(const MapData &map, float wx, float wy);

// Find a good spawn column: the largest connected component in map.columns,
// then a random column from it seeded by `seed`.
// Returns a default HexColumn (q=r=0, height=0) if columns is empty.
HexColumn find_spawn_column(const MapData &map, uint32_t seed);
