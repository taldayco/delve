#include "core/task_system.h"

void TaskSystem::init(int num_threads) {
  stop_ = false;
  for (int i = 0; i < num_threads; ++i)
    threads_.emplace_back([this] { worker_loop(); });
}

void TaskSystem::shutdown() {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto &t : threads_) {
    if (t.joinable()) t.join();
  }
  threads_.clear();
}

void TaskSystem::enqueue(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lk(mtx_);
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

bool TaskSystem::is_idle() const {
  std::lock_guard<std::mutex> lk(const_cast<std::mutex &>(mtx_));
  return queue_.empty() && active_count_.load() == 0;
}

void TaskSystem::worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lk(mtx_);
      cv_.wait(lk, [this] { return stop_.load() || !queue_.empty(); });
      if (stop_ && queue_.empty()) return;
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    active_count_.fetch_add(1);
    task();
    active_count_.fetch_sub(1);
    cv_.notify_all(); // wake is_idle() waiters if any
  }
}
