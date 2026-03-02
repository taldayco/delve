#pragma once
#include "terrain/map_data.h"
#include "terrain/contour.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

struct HeightStats {
  float min, max, mean, stddev;
};

struct ElevationHistogram {
  int bands;
  std::vector<int> counts;
  std::vector<float> thresholds;
};

// Compute min, max, mean, stddev of elevation values
inline HeightStats elevation_stats(const std::vector<float>& elevation) {
  if (elevation.empty()) return {0, 0, 0, 0};

  float mn = elevation[0], mx = elevation[0];
  double sum = 0;
  for (float e : elevation) {
    mn = std::min(mn, e);
    mx = std::max(mx, e);
    sum += e;
  }
  float mean = (float)(sum / elevation.size());

  double var_sum = 0;
  for (float e : elevation) {
    double d = e - mean;
    var_sum += d * d;
  }
  float stddev = (float)std::sqrt(var_sum / elevation.size());

  return {mn, mx, mean, stddev};
}

// Build a histogram of elevation values across N bands in [0, 1]
inline ElevationHistogram elevation_distribution(const std::vector<float>& elevation, int bands = 10) {
  ElevationHistogram hist;
  hist.bands = bands;
  hist.counts.assign(bands, 0);
  hist.thresholds.resize(bands + 1);
  for (int i = 0; i <= bands; i++) hist.thresholds[i] = (float)i / bands;

  for (float e : elevation) {
    int idx = std::clamp((int)(e * bands), 0, bands - 1);
    hist.counts[idx]++;
  }
  return hist;
}

// Percentage of map below a water threshold
inline float water_coverage_percent(const std::vector<float>& elevation, float threshold = 0.2f) {
  if (elevation.empty()) return 0;
  int count = 0;
  for (float e : elevation) {
    if (e < threshold) count++;
  }
  return 100.0f * count / elevation.size();
}

// Count distinct plateaus
inline int plateau_count(const std::vector<Plateau>& plateaus) {
  return (int)plateaus.size();
}

// Height stats across hex columns
inline HeightStats hex_column_height_stats(const std::vector<HexColumn>& columns) {
  if (columns.empty()) return {0, 0, 0, 0};

  float mn = columns[0].height, mx = columns[0].height;
  double sum = 0;
  for (auto& c : columns) {
    mn = std::min(mn, c.height);
    mx = std::max(mx, c.height);
    sum += c.height;
  }
  float mean = (float)(sum / columns.size());

  double var_sum = 0;
  for (auto& c : columns) {
    double d = c.height - mean;
    var_sum += d * d;
  }
  float stddev = (float)std::sqrt(var_sum / columns.size());

  return {mn, mx, mean, stddev};
}

// Count lava bodies
inline int lava_body_count(const MapData& map) {
  return (int)map.lava_bodies.size();
}

// Percentage of terrain_map pixels that are void
inline float void_region_ratio(const MapData& map) {
  if (map.terrain_map.empty()) return 0;
  int count = 0;
  for (int16_t t : map.terrain_map) {
    if (t == TERRAIN_VOID) count++;
  }
  return 100.0f * count / map.terrain_map.size();
}

// Percentage of terrain_map pixels that are lava
inline float lava_region_ratio(const MapData& map) {
  if (map.terrain_map.empty()) return 0;
  int count = 0;
  for (int16_t t : map.terrain_map) {
    if (t == TERRAIN_LAVA) count++;
  }
  return 100.0f * count / map.terrain_map.size();
}

// Percentage of terrain_map pixels that are basalt (solid ground)
inline float basalt_coverage_ratio(const MapData& map) {
  if (map.terrain_map.empty()) return 0;
  int count = 0;
  for (int16_t t : map.terrain_map) {
    if (t == TERRAIN_BASALT) count++;
  }
  return 100.0f * count / map.terrain_map.size();
}
