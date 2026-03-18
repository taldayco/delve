#pragma once
#include <atomic>
#include <cstddef>

extern std::atomic<bool> g_emergency_shutdown;

void start_watchdog(size_t rss_limit_bytes = 0);

void stop_watchdog();
