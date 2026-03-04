#pragma once
// Stub for SDL functions used by terrain code in headless builds.
// Only SDL_Log is needed — terrain code doesn't use GPU/window functions.

#include <cstdio>
#include <cstdarg>
#include <cstdint>

#ifndef SDL_STUB_DEFINED
#define SDL_STUB_DEFINED

// Minimal type stubs
using Sint64 = int64_t;
using Uint64 = uint64_t;
using Uint32 = uint32_t;

inline void SDL_Log(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

inline void SDL_LogError(int, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

inline Uint64 SDL_GetTicks() { return 0; }

// Category constants
#define SDL_LOG_CATEGORY_APPLICATION 0

// GPU stubs for skeleton_mesh tests (headless builds don't use GPU).
struct SDL_GPUBuffer {};
struct SDL_GPUDevice {};
inline void SDL_ReleaseGPUBuffer(SDL_GPUDevice *, SDL_GPUBuffer *) {}

#endif // SDL_STUB_DEFINED
