#pragma once
#include "Thread.hpp"
#include <string>
#include <system_error>
#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace firestone::concurrency {

class PosixThread : public Thread {
public:
    PosixThread() = default;
    explicit PosixThread(std::function<void()> task) : Thread(check(std::move(task))) {}
    static constexpr bool available() noexcept {
#if defined(__unix__) || defined(__APPLE__)
        return true;
#else
        return false;
#endif
    }
    void set_name(const std::string& name) {
#if defined(__linux__)
        if (!joinable()) throw std::logic_error("thread is not joinable");
        if (name.size() > 15 || name.find('\0') != std::string::npos) throw std::invalid_argument("invalid POSIX thread name");
        auto error = pthread_setname_np(native_handle(), name.c_str());
        if (error) throw std::system_error(error, std::generic_category(), "pthread_setname_np");
#else
        (void)name; throw std::runtime_error("setting another thread's name is unavailable");
#endif
    }
private:
    static std::function<void()> check(std::function<void()> task) {
        if (!available()) throw std::runtime_error("POSIX threads unavailable");
        return task;
    }
};
} // namespace firestone::concurrency
