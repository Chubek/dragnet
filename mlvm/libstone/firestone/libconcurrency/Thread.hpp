#pragma once
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace firestone::concurrency {

// Joining scope ownership; join() propagates the worker's exception.
class Thread {
public:
    Thread() = default;
    explicit Thread(std::function<void()> task) : failure_(std::make_shared<std::exception_ptr>()) {
        if (!task) throw std::invalid_argument("empty thread task");
        worker_ = std::thread([task = std::move(task), failure = failure_] {
            try { task(); } catch (...) { *failure = std::current_exception(); }
        });
    }
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) noexcept = default;
    Thread& operator=(Thread&& other) noexcept {
        if (this != &other) { finish(); worker_ = std::move(other.worker_); failure_ = std::move(other.failure_); }
        return *this;
    }
    ~Thread() { finish(); }
    bool joinable() const noexcept { return worker_.joinable(); }
    std::thread::id id() const noexcept { return worker_.get_id(); }
    std::thread::native_handle_type native_handle() { return worker_.native_handle(); }
    void join() { worker_.join(); if (*failure_) std::rethrow_exception(*failure_); }
private:
    std::thread worker_;
    std::shared_ptr<std::exception_ptr> failure_;
    void finish() noexcept {
        if (!worker_.joinable()) return;
        try {
            // Destruction by the worker itself cannot join; the task owns its state.
            if (worker_.get_id() == std::this_thread::get_id()) worker_.detach(); else worker_.join();
        } catch (...) {}
    }
};
} // namespace firestone::concurrency
