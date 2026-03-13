
#pragma once
#include "color.h"
#include <algorithm>
#include <cstdint>

struct Palette {
  const char *name;
  uint32_t colors[6];
};

static const Palette PALETTES[] = {
    {"Grayscale",
     {0xFF3A3A3A, 0xFF2E2E2E, 0xFF222222, 0xFF161616, 0xFF000000, 0xFF000000}}};

static constexpr int PALETTE_COUNT = sizeof(PALETTES) / sizeof(Palette);

inline uint32_t get_elevation_color_smooth(float h, const Palette &p) {
  h = std::clamp(h, 0.0f, 1.0f);

  if (h < 0.25f) {
    float t = h / 0.25f;
    return lerp_color(p.colors[0], p.colors[1], t);
  } else if (h < 0.5f) {
    float t = (h - 0.25f) / 0.25f;
    return lerp_color(p.colors[1], p.colors[2], t);
  } else if (h < 0.75f) {
    float t = (h - 0.5f) / 0.25f;
    return lerp_color(p.colors[2], p.colors[3], t);
  } else {
    float t = (h - 0.75f) / 0.25f;
    return lerp_color(p.colors[3], p.colors[4], t);
  }
}

inline uint32_t organic_color(float h, int x, int y, const Palette &p) {
  uint32_t base = get_elevation_color_smooth(h, p);
  return add_noise_variation(base, x, y);
}

inline uint32_t get_elevation_color(float h, const Palette &p) {
  if (h < 0.2f)
    return p.colors[0];
  if (h < 0.4f)
    return p.colors[1];
  if (h < 0.6f)
    return p.colors[2];
  if (h < 0.8f)
    return p.colors[3];
  return p.colors[4];
}
