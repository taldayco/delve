#include "terrain/noise_layers.h"
#include "terrain/FastNoiseLite.h"
#include <algorithm>
#include <cmath>
#include <vector>



static void seed_offset(int seed, float &ox, float &oy) {
  unsigned int h = static_cast<unsigned int>(seed);
  h ^= h >> 13;
  h *= 0x5bd1e995;
  h ^= h >> 15;
  ox = static_cast<float>(h % 10000) + 1000.0f;
  h ^= h >> 13;
  h *= 0x5bd1e995;
  h ^= h >> 15;
  oy = static_cast<float>(h % 10000) + 1000.0f;
}



static float biased_smoothstep(float t, float bias) {
  float smooth = t * t * (3.0f - 2.0f * t);
  float ease_out = 1.0f - (1.0f - t) * (1.0f - t);
  return smooth * (1.0f - bias) + ease_out * bias;
}

void generate_elevation_layer(std::vector<float> &out, int width, int height,
                              const ElevationParams &params) {
  int n = width * height;
  out.resize(n);

  std::vector<float> gradient_x(n, 0.0f);
  std::vector<float> gradient_y(n, 0.0f);
  std::fill(out.begin(), out.end(), 0.0f);

  FastNoiseLite noise(params.seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(FastNoiseLite::FractalType_None);

  float amplitude = 1.0f;
  float frequency = params.frequency;
  float max_value = 0.0f;

  constexpr float GRADIENT_SCALE = 2.0f;

  float ox, oy;
  seed_offset(params.seed, ox, oy);

  for (int octave = 0; octave < params.octaves; ++octave) {
    noise.SetFrequency(frequency);

    std::vector<float> octave_values(n);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        float world_x = (float)x * params.map_scale + ox;
        float world_y = (float)y * params.map_scale + oy;
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


  for (int i = 0; i < n; ++i) {
    out[i] = (out[i] / max_value + 1.0f) * 0.5f;
  }


  for (int i = 0; i < n; ++i) {
    out[i] = biased_smoothstep(out[i], params.scurve_bias);
  }
}

void generate_river_mask(std::vector<float> &out, int width, int height,
                         const RiverParams &params) {
  int n = width * height;
  out.resize(n);

  FastNoiseLite noise(params.seed);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
  noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
  noise.SetFrequency(params.frequency);
  noise.SetFractalOctaves(params.octaves);
  noise.SetFractalLacunarity(params.lacunarity);
  noise.SetFractalGain(params.gain);

  float ox, oy;
  seed_offset(params.seed, ox, oy);

  float min_val = 1e9f, max_val = -1e9f;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float v = noise.GetNoise((float)x * params.map_scale + ox, (float)y * params.map_scale + oy);
      out[y * width + x] = v;
      min_val = std::min(min_val, v);
      max_val = std::max(max_val, v);
    }
  }


  float range = max_val - min_val;
  if (range > 1e-6f) {
    for (int i = 0; i < n; ++i) {
      out[i] = (out[i] - min_val) / range;
    }
  }
}

void generate_worley_layer(std::vector<float> &out_value,
                           std::vector<float> &out_edge,
                           std::vector<float> &out_cell_value, int width, int height,
                           const WorleyParams &params) {
  int n = width * height;
  out_value.resize(n);
  out_edge.resize(n);
  out_cell_value.resize(n);