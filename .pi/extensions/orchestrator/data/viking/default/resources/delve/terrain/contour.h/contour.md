#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct Line {
  float x1, y1, x2, y2;
  float elevation;
};

void extract_contours(std::span<const float> heightmap, int width, int height,
                      float interval, std::vector<Line> &out_lines,
                      std::vector<int> &out_band_map);

// Remove contour segments shorter than epsilon (world units) — call after extract_contours.
void simplify_contours(std::vector<Line> &lines, float epsilon);

struct Plateau {
  float height;
  std::vector<int> pixels;
  float center_x, center_y;
  float min_x, max_x, min_y, max_y;
};

std::vector<Plateau> detect_plateaus(std::span<const int> band_map,
                                     std::span<const float> heightmap,
                                     int width, int height,
                                     std::vector<int16_t>& terrain_map);
