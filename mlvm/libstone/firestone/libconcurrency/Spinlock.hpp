#pragma once
#include <atomic>
#include <thread>

namespace firestone::concurrency {
class Spinlock {
public:
    Spinlock() noexcept = default;
    Spinlock(const Spinlock&) = delete;
    Spinlock& operator=(const Spinlock&) = delete;
    void lock() noexcept {
        unsigned attempts = 0;
        while (!try_lock()) if (++attempts >= 64) { std::this_thread::yield(); attempts = 0; }
    }
    bool try_lock() noexcept { return !flag_.test_and_set(std::memory_order_acquire); }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};
} // namespace firestone::concurrency
