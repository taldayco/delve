#pragma once
#include "terrain/terrain_mesh.h"
#include "terrain/hex.h"
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

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
      // Cross product magnitude
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

// Count the number of line edges (pairs of indices) in a skeleton mesh index buffer.
// The skeleton pipeline uses LINELIST, so every 2 indices = 1 edge.
inline size_t count_line_edges(const std::vector<uint32_t> &indices) {
  return indices.size() / 2;
}

// Returns true if all indices in `indices` are < vertex_count (no out-of-bounds).
inline bool line_indices_valid(const std::vector<uint32_t> &indices, size_t vertex_count) {
  for (uint32_t idx : indices) {
    if (idx >= (uint32_t)vertex_count) return false;
  }
  return true;
}

// Expected number of line indices for a skeleton mesh given per-bone side counts.
// Each bone with `sides[i]` sides emits sides[i] * 3 edges * 2 indices.
inline size_t wireframe_edge_count_expected(const std::vector<int> &sides_per_bone) {
  size_t total = 0;
  for (int s : sides_per_bone) total += (size_t)s * 3 * 2;
  return total;
}

// Convenience overload: all bones have the same side count.
inline size_t wireframe_edge_count_expected(int bone_count, int sides) {
  return (size_t)(bone_count) * (size_t)(sides) * 3 * 2;
}

// Count edges where both index endpoints are identical (degenerate).
inline int count_degenerate_edges(const std::vector<uint32_t> &indices) {
  int count = 0;
  for (size_t i = 0; i + 1 < indices.size(); i += 2) {
    if (indices[i] == indices[i + 1]) ++count;
  }
  return count;
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

// Count edges in a line-list index buffer where both endpoint vertices have
// identical world positions (zero-length wireframe edges).
// vertices_pos: flat list of vec3 positions parallel to the vertex array.
inline int count_zero_length_edges(const std::vector<uint32_t> &indices,
                                   const std::vector<glm::vec3> &positions,
                                   float eps = 1e-6f) {
  int count = 0;
  for (size_t i = 0; i + 1 < indices.size(); i += 2) {
    const glm::vec3 &a = positions[indices[i]];
    const glm::vec3 &b = positions[indices[i + 1]];
    float d = (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z);
    if (d < eps * eps) ++count;
  }
  return count;
}

// Check that all vertex positions in the line-list are within `max_dist` of
// at least one of the provided skeleton joint positions.
inline bool all_wireframe_verts_near_skeleton(
    const std::vector<glm::vec3> &vert_positions,
    const std::vector<glm::vec3> &joints,
    float max_dist) {
  for (const glm::vec3 &p : vert_positions) {
    float best = 1e30f;
    for (const glm::vec3 &j : joints) {
      float d = (p.x-j.x)*(p.x-j.x) + (p.y-j.y)*(p.y-j.y) + (p.z-j.z)*(p.z-j.z);
      if (d < best) best = d;
    }
    if (best > max_dist * max_dist) return false;
  }
  return true;
}
