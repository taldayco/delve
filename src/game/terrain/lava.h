
#pragma once
#include <cstdint>
#include <unordered_set>
#include <vector>

struct MapData;

struct LavaVertex {
  float x, y;
  float base_z;
};

struct LavaMesh {
  std::vector<LavaVertex> vertices;
  std::vector<uint32_t> indices;
  int grid_width = 0;
  int grid_height = 0;
  std::vector<uint8_t> active;
};

struct LavaBody {
  int plateau_index = -1;
  float height = 0.f;
  float min_x = 0, max_x = 0;
  float min_y = 0, max_y = 0;
  float aspect_ratio = 0.f;
  std::vector<int> pixels;
  float time_offset = 0.f;
  LavaMesh mesh;
  std::unordered_set<int> pixel_set;
};

struct FloodFillResult {
  std::vector<LavaBody> lava_bodies;
  std::vector<LavaBody> void_bodies;
};

FloodFillResult generate_lava_and_void(MapData &data, float void_chance, int seed = 0);
