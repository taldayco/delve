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

  // Physical Scale: 1 basalt hex = 2 feet wide (flat-to-flat)
  static constexpr float HEX_DIAMETER_FEET       = 2.0f;
  static constexpr float WORLD_UNITS_PER_FOOT     = HEX_SIZE / HEX_DIAMETER_FEET; // 4.0
  static constexpr float BASALT_LAYER_HEIGHT_FEET = 0.5f;
  static constexpr float BASALT_LAYER_HEIGHT_WU   = BASALT_LAYER_HEIGHT_FEET * WORLD_UNITS_PER_FOOT; // 2.0

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

  static constexpr int ACTOR_MAX_VERTICES = 500;
  static constexpr int ACTOR_BONE_INFLUENCES = 2;
  static constexpr int ACTOR_DEFAULT_CROSS_SECTION_SIDES = 4;

  // Actor Physical Proportions (adult male, 8-head canon)
  // One basalt hex is 2 ft wide; HEX_SIZE = 8.0 WU → 4.0 WU/ft.
  static constexpr float HEX_REAL_WIDTH_FT        = 2.0f;
  static constexpr float HEX_REAL_HEIGHT_FT       = HEX_REAL_WIDTH_FT;
  static constexpr float WU_PER_FT                = HEX_SIZE / HEX_REAL_WIDTH_FT; // 4.0

  static constexpr float ACTOR_HEIGHT_FEET        = 5.9f;
  static constexpr float ACTOR_HEIGHT_WU          = ACTOR_HEIGHT_FEET * WORLD_UNITS_PER_FOOT;       // 23.6
  // Legacy alias kept for existing code
  static constexpr float ACTOR_HEIGHT_FT          = ACTOR_HEIGHT_FEET;
  static constexpr float ACTOR_TOTAL_HEIGHT_WU    = ACTOR_HEIGHT_WU;

  // Head
  static constexpr float ACTOR_HEAD_HEIGHT_WU     = ACTOR_HEIGHT_WU / 8.0f;                        // head = 1/8 total
  static constexpr float ACTOR_HEAD_WIDTH_WU      = ACTOR_HEAD_HEIGHT_WU * 0.75f;                  // head w = 3/4 head h
  // Torso
  static constexpr float ACTOR_TORSO_HEIGHT_WU    = ACTOR_HEIGHT_WU * 0.375f;                      // 3/8 total
  static constexpr float ACTOR_SHOULDER_WIDTH_WU  = ACTOR_HEIGHT_WU * 0.375f;                      // 3/8 total
  static constexpr float ACTOR_HIP_WIDTH_WU       = ACTOR_HEIGHT_WU * 0.25f;                       // 1/4 total
  static constexpr float ACTOR_WAIST_WIDTH_WU     = ACTOR_HEIGHT_WU * (5.0f / 16.0f);              // 5/16 total
  // Legs
  static constexpr float ACTOR_UPPER_LEG_WU       = ACTOR_HEIGHT_WU * 0.25f;                       // 1/4 total
  static constexpr float ACTOR_LOWER_LEG_WU       = ACTOR_HEIGHT_WU * 0.25f;                       // 1/4 total
  static constexpr float ACTOR_FOOT_LENGTH_WU     = ACTOR_HEIGHT_WU / 7.0f;                        // 1/7 total
  static constexpr float ACTOR_FOOT_LEN_WU        = ACTOR_FOOT_LENGTH_WU;                          // legacy alias
  // Arms
  static constexpr float ACTOR_UPPER_ARM_WU       = ACTOR_HEIGHT_WU * (3.0f / 16.0f);              // 3/16 total
  static constexpr float ACTOR_FOREARM_WU         = ACTOR_HEIGHT_WU * 0.125f;                      // 1/8 total
  static constexpr float ACTOR_HAND_LENGTH_WU     = ACTOR_HEIGHT_WU * 0.1f;                        // 1/10 total
  static constexpr float ACTOR_HAND_LEN_WU        = ACTOR_HAND_LENGTH_WU;                          // legacy alias
  // Neck
  static constexpr float ACTOR_NECK_HEIGHT_WU     = ACTOR_HEIGHT_WU / 32.0f;
};
