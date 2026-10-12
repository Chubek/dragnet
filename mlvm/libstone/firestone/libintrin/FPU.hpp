#pragma once
#include <cfenv>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace firestone::intrin {
enum class RoundingMode { Nearest = FE_TONEAREST, Downward = FE_DOWNWARD, Upward = FE_UPWARD, TowardZero = FE_TOWARDZERO };

// Controls this thread's FP environment. Compiler FP optimizations must honor fenv.
class ScopedRounding {
public:
    explicit ScopedRounding(RoundingMode mode) : previous_(std::fegetround()) {
        if (previous_ < 0 || std::fesetround(static_cast<int>(mode))) throw std::runtime_error("rounding mode unavailable");
    }
    ScopedRounding(const ScopedRounding&) = delete;
    ScopedRounding& operator=(const ScopedRounding&) = delete;
    ~ScopedRounding() { std::fesetround(previous_); }
private:
    int previous_;
};
struct FPU {
    template<class T> static T fused_multiply_add(T a, T b, T c) {
        static_assert(std::is_floating_point<T>::value, "FPU requires floating point"); return std::fma(a, b, c);
    }
    template<class T> static T square_root(T value) {
        static_assert(std::is_floating_point<T>::value, "FPU requires floating point"); return std::sqrt(value);
    }
    static int exceptions() noexcept { return std::fetestexcept(FE_ALL_EXCEPT); }
    static void clear_exceptions() noexcept { std::feclearexcept(FE_ALL_EXCEPT); }
};
} // namespace firestone::intrin
