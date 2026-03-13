#pragma once
#include <atomic>
#include <cstddef>

// Global emergency shutdown flag. Checked by main loop, worker threads,
// and pipeline stages to bail out before the system OOMs.
extern std::atomic<bool> g_emergency_shutdown;

// Start a background watchdog thread that monitors resident set size.
// If RSS exceeds rss_limit_bytes (0 = auto: 75% of system RAM), the
// watchdog sets g_emergency_shutdown and calls std::quick_exit(1).
void start_watchdog(size_t rss_limit_bytes = 0);

// Stop the watchdog thread (call during normal cleanup).
void stop_watchdog();
