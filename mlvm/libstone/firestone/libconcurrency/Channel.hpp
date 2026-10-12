#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace firestone::concurrency {

// MPMC FIFO; capacity zero is unbounded. Close wakes waiters; queued values drain.
template<class T>
class Channel {
    static_assert(std::is_nothrow_move_constructible<T>::value, "channel elements require noexcept move construction");
public:
    explicit Channel(std::size_t capacity = 0) : capacity_(capacity) {}
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    bool send(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        writable_.wait(lock, [&] { return closed_ || !capacity_ || queue_.size() < capacity_; });
        if (closed_) return false;
        queue_.push_back(std::move(value)); lock.unlock(); readable_.notify_one(); return true;
    }
    bool try_send(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_ || (capacity_ && queue_.size() == capacity_)) return false;
        queue_.push_back(std::move(value)); lock.unlock(); readable_.notify_one(); return true;
    }
    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mutex_);
        readable_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        return pop(lock);
    }
    std::optional<T> try_receive() { std::unique_lock<std::mutex> lock(mutex_); return pop(lock); }
    void close() {
        { std::lock_guard<std::mutex> lock(mutex_); closed_ = true; }
        readable_.notify_all(); writable_.notify_all();
    }
    bool closed() const { std::lock_guard<std::mutex> lock(mutex_); return closed_; }
    std::size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
private:
    mutable std::mutex mutex_;
    std::condition_variable readable_, writable_;
    std::deque<T> queue_;
    std::size_t capacity_;
    bool closed_ = false;
    std::optional<T> pop(std::unique_lock<std::mutex>& lock) {
        if (queue_.empty()) return std::nullopt;
        std::optional<T> result(std::move(queue_.front())); queue_.pop_front();
        lock.unlock(); writable_.notify_one(); return result;
    }
};
} // namespace firestone::concurrency
