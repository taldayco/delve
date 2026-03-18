#pragma once
#include "terrain/map_data.h"
#include "terrain/hex.h"
#include <glm/glm.hpp>
#include <cstdint>

float sample_world_height(const MapData &map, float wx, float wy);

float sphere_trace_height(const MapData &map, float wx, float wy, float radius);

glm::vec3 sample_terrain_normal(const MapData &map, float wx, float wy);

HexColumn find_spawn_column(const MapData &map, uint32_t seed);
