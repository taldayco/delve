#pragma once
#include "terrain/terrain_mesh.h"
#include "terrain/hex.h"
#include <cmath>

inline size_t mesh_vertex_count(const TerrainMesh &m) {
  size_t total = 0;
  for (auto &layer : m.basalt_layers) total += layer.vertices.size();
  return total;
}

inline size_t mesh_index_count(const TerrainMesh &m) {
  size_t total = 0;
  for (auto &layer : m.basalt_layers) total += layer.indices.size();
  return total;
}

inline int mesh_degenerate_triangles(const TerrainMesh &m) {
  int count = 0;
  for (auto &layer : m.basalt_layers) {
    for (size_t i = 0; i + 2 < layer.indices.size(); i += 3) {
      auto &a = layer.vertices[layer.indices[i]];
      auto &b = layer.vertices[layer.indices[i+1]];
      auto &c = layer.vertices[layer.indices[i+2]];
      float abx = b.pos_x - a.pos_x, aby = b.pos_y - a.pos_y, abz = b.pos_z - a.pos_z;
      float acx = c.pos_x - a.pos_x, acy = c.pos_y - a.pos_y, acz = c.pos_z - a.pos_z;
      float cx = aby*acz - abz*acy;
      float cy = abz*acx - abx*acz;
      float cz = abx*acy - aby*acx;
      if (cx*cx + cy*cy + cz*cz < 1e-12f) ++count;
    }
  }
  return count;
}

inline float normal_validity(const TerrainMesh &m) {
  size_t total = 0, valid = 0;
  for (auto &layer : m.basalt_layers) {
    for (auto &v : layer.vertices) {
      ++total;
      float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
      if (std::abs(len - 1.0f) < 0.01f) ++valid;
    }
  }
  return total > 0 ? (float)valid / total : 0;
}

inline float hex_roundtrip_accuracy(float hex_size, int range) {
  int total = 0, accurate = 0;
  for (int q = -range; q <= range; ++q) {
    for (int r = -range; r <= range; ++r) {
      float px, py;
      hex_to_pixel(q, r, hex_size, px, py);
      HexCoord back = pixel_to_hex(px, py, hex_size);
      ++total;
      if (back.q == q && back.r == r) ++accurate;
    }
  }
  return total > 0 ? (float)accurate / total : 0;
}

inline float vertex_color_validity(const TerrainMesh &m) {
  size_t total = 0, valid = 0;
  for (auto &layer : m.basalt_layers) {
    for (auto &v : layer.vertices) {
      ++total;
      if (v.color_r >= 0 && v.color_r <= 1 &&
          v.color_g >= 0 && v.color_g <= 1 &&
          v.color_b >= 0 && v.color_b <= 1)
        ++valid;
    }
  }
  return total > 0 ? (float)valid / total : 0;
}
