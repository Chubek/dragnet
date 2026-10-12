#pragma once
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace firestone::compute {
class OpenMP {
public:
    static constexpr bool available() noexcept {
#ifdef _OPENMP
        return true;
#else
        return false;
#endif
    }
    static int max_threads() noexcept {
#ifdef _OPENMP
        return omp_get_max_threads();
#else
        return 1;
#endif
    }
    // Always executable: serial fallback when compiled without OpenMP.
    template<class Function> static void parallel_for(std::size_t count, Function function) {
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) throw std::length_error("OpenMP iteration range");
#ifdef _OPENMP
        std::exception_ptr failure; std::mutex mutex;
#pragma omp parallel for schedule(static)
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(count); ++i) {
            try { function(static_cast<std::size_t>(i)); }
            catch (...) { std::lock_guard<std::mutex> lock(mutex); if (!failure) failure = std::current_exception(); }
        }
        if (failure) std::rethrow_exception(failure);
#else
        for (std::size_t i = 0; i < count; ++i) function(i);
#endif
    }
    // Parallel map, ordered fold: floating-point reduction order is deterministic.
    template<class T, class Map, class Reduce>
    static T parallel_reduce(std::size_t count, T initial, Map map, Reduce reduce) {
        // optional<T> avoids default-construction requirements and vector<bool> bit sharing.
        std::vector<std::optional<T>> values(count);
        parallel_for(count, [&](std::size_t i) { values[i].emplace(map(i)); });
        for (auto& value : values) initial = reduce(std::move(initial), std::move(*value));
        return initial;
    }
};
} // namespace firestone::compute
