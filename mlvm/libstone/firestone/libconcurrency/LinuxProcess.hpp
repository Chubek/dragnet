#pragma once
#include <cerrno>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace firestone::concurrency {
struct ProcessStatus {
    int exit_code = -1;
    int signal = 0;
    bool success() const noexcept { return exit_code == 0 && signal == 0; }
};

// Owns and reaps one child. Destruction waits; no shell interpretation.
class LinuxProcess {
public:
    LinuxProcess() = default;
    static constexpr bool available() noexcept {
#if defined(__linux__)
        return true;
#else
        return false;
#endif
    }
    static LinuxProcess spawn(const std::vector<std::string>& arguments) {
        if (arguments.empty() || arguments.front().empty()) throw std::invalid_argument("missing executable");
        for (const auto& argument : arguments)
            if (argument.find('\0') != std::string::npos) throw std::invalid_argument("NUL in process argument");
#if defined(__linux__)
        std::vector<char*> argv;
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr); pid_t pid;
        auto error = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
        if (error) throw std::system_error(error, std::generic_category(), "posix_spawnp");
        LinuxProcess result; result.pid_ = pid; return result;
#else
        throw std::runtime_error("Linux processes unavailable");
#endif
    }
    LinuxProcess(const LinuxProcess&) = delete;
    LinuxProcess& operator=(const LinuxProcess&) = delete;
    LinuxProcess(LinuxProcess&& other) noexcept : pid_(std::exchange(other.pid_, -1)), status_(std::move(other.status_)) { other.status_.reset(); }
    LinuxProcess& operator=(LinuxProcess&& other) noexcept {
        if (this != &other) { reap(); pid_ = std::exchange(other.pid_, -1); status_ = std::move(other.status_); other.status_.reset(); }
        return *this;
    }
    ~LinuxProcess() { reap(); }
    int pid() const noexcept { return pid_; }
    ProcessStatus wait() { auto result = collect(false); return *result; }
    std::optional<ProcessStatus> poll() { return collect(true); }
private:
    int pid_ = -1;
    std::optional<ProcessStatus> status_;
    std::optional<ProcessStatus> collect(bool nonblocking) {
        if (pid_ < 0 && !status_) throw std::logic_error("no child process");
        if (status_) return status_;
#if defined(__linux__)
        int raw = 0; pid_t result;
        do { result = waitpid(pid_, &raw, nonblocking ? WNOHANG : 0); } while (result < 0 && errno == EINTR);
        if (result < 0) throw std::system_error(errno, std::generic_category(), "waitpid");
        if (!result) return std::nullopt;
        status_ = ProcessStatus{WIFEXITED(raw) ? WEXITSTATUS(raw) : -1, WIFSIGNALED(raw) ? WTERMSIG(raw) : 0};
        pid_ = -1; return status_;
#else
        (void)nonblocking; throw std::runtime_error("Linux processes unavailable");
#endif
    }
    void reap() noexcept { if (pid_ >= 0) { try { (void)wait(); } catch (...) {} } }
};
} // namespace firestone::concurrency
