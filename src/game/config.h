#pragma once
#include <stdint.h>

struct Config {
  static constexpr int MAP_WIDTH  = 1024;
  static constexpr int MAP_HEIGHT = 1024;

  static constexpr float DEFAULT_MAP_SCALE = 1.0f;

  static constexpr float HEX_SIZE = 8.0f;
  static constexpr float MAP_WIDTH_UNITS  = MAP_WIDTH / HEX_SIZE;
  static constexpr float MAP_HEIGHT_UNITS = MAP_HEIGHT / HEX_SIZE;
  static constexpr float LAVA_GRID_SPACING = 10.0f;

  static constexpr float ISO_TW = 2.0f;
  static constexpr float ISO_TH = 1.0f;
  static constexpr float ISO_HS = 12.5f;

  static constexpr uint32_t LAVA_COLOR = 0xFFFF8C00;
  static constexpr float DEFAULT_CONTOUR_OPACITY = 0.35f;
};
