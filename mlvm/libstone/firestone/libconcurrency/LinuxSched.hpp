#pragma once
#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif

namespace firestone::concurrency {
class LinuxSched {
public:
    static constexpr bool available() noexcept {
#if defined(__linux__)
        return true;
#else
        return false;
#endif
    }
    static std::vector<unsigned> affinity(int pid = 0) {
#if defined(__linux__)
        cpu_set_t mask; CPU_ZERO(&mask);
        if (sched_getaffinity(pid, sizeof(mask), &mask) != 0) throw std::system_error(errno, std::generic_category(), "sched_getaffinity");
        std::vector<unsigned> result;
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) if (CPU_ISSET(cpu, &mask)) result.push_back(cpu);
        return result;
#else
        (void)pid; throw std::runtime_error("Linux scheduling unavailable");
#endif
    }
    static void set_affinity(const std::vector<unsigned>& cpus, int pid = 0) {
#if defined(__linux__)
        if (cpus.empty()) throw std::invalid_argument("empty CPU affinity");
        cpu_set_t mask; CPU_ZERO(&mask);
        for (auto cpu : cpus) { if (cpu >= CPU_SETSIZE) throw std::out_of_range("CPU affinity index"); CPU_SET(cpu, &mask); }
        if (sched_setaffinity(pid, sizeof(mask), &mask) != 0) throw std::system_error(errno, std::generic_category(), "sched_setaffinity");
#else
        (void)cpus; (void)pid; throw std::runtime_error("Linux scheduling unavailable");
#endif
    }
    static void yield() {
#if defined(__linux__)
        if (sched_yield() != 0) throw std::system_error(errno, std::generic_category(), "sched_yield");
#else
        throw std::runtime_error("Linux scheduling unavailable");
#endif
    }
};
} // namespace firestone::concurrency
