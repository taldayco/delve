#pragma once
#include "terrain/hex.h"
#include "terrain/contour.h"
#include <cstdint>
#include <vector>

struct MapData;

std::vector<HexColumn>
generate_basalt_columns(std::span<const float> heightmap, int width, int height,
                        float hex_size,
                        const std::vector<Plateau> &plateaus,
                        std::vector<int> &plateaus_with_columns_out,
                        std::vector<int16_t> &terrain_map);

struct WorleyBasaltParams {
  float density_threshold = 0.2f;
  float jitter_scale = 0.05f;
  float edge_threshold = 0.7f;
};

std::vector<HexColumn>
generate_basalt_columns_v2(MapData &data, float hex_size,
                           const WorleyBasaltParams &params = {});
