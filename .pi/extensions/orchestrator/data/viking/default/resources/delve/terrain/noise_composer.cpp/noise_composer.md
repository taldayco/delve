#include "terrain/noise_composer.h"
#include "terrain/contour.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <vector>



static void cleanup_small_regions(std::vector<float> &heightmap, int width,
                                  int height, int min_region_size) {
  int n = width * height;
  std::vector<bool> visited(n, false);
  std::vector<int> region_pixels;
  region_pixels.reserve(1000);

  for (int start_y = 0; start_y < height; ++start_y) {
    for (int start_x = 0; start_x < width; ++start_x) {
      int start_idx = start_y * width + start_x;
      if (visited[start_idx])
        continue;

      float region_height = heightmap[start_idx];
      region_pixels.clear();

      std::vector<int> stack;
      stack.push_back(start_idx);
      visited[start_idx] = true;

      while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        region_pixels.push_back(idx);

        int px = idx % width;
        int py = idx / width;

        int neighbors[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        for (auto [dx, dy] : neighbors) {
          int nx = px + dx, ny = py + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] &&
                std::abs(heightmap[nidx] - region_height) < 0.01f) {
              visited[nidx] = true;
              stack.push_back(nidx);
            }
          }
        }
      }

      if ((int)region_pixels.size() < min_region_size) {
        float sum = 0;
        int count = 0;

        for (int idx : region_pixels) {
          int px = idx % width;
          int py = idx / width;

          for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
              int nx = px + dx, ny = py + dy;
              if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                int nidx = ny * width + nx;
                if (std::abs(heightmap[nidx] - region_height) > 0.01f) {
                  sum += heightmap[nidx];
                  count++;
                }
              }
            }
          }
        }

        float replacement = (count > 0) ? sum / count : region_height;
        for (int idx : region_pixels) {
          heightmap[idx] = replacement;
        }
      }
    }
  }
}

void compose_layers(MapData &data, const ElevationParams &elev,
                    const RiverParams &river, const WorleyParams &worley,
                    const CompositionParams &comp, NoiseCache *cache) {
  int w = data.width;
  int h = data.height;
  int n = w * h;

  SDL_Log("Composing layers (%dx%d)...", w, h);
  auto start = SDL_GetTicks();


  RiverParams river_scaled = river;
  river_scaled.map_scale = elev.map_scale;
  WorleyParams worley_scaled = worley;
  worley_scaled.map_scale = elev.map_scale;


  uint64_t elev_hash = cache ? NoiseCache::hash_params(elev) : 0;
  uint64_t river_hash = cache ? NoiseCache::hash_params(river_scaled) : 0;
  uint64_t worley_hash = cache ? NoiseCache::hash_params(worley_scaled) : 0;

  if (!cache || !cache->get(NoiseCache::ELEVATION, elev_hash, data.elevation)) {
    generate_elevation_layer(data.elevation, w, h, elev);
    if (cache)
      cache->put(NoiseCache::ELEVATION, elev_hash, data.elevation);
    SDL_Log("  Elevation: generated");
  } else {
    SDL_Log("  Elevation: cache hit");
  }

  if (!cache || !cache->get(NoiseCache::RIVER, river_hash, data.river_mask)) {
    generate_river_mask(data.river_mask, w, h, river_scaled);
    if (cache)
      cache->put(NoiseCache::RIVER, river_hash, data.river_mask);
    SDL_Log("  River mask: generated");
  } else {
    SDL_Log("  River mask: cache hit");
  }

  if (!cache || !cache->get3(NoiseCache::WORLEY, worley_hash, data.worley,
                             data.worley_edge, data.worley_cell_value)) {
    generate_worley_layer(data.worley, data.worley_edge, data.worley_cell_value, w, h, worley_scaled);
    if (cache)
      cache->put3(NoiseCache::WORLEY, worley_hash, data.worley,
                  data.worley_edge, data.worley_cell_value);
    SDL_Log("  Worley: generated");
  } else {
    SDL_Log("  Worley: cache hit");
  }


  data.final_elevation = data.elevation;


  for (int i = 0; i < n; ++i) {
    data.basalt_height[i] =
        std::floor(data.final_elevation[i] * comp.terrace_levels) /
        comp.terrace_levels;
  }


  cleanup_small_regions(data.basalt_height, w, h, comp.min_region_size);

  SDL_Log("Layer composition: %llu ms", SDL_GetTicks() - start);
}
