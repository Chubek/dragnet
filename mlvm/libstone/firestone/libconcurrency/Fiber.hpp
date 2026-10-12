#pragma once
#include <exception>
#include <functional>
#include <stdexcept>
#include <utility>

namespace firestone::concurrency {
enum class FiberState { Ready, Running, Suspended, Complete, Cancelled, Failed };
enum class FiberStep { Yield, Complete };

// Stackless cooperative fiber: captured state persists between resume calls.
class Fiber {
public:
    explicit Fiber(std::function<FiberStep()> step) : step_(std::move(step)) {
        if (!step_) throw std::invalid_argument("empty fiber step");
    }
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;
    FiberState state() const noexcept { return state_; }
    bool done() const noexcept { return state_ == FiberState::Complete || state_ == FiberState::Cancelled || state_ == FiberState::Failed; }
    void cancel() {
        if (state_ == FiberState::Running) throw std::logic_error("cannot cancel a running fiber");
        if (!done()) state_ = FiberState::Cancelled;
    }
    FiberState resume() {
        if (state_ == FiberState::Running) throw std::logic_error("recursive fiber resume");
        if (state_ == FiberState::Failed) std::rethrow_exception(failure_);
        if (done()) return state_;
        state_ = FiberState::Running;
        try { state_ = step_() == FiberStep::Complete ? FiberState::Complete : FiberState::Suspended; }
        catch (...) { failure_ = std::current_exception(); state_ = FiberState::Failed; throw; }
        return state_;
    }
private:
    std::function<FiberStep()> step_;
    FiberState state_ = FiberState::Ready;
    std::exception_ptr failure_;
};
} // namespace firestone::concurrency
