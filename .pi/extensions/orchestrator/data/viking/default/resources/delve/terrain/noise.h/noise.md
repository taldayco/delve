#pragma once
#include <span>

struct NoiseParams {
  float frequency = 0.005f;
  int octaves = 6;
  float lacunarity = 2.0f;
  float gain = 0.5f;
  int seed = 1337;
  int terrace_levels = 8;
  int min_region_size = 153;
};

void generate_heightmap(std::span<float> out, int width, int height,
                        const NoiseParams &params, float map_scale = 1.0f);
