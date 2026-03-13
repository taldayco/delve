#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

struct Vec2 {
  float x, y;
};

struct IsoVec2 {
  float x, y;
};

struct PixelBuffer {
  std::vector<uint32_t> pixels;
  int width;
  int height;
};

struct Camera3D {
  glm::vec3 position = glm::vec3(0.0f);
  float frustum_half_width  = 64.0f;
  float frustum_half_height = 64.0f;
  float near_plane = -500.0f;
  float far_plane  =  500.0f;
};
