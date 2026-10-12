#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace firestone::concurrency {
class Semaphore {
public:
    explicit Semaphore(std::size_t initial = 0, std::size_t maximum = std::numeric_limits<std::size_t>::max())
        : count_(initial), maximum_(maximum) {
        if (!maximum || initial > maximum) throw std::invalid_argument("invalid semaphore count");
    }
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    void release(std::size_t count = 1) {
        { std::lock_guard<std::mutex> lock(mutex_);
          if (count > maximum_ - count_) throw std::overflow_error("semaphore count overflow");
          count_ += count; }
        if (count) ready_.notify_all();
    }
    void acquire() { std::unique_lock<std::mutex> lock(mutex_); ready_.wait(lock, [&] { return count_ != 0; }); --count_; }
    bool try_acquire() { std::lock_guard<std::mutex> lock(mutex_); if (!count_) return false; --count_; return true; }
    template<class Rep, class Period>
    bool try_acquire_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, timeout, [&] { return count_ != 0; })) return false;
        --count_; return true;
    }
private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::size_t count_, maximum_;
};
} // namespace firestone::concurrency
