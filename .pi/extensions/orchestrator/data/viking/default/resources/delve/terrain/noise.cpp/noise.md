#include "terrain/noise.h"
#include "terrain/FastNoiseLite.h"
#include <algorithm>
#include <cmath>
#include <vector>

void generate_heightmap(std::span<float> out, int width, int height,
                        const NoiseParams &params, float map_scale) {
  std::vector<float> gradient_x(width * height, 0.0f);
  std::vector<float> gradient_y(width * height, 0.0f);
  std::fill(out.begin(), out.end(), 0.0f);

  FastNoiseLite noise(params.seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(FastNoiseLite::FractalType_None);

  float amplitude = 1.0f;
  float frequency = params.frequency;
  float max_value = 0.0f;

  constexpr float GRADIENT_SCALE = 2.0f;

  for (int octave = 0; octave < params.octaves; ++octave) {
    noise.SetFrequency(frequency);

    std::vector<float> octave_values(width * height);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        float world_x = (float)x * map_scale;
        float world_y = (float)y * map_scale;
        octave_values[y * width + x] = noise.GetNoise(world_x, world_y);
      }
    }

    for (int y = 1; y < height - 1; ++y) {
      for (int x = 1; x < width - 1; ++x) {
        int idx = y * width + x;
        float grad_magnitude = std::sqrt(gradient_x[idx] * gradient_x[idx] +
                                         gradient_y[idx] * gradient_y[idx]);

        float erosion_factor = 1.0f / (1.0f + grad_magnitude * GRADIENT_SCALE);
        float scaled_amplitude = amplitude * erosion_factor;

        out[idx] += octave_values[idx] * scaled_amplitude;

        float dx = (octave_values[y * width + (x + 1)] -
                    octave_values[y * width + (x - 1)]) *
                   0.5f;
        float dy = (octave_values[(y + 1) * width + x] -
                    octave_values[(y - 1) * width + x]) *
                   0.5f;

        gradient_x[idx] += dx * scaled_amplitude * frequency;
        gradient_y[idx] += dy * scaled_amplitude * frequency;
      }
    }

    max_value += amplitude;
    amplitude *= params.gain;
    frequency *= params.lacunarity;
  }
  for (int x = 0; x < width; ++x) {
    out[x] = out[width + x];
    out[(height - 1) * width + x] = out[(height - 2) * width + x];
  }
  for (int y = 0; y < height; ++y) {
    out[y * width] = out[y * width + 1];
    out[y * width + (width - 1)] = out[y * width + (width - 2)];
  }
  for (int i = 0; i < width * height; ++i) {
    out[i] = (out[i] / max_value + 1.0f) * 0.5f;
  }

  constexpr int terrace_levels = 8;
  for (int i = 0; i < width * height; ++i) {
    float level = std::floor(out[i] * terrace_levels) / terrace_levels;
    out[i] = level;
  }
  std::vector<float> smoothed(width * height);
  const int MIN_REGION_SIZE = params.min_region_size;

  std::vector<bool> visited(width * height, false);
  std::vector<int> region_pixels;
  region_pixels.reserve(1000);

  for (int start_y = 0; start_y < height; ++start_y) {
    for (int start_x = 0; start_x < width; ++start_x) {
      int start_idx = start_y * width + start_x;
      if (visited[start_idx])
        continue;

      float region_height = out[start_idx];
      region_pixels.clear();

      std::vector<int> stack;
      stack.push_back(start_idx);
      visited[start_idx] = true;

      while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        region_pixels.push_back(idx);

        int x = idx % width;
        int y = idx / width;

        int neighbors[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        for (auto [dx, dy] : neighbors) {
          int nx = x + dx, ny = y + dy;
          if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
            int nidx = ny * width + nx;
            if (!visited[nidx] && std::abs(out[nidx] - region_height) < 0.01f) {
              visited[nidx] = true;
              stack.push_back(nidx);
            }
          }
        }
      }

      if (region_pixels.size() < MIN_REGION_SIZE) {
        float sum = 0;
        int count = 0;

        for (int idx : region_pixels) {
          int x = idx % width;
          int y = idx / width;

          for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
              int nx = x + dx, ny = y + dy;
              if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                int nidx = ny * width + nx;
                if (std::abs(out[nidx] - region_height) > 0.01f) {
                  sum += out[nidx];
                  count++;
                }
              }
            }
          }
        }

        float replacement = (count > 0) ? sum / count : region_height;
        for (int idx : region_pixels) {
          out[idx] = replacement;
        }
      }
    }
  }
}
