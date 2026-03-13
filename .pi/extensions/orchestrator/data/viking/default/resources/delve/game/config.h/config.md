#pragma once
#include <stdint.h>

struct Config {

  static inline int MAP_WIDTH = 1024;
  static inline int MAP_HEIGHT = 1024;


  static inline int WINDOW_WIDTH = 1450;
  static inline int WINDOW_HEIGHT = 1024;
  static inline int UI_PANEL_WIDTH = 376;


  static constexpr float WINDOW_WIDTH_PERCENT = 0.85f;
  static constexpr float WINDOW_HEIGHT_PERCENT = 0.85f;
  static constexpr float UI_PANEL_WIDTH_PERCENT = 0.25f;
  static constexpr int UI_PANEL_MIN_WIDTH = 400;


  static constexpr float DEFAULT_MAP_SCALE = 1.0f;


  static constexpr float HEX_SIZE = 8.0f;
  static constexpr float WORLD_UNIT = HEX_SIZE;
  static constexpr int   MAP_COLS   = 1024;
  static constexpr int   MAP_ROWS   = 1024;
  static constexpr float MAP_WIDTH_UNITS  = MAP_COLS / HEX_SIZE;
  static constexpr float MAP_HEIGHT_UNITS = MAP_ROWS / HEX_SIZE;
  static constexpr float LAVA_GRID_SPACING = 10.0f;
  static constexpr float HEIGHT_THRESHOLD = 0.02f;
  static constexpr int MIN_PLATEAU_SIZE = 50;


  static constexpr float DEFAULT_NOISE_SCALE = 0.003f;
  static constexpr int DEFAULT_NOISE_OCTAVES = 4;
  static constexpr float DEFAULT_NOISE_LACUNARITY = 1.752f;
  static constexpr float DEFAULT_NOISE_GAIN = 0.5f;
  static constexpr int DEFAULT_NOISE_SEED = 1337;
  static constexpr int DEFAULT_NOISE_LEVELS = 8;


  static constexpr float ISO_HEIGHT_SCALE = 0.5f;

  static constexpr float GRADIENT_SCALE = 2.0f;
  static constexpr uint32_t BACKGROUND_COLOR = 0xFF000000;
  static constexpr uint32_t LAVA_COLOR = 0xFFFF8C00;
  static constexpr float DEFAULT_CONTOUR_OPACITY = 0.35f;
};
