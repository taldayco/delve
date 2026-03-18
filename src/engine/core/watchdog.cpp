#include "core/watchdog.h"
#include <SDL3/SDL_log.h>
#include <thread>
#include <chrono>
#include <cstdlib>

#ifdef __linux__
#include <fstream>
#include <string>
#include <unistd.h>
#endif

std::atomic<bool> g_emergency_shutdown{false};

static std::atomic<bool> s_watchdog_running{false};
static std::thread       s_watchdog_thread;

static size_t get_rss_bytes() {
#ifdef __linux__
  std::ifstream status("/proc/self/status");
  if (!status.is_open()) return 0;

  std::string line;
  while (std::getline(status, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      size_t kb = 0;
      const char *p = line.c_str() + 6;
      while (*p == ' ' || *p == '\t') ++p;
      while (*p >= '0' && *p <= '9') {
        kb = kb * 10 + (*p - '0');
        ++p;
      }
      return kb * 1024;
    }
  }
#endif
  return 0;
}

static size_t get_total_ram() {
#ifdef __linux__
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page_size > 0)
    return (size_t)pages * (size_t)page_size;
#endif
  return 0;
}

void start_watchdog(size_t rss_limit_bytes) {
  if (s_watchdog_running.exchange(true))
    return;

  if (rss_limit_bytes == 0) {
    size_t total = get_total_ram();
    if (total > 0)
      rss_limit_bytes = (size_t)(total * 0.75);
    else
      rss_limit_bytes = (size_t)2 * 1024 * 1024 * 1024;
  }

  size_t limit = rss_limit_bytes;
  SDL_Log("Watchdog: starting (RSS limit = %zu MB)", limit / (1024 * 1024));

  s_watchdog_thread = std::thread([limit]() {
    while (s_watchdog_running.load(std::memory_order_relaxed)) {
      size_t rss = get_rss_bytes();
      if (rss > 0 && rss > limit) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "WATCHDOG: RSS %zu MB exceeds limit %zu MB — emergency shutdown!",
                        rss / (1024 * 1024), limit / (1024 * 1024));
        g_emergency_shutdown.store(true, std::memory_order_release);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::quick_exit(1);
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  });
}

void stop_watchdog() {
  if (!s_watchdog_running.exchange(false))
    return;
  if (s_watchdog_thread.joinable())
    s_watchdog_thread.join();
  SDL_Log("Watchdog: stopped");
}
