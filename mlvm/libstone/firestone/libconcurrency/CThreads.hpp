#pragma once
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#if __has_include(<threads.h>) && !defined(__STDC_NO_THREADS__)
#include <threads.h>
#define FIRESTONE_HAS_C_THREADS 1
#else
#define FIRESTONE_HAS_C_THREADS 0
#endif

namespace firestone::concurrency {

class CThreads {
public:
    static constexpr bool available() noexcept { return FIRESTONE_HAS_C_THREADS != 0; }
    CThreads() = default;
    explicit CThreads(std::function<int()> task) {
        if (!task) throw std::invalid_argument("empty C thread task");
#if FIRESTONE_HAS_C_THREADS
        state_ = std::make_unique<State>(); state_->task = std::move(task);
        if (thrd_create(&thread_, &invoke, state_.get()) != thrd_success) throw std::runtime_error("thrd_create failed");
        joinable_ = true;
#else
        throw std::runtime_error("C11 threads unavailable");
#endif
    }
    CThreads(const CThreads&) = delete;
    CThreads& operator=(const CThreads&) = delete;
    CThreads(CThreads&& other) noexcept { swap(other); }
    CThreads& operator=(CThreads&& other) noexcept { if (this != &other) { finish(); swap(other); } return *this; }
    ~CThreads() { finish(); }
    bool joinable() const noexcept { return joinable_; }
    int join() {
        if (!joinable_) throw std::logic_error("C thread is not joinable");
#if FIRESTONE_HAS_C_THREADS
        int result = 0;
        if (thrd_join(thread_, &result) != thrd_success) throw std::runtime_error("thrd_join failed");
        joinable_ = false;
        if (state_->failure) std::rethrow_exception(state_->failure);
        return result;
#else
        throw std::runtime_error("C11 threads unavailable");
#endif
    }
private:
    bool joinable_ = false;
#if FIRESTONE_HAS_C_THREADS
    struct State { std::function<int()> task; std::exception_ptr failure; };
    std::unique_ptr<State> state_;
    thrd_t thread_{};
    static int invoke(void* data) noexcept {
        auto& state = *static_cast<State*>(data);
        try { return state.task(); } catch (...) { state.failure = std::current_exception(); return -1; }
    }
#endif
    void swap(CThreads& other) noexcept {
        std::swap(joinable_, other.joinable_);
#if FIRESTONE_HAS_C_THREADS
        state_.swap(other.state_); std::swap(thread_, other.thread_);
#endif
    }
    void finish() noexcept {
#if FIRESTONE_HAS_C_THREADS
        if (joinable_) {
            if (thrd_equal(thrd_current(), thread_)) std::terminate();
            if (thrd_join(thread_, nullptr) != thrd_success) std::terminate();
            joinable_ = false;
        }
#endif
    }
};
} // namespace firestone::concurrency
