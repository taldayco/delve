#pragma once
#include <stdint.h>

inline uint32_t hash2d(int x, int y) {
  uint32_t h = (x * 374761393) ^ (y * 668265263);
  return (h ^ (h >> 13)) * 1274126177;
}

inline uint32_t hash1d(int idx) {
  return (idx * 374761393u) ^ 668265263u;
}
