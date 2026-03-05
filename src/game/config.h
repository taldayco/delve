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


  // World-space coordinate system: 1 world unit = 2 feet.
  // One basalt column is 2 feet wide/long, so it spans exactly 1 world unit.
  // HEX_SIZE is the pixel/screen-space size of one hex (rendering only).
  static constexpr float HEX_SIZE = 2.0f;
  // WORLD_UNIT: feet per world unit (1 world unit = 2 feet = 1 basalt column).
  static constexpr float WORLD_UNIT = 2.0f;
  // BASALT_COLUMN_FEET: width and length of one basalt column in world-space feet.
  static constexpr float BASALT_COLUMN_FEET = 2.0f;
  // HUMAN_HEIGHT_FEET: average human height (~5.9 ft = 8 head-lengths).
  static constexpr float HUMAN_HEIGHT_FEET = 5.9f;
  // HEAD_HEIGHT_FEET: one head-length (HUMAN_HEIGHT_FEET / 8).
  static constexpr float HEAD_HEIGHT_FEET = HUMAN_HEIGHT_FEET / 8.0f;
  static constexpr int   MAP_COLS   = 1024;
  static constexpr int   MAP_ROWS   = 1024;
  static constexpr float MAP_WIDTH_UNITS  = MAP_COLS / HEX_SIZE;
  static constexpr float MAP_HEIGHT_UNITS = MAP_ROWS / HEX_SIZE;
  static constexpr float LAVA_GRID_SPACING = 2.5f;
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
