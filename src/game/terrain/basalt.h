#pragma once
#include "terrain/hex.h"
#include "terrain/contour.h"
#include <cstdint>
#include <vector>

struct MapData;

struct WorleyBasaltParams {
  float density_threshold = 0.2f;
  float jitter_scale = 0.05f;
  float edge_threshold = 0.7f;
};

std::vector<HexColumn>
generate_basalt_columns_v2(MapData &data, float hex_size,
                           const WorleyBasaltParams &params = {});
