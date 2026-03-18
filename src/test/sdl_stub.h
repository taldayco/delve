#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdint>

#ifndef SDL_STUB_DEFINED
#define SDL_STUB_DEFINED

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

#define SDL_LOG_CATEGORY_APPLICATION 0

#endif
