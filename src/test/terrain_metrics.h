#pragma once
#include "terrain/map_data.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <vector>

struct ElevationStats {
  float min, max, mean, stddev;
};

inline ElevationStats elevation_stats(std::span<const float> data) {
  if (data.empty()) return {0, 0, 0, 0};
  float mn = data[0], mx = data[0], sum = 0;
  for (float v : data) { mn = std::min(mn, v); mx = std::max(mx, v); sum += v; }
  float mean = sum / data.size();
  float var = 0;
  for (float v : data) { float d = v - mean; var += d * d; }
  return {mn, mx, mean, std::sqrt(var / data.size())};
}

inline std::vector<int> elevation_distribution(std::span<const float> data, int bands) {
  std::vector<int> hist(bands, 0);
  for (float v : data) {
    int b = std::clamp((int)(v * bands), 0, bands - 1);
    hist[b]++;
  }
  return hist;
}

inline float water_coverage_percent(std::span<const float> data, float threshold = 0.1f) {
  if (data.empty()) return 0;
  int count = 0;
  for (float v : data) if (v < threshold) ++count;
  return 100.0f * count / data.size();
}

inline int plateau_count(std::span<const int> band_map, int width, int height) {
  int mx = 0;
  for (int v : band_map) mx = std::max(mx, v);
  return mx;
}

inline ElevationStats hex_column_height_stats(const std::vector<HexColumn> &cols) {
  if (cols.empty()) return {0, 0, 0, 0};
  std::vector<float> heights;
  heights.reserve(cols.size());
  for (auto &c : cols) heights.push_back(c.height);
  return elevation_stats(heights);
}

inline int lava_body_count(const MapData &md) { return (int)md.lava_bodies.size(); }

inline float void_region_ratio(const MapData &md) {
  if (md.terrain_map.empty()) return 0;
  int count = 0;
  for (auto v : md.terrain_map) if (v == TERRAIN_VOID) ++count;
  return (float)count / md.terrain_map.size();
}

inline float lava_region_ratio(const MapData &md) {
  if (md.terrain_map.empty()) return 0;
  int count = 0;
  for (auto v : md.terrain_map) if (v == TERRAIN_LAVA) ++count;
  return (float)count / md.terrain_map.size();
}

inline float basalt_coverage_ratio(const MapData &md) {
  if (md.terrain_map.empty()) return 0;
  int count = 0;
  for (auto v : md.terrain_map) if (v == TERRAIN_BASALT) ++count;
  return (float)count / md.terrain_map.size();
}
