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

        if ((int)component_pixels.size() >= MAX_COMPONENT_PIXELS) {
          capped = true;
          break;
        }

        int cx = idx % width, cy = idx / width;

        mn_x = std::min(mn_x, (float)cx);
        mx_x = std::max(mx_x, (float)cx);
        mn_y = std::min(mn_y, (float)cy);
        mx_y = std::max(mx_y, (float)cy);

        for (auto [dx, dy] : dirs) {
          int nx = cx + dx, ny = cy + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] && data.terrain_map[nidx] != TERRAIN_BASALT) {
              visited[nidx] = true;
              q.push(nidx);
            }
          }
        }
      }

      // If capped, drain remaining BFS queue (mark visited) and skip body.
      if (capped) {
        while (!q.empty()) {
          visited[q.front()] = true;
          q.pop();
        }
        continue;
      }

      if (component_pixels.size() < 50)
        continue;

      // Check global pixel budget before creating a new body.
      if (total_pixels_used + (int)component_pixels.size() > TOTAL_PIXEL_BUDGET) {
        SDL_Log("generate_lava_and_void: pixel budget exhausted (%d used, component %zu would exceed %d)",
                total_pixels_used, component_pixels.size(), TOTAL_PIXEL_BUDGET);
        budget_exceeded = true;
        continue;
      }

      bool is_void = dist(rng) < void_chance;
      int16_t terrain_type = is_void ? TERRAIN_VOID : TERRAIN_LAVA;

      LavaBody body;
      body.plateau_index = -1;
      body.height = 0.0f;
      body.min_x = mn_x;
      body.max_x = mx_x;
      body.min_y = mn_y;
      body.max_y = mx_y;
      float bw = mx_x - mn_x + 1.f, bh = mx_y - mn_y + 1.f;
      body.aspect_ratio = std::max(bw, bh) / std::max(1.0f, std::min(bw, bh));
      body.pixels = std::move(component_pixels);
      body.time_offset =
          (hash1d(body_index++) % 1000) / 1000.0f * 6.283185f;

      if (!is_void) {
        generate_lava_grid_mesh(body, width, height, 2.0f);
      }

      for (int idx : body.pixels) {
        if (data.terrain_map[idx] != TERRAIN_BASALT)
          data.terrain_map[idx] = terrain_type;
      }

      total_pixels_used += (int)body.pixels.size();

      // Pixels are no longer needed after terrain_map stamping — free them.
      body.pixels.clear();
      body.pixels.shrink_to_fit();

      if (is_void)
        result.void_bodies.push_back(std::move(body));
      else
        result.lava_bodies.push_back(std::move(body));
    }
  }

  SDL_Log("generate_lava_and_void: %zu lava bodies, %zu void bodies, %d total pixels used",
          result.lava_bodies.size(), result.void_bodies.size(), total_pixels_used);
  return result;
}
