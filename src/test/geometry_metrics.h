#pragma once
#include "terrain/terrain_mesh.h"
#include "terrain/hex.h"
#include <cmath>
#include <vector>

// Total vertex count across all basalt layers
inline size_t mesh_vertex_count(const TerrainMesh& mesh) {
  size_t total = 0;
  for (auto& layer : mesh.basalt_layers) {
    total += layer.vertices.size();
  }
  return total;
}

// Total index count across all basalt layers
inline size_t mesh_index_count(const TerrainMesh& mesh) {
  size_t total = 0;
  for (auto& layer : mesh.basalt_layers) {
    total += layer.indices.size();
  }
  return total;
}

// Count degenerate triangles (zero-area) in basalt layers
inline int mesh_degenerate_triangles(const TerrainMesh& mesh) {
  int count = 0;
  for (auto& layer : mesh.basalt_layers) {
    for (size_t i = 0; i + 2 < layer.indices.size(); i += 3) {
      auto& v0 = layer.vertices[layer.indices[i]];
      auto& v1 = layer.vertices[layer.indices[i + 1]];
      auto& v2 = layer.vertices[layer.indices[i + 2]];

      // Cross product of two edges
      float ax = v1.pos_x - v0.pos_x, ay = v1.pos_y - v0.pos_y, az = v1.pos_z - v0.pos_z;
      float bx = v2.pos_x - v0.pos_x, by = v2.pos_y - v0.pos_y, bz = v2.pos_z - v0.pos_z;
      float cx = ay * bz - az * by;
      float cy = az * bx - ax * bz;
      float cz = ax * by - ay * bx;
      float area = std::sqrt(cx * cx + cy * cy + cz * cz) * 0.5f;

      if (area < 1e-6f) count++;
    }
  }
  return count;
}

// Percentage of basalt vertex normals that are unit-length (within epsilon)
inline float normal_validity(const TerrainMesh& mesh, float epsilon = 0.01f) {
  size_t total = 0, valid = 0;
  for (auto& layer : mesh.basalt_layers) {
    for (auto& v : layer.vertices) {
      float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
      if (std::abs(len - 1.0f) < epsilon) valid++;
      total++;
    }
  }
  if (total == 0) return 100.0f;
  return 100.0f * valid / total;
}

// Verify hex_to_pixel and pixel_to_hex are inverse operations
inline float hex_roundtrip_accuracy(int test_range, float hex_size) {
  int tested = 0, passed = 0;
  for (int q = -test_range; q <= test_range; q++) {
    for (int r = -test_range; r <= test_range; r++) {
      float px, py;
      hex_to_pixel(q, r, hex_size, px, py);
      HexCoord result = pixel_to_hex(px, py, hex_size);
      if (result.q == q && result.r == r) passed++;
      tested++;
    }
  }
  if (tested == 0) return 100.0f;
  return 100.0f * passed / tested;
}

// Check that all vertex colors are in valid [0, 1] range
inline float vertex_color_validity(const TerrainMesh& mesh) {
  size_t total = 0, valid = 0;
  for (auto& layer : mesh.basalt_layers) {
    for (auto& v : layer.vertices) {
      bool ok = v.color_r >= 0.0f && v.color_r <= 1.0f &&
                v.color_g >= 0.0f && v.color_g <= 1.0f &&
                v.color_b >= 0.0f && v.color_b <= 1.0f;
      if (ok) valid++;
      total++;
    }
  }
  if (total == 0) return 100.0f;
  return 100.0f * valid / total;
}
