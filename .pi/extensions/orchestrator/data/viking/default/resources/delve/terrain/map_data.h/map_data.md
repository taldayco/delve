#pragma once
#include "terrain/contour.h"
#include "terrain/hex.h"
#include "terrain/lava.h"
#include <cstdint>
#include <vector>


constexpr int16_t TERRAIN_EMPTY  =  0;
constexpr int16_t TERRAIN_BASALT = -1;
constexpr int16_t TERRAIN_LAVA   = -2;
constexpr int16_t TERRAIN_VOID   = -3;

struct MapData {
  int width = 0;
  int height = 0;


  std::vector<float> elevation;
  std::vector<float> river_mask;
  std::vector<float> worley;
  std::vector<float> worley_edge;
  std::vector<float> worley_cell_value;


  std::vector<float> final_elevation;
  std::vector<uint8_t> liquid_mask;
  std::vector<float> basalt_height;


  std::vector<HexColumn> columns;
  std::vector<int16_t> terrain_map;
  std::vector<LavaBody> lava_bodies;
  std::vector<LavaBody> void_bodies;
  std::vector<Line> contour_lines;
  std::vector<int> band_map;

  void allocate(int w, int h) {
    width = w;
    height = h;
    int n = w * h;
    elevation.resize(n);
    river_mask.resize(n);
    worley.resize(n);
    worley_edge.resize(n);
    worley_cell_value.resize(n);
    final_elevation.resize(n);
    liquid_mask.resize(n);
    basalt_height.resize(n);
    terrain_map.assign(n, 0);
    columns.clear();
    lava_bodies.clear();
    void_bodies.clear();
    contour_lines.clear();
    band_map.resize(n);
  }
};
