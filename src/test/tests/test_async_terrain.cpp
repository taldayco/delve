#include "test_harness.h"
#include "core/task_system.h"
#include <atomic>
#include <thread>
#include <chrono>

DELVE_TEST(task_system_executes_all_enqueued) {
    TaskSystem ts;
    ts.init(4);
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i) {
        ts.enqueue([&counter] { counter.fetch_add(1); });
    }
    for (int i = 0; i < 2000 && !ts.is_idle(); ++i)
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    ts.shutdown();
    EXPECT_EQ(counter.load(), 100);
    return true;
}

DELVE_TEST(task_system_shutdown_drains_queue) {
    TaskSystem ts;
    ts.init(2);
    std::atomic<bool> gate{false};
    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i) {
        ts.enqueue([&gate, &counter] {
            while (!gate.load())
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            counter.fetch_add(1);
        });
    }
    gate.store(true);
    ts.shutdown();
    EXPECT_EQ(counter.load(), 20);
    return true;
}

DELVE_TEST(async_cancel_pattern_no_crash) {
    std::atomic<bool> cancel_requested{false};
    std::atomic<int> completed{0};
    TaskSystem ts;
    ts.init(4);
    for (int i = 0; i < 50; ++i) {
        ts.enqueue([&cancel_requested, &completed] {
            for (int j = 0; j < 10; ++j) {
                if (cancel_requested.load()) return;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            completed.fetch_add(1);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    cancel_requested.store(true);
    for (int i = 0; i < 2000 && !ts.is_idle(); ++i)
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    ts.shutdown();
    EXPECT_LT(completed.load(), 50);
    return true;
}
