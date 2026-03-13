#include "terrain/hex.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

void hex_to_pixel(int q, int r, float hex_size, float &out_x, float &out_y) {
  const float sqrt3 = 1.732f;
  out_x = hex_size * 1.5f * q;
  out_y = hex_size * sqrt3 * (r + q * 0.5f);
}

HexCoord pixel_to_hex(float x, float y, float hex_size) {
  const float sqrt3 = 1.732f;
  float q = (2.0f / 3.0f * x) / hex_size;
  float r = (-1.0f / 3.0f * x + sqrt3 / 3.0f * y) / hex_size;

  int iq = (int)std::round(q);
  int ir = (int)std::round(r);
  int is = (int)std::round(-q - r);

  float dq = std::abs(iq - q);
  float dr = std::abs(ir - r);
  float ds = std::abs(is - (-q - r));

  if (dq > dr && dq > ds) {
    iq = -ir - is;
  } else if (dr > ds) {
    ir = -iq - is;
  }

  return {iq, ir};
}

void get_hex_corners(int q, int r, float hex_size, Vec2 corners[6]) {
  const float PI = 3.14159265359f;
  float cx, cy;
  hex_to_pixel(q, r, hex_size, cx, cy);
  for (int i = 0; i < 6; ++i) {
    float angle = i * PI / 3.0f;
    corners[i].x = cx + hex_size * std::cos(angle);
    corners[i].y = cy + hex_size * std::sin(angle);
  }
}

bool pixel_in_hex(float px, float py, int q, int r, float hex_size) {
  Vec2 corners[6];
  get_hex_corners(q, r, hex_size, corners);
  for (int i = 0; i < 6; ++i) {
    int next = (i + 1) % 6;
    float edge_x = corners[next].x - corners[i].x;
    float edge_y = corners[next].y - corners[i].y;
    float to_point_x = px - corners[i].x;
    float to_point_y = py - corners[i].y;
    float cross = edge_x * to_point_y - edge_y * to_point_x;
    if (cross < 0) return false;
  }
  return true;
}

void compute_visible_edges(std::vector<HexColumn> &columns) {
  std::unordered_map<HexCoord, HexColumn *, HexHash> col_map;
  for (auto &col : columns) {
    HexCoord key = {col.q, col.r};
    col_map[key] = &col;
  }
  const int neighbors[6][2] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
  for (auto &col : columns) {
    for (int i = 0; i < 6; ++i) {
      col.visible_edges[i] = false;
      col.edge_drops[i]    = 0.0f;
      HexCoord nb = {col.q + neighbors[i][0], col.r + neighbors[i][1]};
      auto it = col_map.find(nb);
      if (it == col_map.end()) {
        col.visible_edges[i] = true;
        col.edge_drops[i]    = col.height;
      } else {
        float diff = col.height - it->second->height;
        if (diff > 0.01f) {
          col.visible_edges[i] = true;
          col.edge_drops[i]    = diff;
        }
      }
    }
  }
}
