#pragma once
#include "core/types.h"
#include "terrain/util.h"
#include <cstdint>
#include <vector>

struct HexColumn {
  int q, r;
  float height;
  float base_height;
  bool visible_edges[6];
  float edge_drops[6];
};

struct HexCoord {
  int q, r;
  bool operator==(const HexCoord &o) const { return q == o.q && r == o.r; }
};

struct HexHash {
  size_t operator()(const HexCoord &h) const { return hash2d(h.q, h.r); }
};

void hex_to_pixel(int q, int r, float hex_size, float &out_x, float &out_y);
HexCoord pixel_to_hex(float x, float y, float hex_size);
void get_hex_corners(int q, int r, float hex_size, Vec2 corners[6]);
bool pixel_in_hex(float px, float py, int q, int r, float hex_size);
void compute_visible_edges(std::vector<HexColumn> &columns);
