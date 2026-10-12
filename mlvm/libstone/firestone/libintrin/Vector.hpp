#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace firestone::intrin {

template<std::size_t N> class Mask {
public:
    bool& operator[](std::size_t lane) { return lanes_.at(lane); }
    bool operator[](std::size_t lane) const { return lanes_.at(lane); }
    bool all() const noexcept { for (bool lane : lanes_) if (!lane) return false; return true; }
    bool any() const noexcept { for (bool lane : lanes_) if (lane) return true; return false; }
    std::size_t count() const noexcept { std::size_t n = 0; for (bool lane : lanes_) n += lane; return n; }
    friend Mask operator!(const Mask& a) { Mask result; for (std::size_t i = 0; i < N; ++i) result[i] = !a[i]; return result; }
    friend Mask operator&(const Mask& a, const Mask& b) { Mask result; for (std::size_t i = 0; i < N; ++i) result[i] = a[i] && b[i]; return result; }
    friend Mask operator|(const Mask& a, const Mask& b) { Mask result; for (std::size_t i = 0; i < N; ++i) result[i] = a[i] || b[i]; return result; }
private:
    std::array<bool, N> lanes_{};
};

namespace detail {
template<class T> T from_unsigned(std::make_unsigned_t<T> value) noexcept {
    if constexpr (std::is_unsigned<T>::value) return value;
    else {
        using U = std::make_unsigned_t<T>;
        constexpr auto maximum = static_cast<U>(std::numeric_limits<T>::max());
        return value <= maximum ? static_cast<T>(value) :
            static_cast<T>(std::numeric_limits<T>::min() + static_cast<T>(value - maximum - U(1)));
    }
}
template<class T, class F> T arithmetic(T a, T b, F operation) {
    if constexpr (std::is_floating_point<T>::value) return operation(a, b);
    else {
        using U = std::make_unsigned_t<T>;
        using Wide = std::conditional_t<(sizeof(U) < sizeof(unsigned)), unsigned, U>;
        return from_unsigned<T>(static_cast<U>(operation(static_cast<Wide>(static_cast<U>(a)), static_cast<Wide>(static_cast<U>(b)))));
    }
}
} // namespace detail

// Portable lane semantics. Integer +,-,* wrap modulo 2^width, including signed lanes.
template<class T, std::size_t N>
class Vector {
    static_assert(N > 0 && std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, "invalid vector lane type/count");
public:
    using value_type = T;
    static constexpr std::size_t size = N;
    Vector() = default;
    explicit Vector(T value) { lanes_.fill(value); }
    Vector(std::initializer_list<T> values) {
        if (values.size() != N) throw std::invalid_argument("vector lane count");
        std::size_t i = 0; for (T value : values) lanes_[i++] = value;
    }
    T& operator[](std::size_t lane) { return lanes_.at(lane); }
    T operator[](std::size_t lane) const { return lanes_.at(lane); }
    static Vector load(const T* source, std::size_t count = N) {
        if (!source || count < N) throw std::invalid_argument("short vector load");
        Vector result; for (std::size_t i = 0; i < N; ++i) result[i] = source[i]; return result;
    }
    void store(T* destination, std::size_t count = N) const {
        if (!destination || count < N) throw std::invalid_argument("short vector store");
        for (std::size_t i = 0; i < N; ++i) destination[i] = lanes_[i];
    }
    friend Vector operator+(const Vector& a, const Vector& b) {
        Vector result; for (std::size_t i = 0; i < N; ++i) result[i] = detail::arithmetic(a[i], b[i], [](auto x, auto y) { return x + y; }); return result;
    }
    friend Vector operator-(const Vector& a, const Vector& b) {
        Vector result; for (std::size_t i = 0; i < N; ++i) result[i] = detail::arithmetic(a[i], b[i], [](auto x, auto y) { return x - y; }); return result;
    }
    friend Vector operator*(const Vector& a, const Vector& b) {
        Vector result; for (std::size_t i = 0; i < N; ++i) result[i] = detail::arithmetic(a[i], b[i], [](auto x, auto y) { return x * y; }); return result;
    }
    friend Vector operator/(const Vector& a, const Vector& b) {
        Vector result;
        for (std::size_t i = 0; i < N; ++i) {
            if constexpr (std::is_integral<T>::value) {
                if (!b[i]) throw std::domain_error("integer vector division by zero");
                if constexpr (std::is_signed<T>::value)
                    if (a[i] == std::numeric_limits<T>::min() && b[i] == T(-1)) throw std::overflow_error("integer vector division overflow");
            }
            result[i] = a[i] / b[i];
        }
        return result;
    }
    friend Mask<N> operator==(const Vector& a, const Vector& b) { Mask<N> result; for (std::size_t i = 0; i < N; ++i) result[i] = a[i] == b[i]; return result; }
    friend Mask<N> operator<(const Vector& a, const Vector& b) { Mask<N> result; for (std::size_t i = 0; i < N; ++i) result[i] = a[i] < b[i]; return result; }
    friend Mask<N> operator!=(const Vector& a, const Vector& b) { return !(a == b); }
    friend Mask<N> operator>(const Vector& a, const Vector& b) { return b < a; }
    friend Mask<N> operator<=(const Vector& a, const Vector& b) { return (a < b) | (a == b); }
    friend Mask<N> operator>=(const Vector& a, const Vector& b) { return b <= a; }
    T reduce_add() const {
        T result{}; for (T lane : lanes_) result = detail::arithmetic(result, lane, [](auto a, auto b) { return a + b; }); return result;
    }
    Vector shuffle(const std::array<std::size_t, N>& indices) const {
        Vector result; for (std::size_t i = 0; i < N; ++i) result[i] = lanes_.at(indices[i]); return result;
    }
private:
    std::array<T, N> lanes_{};
};

template<class T, std::size_t N>
Vector<T, N> select(const Mask<N>& mask, const Vector<T, N>& yes, const Vector<T, N>& no) {
    Vector<T, N> result; for (std::size_t i = 0; i < N; ++i) result[i] = mask[i] ? yes[i] : no[i]; return result;
}
template<class T, std::size_t N>
Vector<T, N> fma(const Vector<T, N>& a, const Vector<T, N>& b, const Vector<T, N>& c) {
    static_assert(std::is_floating_point<T>::value, "fma requires floating lanes");
    Vector<T, N> result; for (std::size_t i = 0; i < N; ++i) result[i] = std::fma(a[i], b[i], c[i]); return result;
}

enum class InstructionSet { Scalar, SSE4, AVX2, AVX512, NEON };
inline bool supported(InstructionSet isa) noexcept {
    if (isa == InstructionSet::Scalar) return true;
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    switch (isa) {
    case InstructionSet::SSE4: return __builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("sse4.2");
    case InstructionSet::AVX2: return __builtin_cpu_supports("avx2");
    case InstructionSet::AVX512: return __builtin_cpu_supports("avx512f");
    default: break;
    }
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    if (isa == InstructionSet::NEON) return true;
#endif
    return false;
}
template<std::size_t Bits, InstructionSet ISA>
struct VectorProfile {
    static constexpr std::size_t bits = Bits;
    static constexpr InstructionSet instruction_set = ISA;
    template<class T> using Pack = Vector<T, Bits / (sizeof(T) * 8)>;
    static bool available() noexcept { return supported(ISA); }
};

} // namespace firestone::intrin
