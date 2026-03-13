#pragma once
#include <functional>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <thread>

class TaskSystem {
public:
  void init(int num_threads);
  void shutdown();
  void enqueue(std::function<void()> task);
  bool is_idle() const;

private:
  void worker_loop();

  std::deque<std::function<void()>> queue_;
  std::mutex                        mtx_;
  std::condition_variable           cv_;
  std::atomic<int>                  active_count_{0};
  std::atomic<bool>                 stop_{false};
  std::vector<std::thread>          threads_;
};
