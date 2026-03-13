#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

inline uint32_t lerp_color(uint32_t c1, uint32_t c2, float t) {
  uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
  uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;

  uint8_t r = r1 + (r2 - r1) * t;
  uint8_t g = g1 + (g2 - g1) * t;
  uint8_t b = b1 + (b2 - b1) * t;

  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline uint32_t add_noise_variation(uint32_t color, int x, int y,
                                    float strength = 0.08f) {
  uint32_t hash = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177;
  float noise = ((hash & 0xFFFF) / 65535.0f - 0.5f) * strength;

  int r = std::clamp((int)((color >> 16 & 0xFF) * (1.0f + noise)), 0, 255);
  int g = std::clamp((int)((color >> 8 & 0xFF) * (1.0f + noise)), 0, 255);
  int b = std::clamp((int)((color & 0xFF) * (1.0f + noise)), 0, 255);

  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline uint32_t darken_color(uint32_t color, float darkness) {
  float factor = 1.0f - darkness;
  uint8_t r = ((color >> 16) & 0xFF) * factor;
  uint8_t g = ((color >> 8) & 0xFF) * factor;
  uint8_t b = (color & 0xFF) * factor;
  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline uint32_t alpha_blend(uint32_t src, uint32_t dst, float alpha) {
  uint8_t sr = (src >> 16) & 0xFF;
  uint8_t sg = (src >> 8) & 0xFF;
  uint8_t sb = src & 0xFF;
  uint8_t dr = (dst >> 16) & 0xFF;
  uint8_t dg = (dst >> 8) & 0xFF;
  uint8_t db = dst & 0xFF;
  uint8_t r = (uint8_t)(sr * alpha + dr * (1.0f - alpha));
  uint8_t g = (uint8_t)(sg * alpha + dg * (1.0f - alpha));
  uint8_t b = (uint8_t)(sb * alpha + db * (1.0f - alpha));
  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline uint32_t modulate_color(uint32_t color, float factor) {
  uint8_t r = (uint8_t)(((color >> 16) & 0xFF) * factor);
  uint8_t g = (uint8_t)(((color >> 8) & 0xFF) * factor);
  uint8_t b = (uint8_t)((color & 0xFF) * factor);
  return 0xFF000000 | (r << 16) | (g << 8) | b;
}

inline void apply_hex_dither(std::vector<uint32_t> &pixels, int width,
                             int height, float strength,
                             uint32_t skip_color = 0) {
  const float HEX_SIZE = 8.0f;
  const float sqrt3 = 1.732f;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int idx = y * width + x;
      uint32_t pixel = pixels[idx];

      if (skip_color != 0 && pixel == skip_color)
        continue;

      float q = x / (HEX_SIZE * sqrt3);
      float r = y / HEX_SIZE - q * 0.5f;

      int iq = (int)std::round(q);
      int ir = (int)std::round(r);
      int is = (int)std::round(-q - r);

      float dq = std::abs(iq - q);
      float dr = std::abs(ir - r);
      float ds = std::abs(is - (-q - r));

      if (dq > dr && dq > ds) {
        iq = -ir - is;
      } else if (dr > ds) {
        ir = -iq - is;
      }

      uint32_t hash = ((uint32_t)iq * 374761393) ^ ((uint32_t)ir * 668265263);
      float threshold = ((hash & 0xFF) / 255.0f - 0.5f) * strength;

      uint8_t rc = std::clamp(
          (int)(((pixel >> 16) & 0xFF) * (1.0f + threshold)), 0, 255);
      uint8_t gc = std::clamp(
          (int)(((pixel >> 8) & 0xFF) * (1.0f + threshold)), 0, 255);
      uint8_t bc =
          std::clamp((int)((pixel & 0xFF) * (1.0f + threshold)), 0, 255);

      pixels[idx] = 0xFF000000 | (rc << 16) | (gc << 8) | bc;
    }
  }
}
