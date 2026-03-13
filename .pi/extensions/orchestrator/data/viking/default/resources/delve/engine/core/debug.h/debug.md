#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <mutex>

#define TOPO_LOG(fmt, ...) SDL_Log("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

struct Breadcrumb {
    const char* file;
    int line;
    const char* func;
    const char* message;
};

class DebugTracker {
public:
    static DebugTracker& get() {
        static DebugTracker instance;
        return instance;
    }

    void push(const char* file, int line, const char* func, const char* message = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        breadcrumbs.push_back({file, line, func, message});
        if (breadcrumbs.size() > 100) {
            breadcrumbs.erase(breadcrumbs.begin());
        }
    }

    void print_last() {
        std::lock_guard<std::mutex> lock(mutex);
        SDL_Log("--- Last Breadcrumbs ---");
        for (const auto& b : breadcrumbs) {
            SDL_Log("  %s:%d (%s) %s", b.file, b.line, b.func, b.message ? b.message : "");
        }
    }

private:
    DebugTracker() = default;
    std::vector<Breadcrumb> breadcrumbs;
    std::mutex mutex;
};

#define TOPO_BREADCRUMB(msg) DebugTracker::get().push(__FILE__, __LINE__, __FUNCTION__, msg)

struct ScopedTimer {
    const char* name;
    uint64_t start;
    ScopedTimer(const char* name) : name(name), start(SDL_GetTicks()) {
        TOPO_BREADCRUMB(name);
        SDL_Log("START: %s", name);
    }
    ~ScopedTimer() {
        SDL_Log("END: %s (%llu ms)", name, (unsigned long long)(SDL_GetTicks() - start));
    }
};

#define TOPO_SCOPE_TIMER(name) ScopedTimer timer_##__LINE__(name)
