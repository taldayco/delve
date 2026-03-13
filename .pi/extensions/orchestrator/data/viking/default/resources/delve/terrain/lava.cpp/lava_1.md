#include "terrain/lava.h"
#include "terrain/basalt.h"
#include "terrain/map_data.h"
#include "config.h"
#include "terrain/util.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <random>
#include <unordered_set>
#include <vector>

static void generate_lava_grid_mesh(LavaBody &lava, int width, int height, float grid_spacing) {
  lava.mesh.vertices.clear();
  lava.mesh.indices.clear();

  if (lava.pixels.empty()) return;

  // Check grid dimensions BEFORE allocating pixel_set to avoid ~50MB
  // allocation for pathologically large lava bodies.
  int nx = (int)std::ceil((lava.max_x - lava.min_x) / grid_spacing) + 1;
  int ny = (int)std::ceil((lava.max_y - lava.min_y) / grid_spacing) + 1;

  constexpr int MAX_LAVA_GRID_CELLS = 200 * 200; // ~40k cells max
  if (nx * ny > MAX_LAVA_GRID_CELLS) return;

  // NOW build pixel_set (bounded since grid passed the guard)
  if (lava.pixel_set.empty()) {
    for (int idx : lava.pixels) lava.pixel_set.insert(idx);
  }

  auto is_lava = [&](float x, float y) {
    int ix = (int)std::round(x);
    int iy = (int)std::round(y);
    if (ix < 0 || ix >= width || iy < 0 || iy >= height) return false;
    return lava.pixel_set.count(iy * width + ix) > 0;
  };

  std::vector<int> vertex_map(nx * ny, -1);

  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      float wx = lava.min_x + i * grid_spacing;
      float wy = lava.min_y + j * grid_spacing;

      if (is_lava(wx, wy)) {
        vertex_map[j * nx + i] = (int)lava.mesh.vertices.size();
        lava.mesh.vertices.push_back({wx, wy, lava.height});
      }
    }
  }

  for (int j = 0; j < ny - 1; ++j) {
    for (int i = 0; i < nx - 1; ++i) {
      int i00 = vertex_map[j * nx + i];
      int i10 = vertex_map[j * nx + (i + 1)];
      int i01 = vertex_map[(j + 1) * nx + i];
      int i11 = vertex_map[(j + 1) * nx + (i + 1)];

      if (i00 != -1 && i10 != -1 && i01 != -1) {
        lava.mesh.indices.push_back(i00);
        lava.mesh.indices.push_back(i10);
        lava.mesh.indices.push_back(i01);
      }
      if (i10 != -1 && i11 != -1 && i01 != -1) {
        lava.mesh.indices.push_back(i10);
        lava.mesh.indices.push_back(i11);
        lava.mesh.indices.push_back(i01);
      }
    }
  }

  // pixel_set is only needed during mesh generation — free it now so that
  // destructing a LavaBody on the main thread does not stall the render loop.
  lava.pixel_set.clear();
  { std::unordered_set<int> empty; lava.pixel_set.swap(empty); }
}

FloodFillResult generate_lava_and_void(MapData &data, float void_chance, int seed) {
  int width = data.width;
  int height = data.height;
  int n = width * height;

  std::vector<bool> visited(n, false);
  FloodFillResult result;

  const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  // Per-component cap: anything larger is map-spanning junk.
  constexpr int MAX_COMPONENT_PIXELS = 50'000;
  // Global budget: stop creating bodies once we've accumulated this many pixels
  // across all bodies. Prevents compound memory blow-up with many medium bodies.
  constexpr int TOTAL_PIXEL_BUDGET = 200'000;
  int total_pixels_used = 0;

  uint32_t rng_seed = 0xDEADBEEFu;
  rng_seed ^= (uint32_t)width + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  rng_seed ^= (uint32_t)height + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  rng_seed ^= (uint32_t)seed + 0x9e3779b9u + (rng_seed << 6) + (rng_seed >> 2);
  std::mt19937 rng(rng_seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  int body_index = 0;
  bool budget_exceeded = false;

  for (int sy = 0; sy < height && !budget_exceeded; ++sy) {
    for (int sx = 0; sx < width && !budget_exceeded; ++sx) {
      int start = sy * width + sx;
      if (visited[start] || data.terrain_map[start] == TERRAIN_BASALT)
        continue;

      std::vector<int> component_pixels;
      component_pixels.reserve(std::min(MAX_COMPONENT_PIXELS, TOTAL_PIXEL_BUDGET - total_pixels_used));
      std::queue<int> q;
      q.push(start);
      visited[start] = true;

      float mn_x = (float)sx, mx_x = (float)sx;
      float mn_y = (float)sy, mx_y = (float)sy;

      bool capped = false;
      while (!q.empty()) {
        int idx = q.front();
        q.pop();
        component_pixels.push_back(idx);