#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace firestone::bignum {

// Signed magnitude, little-endian base-2^32 limbs. Zero has no limbs or sign.
class BigInt {
public:
    BigInt() = default;
    BigInt(const BigInt&) = default;
    BigInt& operator=(const BigInt&) = default;
    BigInt(BigInt&& other) noexcept : limbs_(std::move(other.limbs_)), negative_(std::exchange(other.negative_, false)) { other.limbs_.clear(); }
    BigInt& operator=(BigInt&& other) noexcept {
        if (this != &other) { limbs_ = std::move(other.limbs_); negative_ = std::exchange(other.negative_, false); other.limbs_.clear(); }
        return *this;
    }
    BigInt(std::int64_t value) : negative_(value < 0) {
        auto magnitude = value < 0 ? std::uint64_t(-(value + 1)) + 1 : std::uint64_t(value);
        if (magnitude) limbs_.push_back(static_cast<std::uint32_t>(magnitude));
        if (magnitude >> 32) limbs_.push_back(static_cast<std::uint32_t>(magnitude >> 32));
    }
    explicit BigInt(std::string_view text, unsigned base = 10) {
        if (base < 2 || base > 36) throw std::invalid_argument("integer base must be 2..36");
        bool negative = false;
        if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
            negative = text.front() == '-'; text.remove_prefix(1);
        }
        if (text.empty()) throw std::invalid_argument("empty integer");
        for (char c : text) {
            unsigned digit = c >= '0' && c <= '9' ? unsigned(c - '0') :
                c >= 'a' && c <= 'z' ? unsigned(c - 'a' + 10) :
                c >= 'A' && c <= 'Z' ? unsigned(c - 'A' + 10) : 36;
            if (digit >= base) throw std::invalid_argument("invalid integer digit");
            multiply_small(base); add_small(digit);
        }
        negative_ = negative && !is_zero();
    }
    bool is_zero() const noexcept { return limbs_.empty(); }
    bool negative() const noexcept { return negative_; }
    int sign() const noexcept { return is_zero() ? 0 : negative_ ? -1 : 1; }
    BigInt abs() const { BigInt result = *this; result.negative_ = false; return result; }
    std::size_t bit_length() const noexcept {
        if (is_zero()) return 0;
        std::size_t result = (limbs_.size() - 1) * 32;
        for (auto top = limbs_.back(); top; top >>= 1) ++result;
        return result;
    }
    bool test_bit(std::size_t bit) const noexcept {
        return bit / 32 < limbs_.size() && ((limbs_[bit / 32] >> (bit % 32)) & 1);
    }
    std::string to_string(unsigned base = 10) const {
        if (base < 2 || base > 36) throw std::invalid_argument("integer base must be 2..36");
        if (is_zero()) return "0";
        auto value = abs(); std::string result;
        while (!value.is_zero()) result += "0123456789abcdefghijklmnopqrstuvwxyz"[value.divide_small(base)];
        if (negative_) result += '-';
        std::reverse(result.begin(), result.end()); return result;
    }
    std::int64_t to_int64() const {
        if (limbs_.size() > 2) throw std::overflow_error("integer does not fit int64");
        std::uint64_t magnitude = is_zero() ? 0 : limbs_[0];
        if (limbs_.size() == 2) magnitude |= std::uint64_t(limbs_[1]) << 32;
        auto limit = std::uint64_t(std::numeric_limits<std::int64_t>::max());
        if (magnitude > limit + unsigned(negative_)) throw std::overflow_error("integer does not fit int64");
        if (negative_ && magnitude == limit + 1) return std::numeric_limits<std::int64_t>::min();
        return negative_ ? -static_cast<std::int64_t>(magnitude) : static_cast<std::int64_t>(magnitude);
    }
    BigInt operator-() const { auto result = *this; if (!is_zero()) result.negative_ = !negative_; return result; }
    friend int compare(const BigInt& a, const BigInt& b) noexcept {
        if (a.negative_ != b.negative_) return a.negative_ ? -1 : 1;
        auto order = compare_magnitude(a, b); return a.negative_ ? -order : order;
    }
    friend bool operator==(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) == 0; }
    friend bool operator!=(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) != 0; }
    friend bool operator<(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) < 0; }
    friend bool operator>(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) > 0; }
    friend bool operator<=(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) <= 0; }
    friend bool operator>=(const BigInt& a, const BigInt& b) noexcept { return compare(a, b) >= 0; }
    friend BigInt operator+(const BigInt& a, const BigInt& b) {
        if (a.negative_ == b.negative_) {
            BigInt result = a; std::uint64_t carry = 0;
            result.limbs_.resize(std::max(a.limbs_.size(), b.limbs_.size()), 0);
            for (std::size_t i = 0; i < result.limbs_.size(); ++i) {
                auto sum = std::uint64_t(result.limbs_[i]) + (i < b.limbs_.size() ? b.limbs_[i] : 0) + carry;
                result.limbs_[i] = static_cast<std::uint32_t>(sum); carry = sum >> 32;
            }
            if (carry) result.limbs_.push_back(static_cast<std::uint32_t>(carry));
            return result;
        }
        if (compare_magnitude(a, b) < 0) return b + a;
        BigInt result = a; std::uint64_t borrow = 0;
        for (std::size_t i = 0; i < result.limbs_.size(); ++i) {
            auto sub = (i < b.limbs_.size() ? std::uint64_t(b.limbs_[i]) : 0) + borrow;
            auto value = std::uint64_t(result.limbs_[i]);
            result.limbs_[i] = static_cast<std::uint32_t>(value - sub); borrow = value < sub;
        }
        result.normalize(); return result;
    }
    friend BigInt operator-(const BigInt& a, const BigInt& b) { return a + (-b); }
    friend BigInt operator*(const BigInt& a, const BigInt& b) {
        if (a.is_zero() || b.is_zero()) return {};
        BigInt result; result.limbs_.resize(a.limbs_.size() + b.limbs_.size());
        result.negative_ = a.negative_ != b.negative_;
        for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
            std::uint64_t carry = 0;
            for (std::size_t j = 0; j < b.limbs_.size(); ++j) {
                auto product = std::uint64_t(a.limbs_[i]) * b.limbs_[j] + result.limbs_[i + j] + carry;
                result.limbs_[i + j] = static_cast<std::uint32_t>(product); carry = product >> 32;
            }
            result.limbs_[i + b.limbs_.size()] = static_cast<std::uint32_t>(carry);
        }
        result.normalize(); return result;
    }
    // Quotient truncates toward zero; remainder has the dividend's sign.
    friend std::pair<BigInt, BigInt> divmod(const BigInt& a, const BigInt& b) {
        if (b.is_zero()) throw std::domain_error("integer division by zero");
        BigInt quotient, remainder; auto divisor = b.abs();
        quotient.limbs_.resize(a.limbs_.size());
        for (auto i = a.bit_length(); i > 0; --i) {
            remainder.multiply_small(2); remainder.add_small(a.test_bit(i - 1));
            if (compare_magnitude(remainder, divisor) >= 0) {
                remainder = remainder - divisor;
                quotient.limbs_[(i - 1) / 32] |= std::uint32_t(1) << ((i - 1) % 32);
            }
        }
        quotient.negative_ = a.negative_ != b.negative_; quotient.normalize();
        remainder.negative_ = a.negative_ && !remainder.is_zero();
        return {std::move(quotient), std::move(remainder)};
    }
    friend BigInt operator/(const BigInt& a, const BigInt& b) { return divmod(a, b).first; }
    friend BigInt operator%(const BigInt& a, const BigInt& b) { return divmod(a, b).second; }
    friend BigInt operator<<(const BigInt& a, std::size_t bits) {
        if (a.is_zero()) return a;
        if (bits > (std::size_t{1} << 26)) throw std::length_error("integer shift too large");
        BigInt result; auto words = bits / 32; auto shift = bits % 32;
        if (words >= result.limbs_.max_size() - a.limbs_.size()) throw std::length_error("integer shift too large");
        result.limbs_.resize(words + a.limbs_.size() + 1); result.negative_ = a.negative_;
        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
            auto value = (std::uint64_t(a.limbs_[i]) << shift) | carry;
            result.limbs_[words + i] = static_cast<std::uint32_t>(value); carry = value >> 32;
        }
        result.limbs_.back() = static_cast<std::uint32_t>(carry); result.normalize(); return result;
    }
    // Magnitude shift: negative values truncate toward zero, like division.
    friend BigInt operator>>(const BigInt& a, std::size_t bits) {
        auto words = bits / 32; auto shift = bits % 32;
        if (words >= a.limbs_.size()) return {};
        BigInt result; result.negative_ = a.negative_; result.limbs_.resize(a.limbs_.size() - words);
        for (std::size_t i = words; i < a.limbs_.size(); ++i) {
            std::uint64_t value = a.limbs_[i];
            if (shift && i + 1 < a.limbs_.size()) value |= std::uint64_t(a.limbs_[i + 1]) << 32;
            result.limbs_[i - words] = static_cast<std::uint32_t>(value >> shift);
        }
        result.normalize(); return result;
    }
    BigInt& operator+=(const BigInt& b) { return *this = *this + b; }
    BigInt& operator-=(const BigInt& b) { return *this = *this - b; }
    BigInt& operator*=(const BigInt& b) { return *this = *this * b; }
    BigInt& operator/=(const BigInt& b) { return *this = *this / b; }
    BigInt& operator%=(const BigInt& b) { return *this = *this % b; }
    BigInt& operator<<=(std::size_t bits) { return *this = *this << bits; }
    BigInt& operator>>=(std::size_t bits) { return *this = *this >> bits; }
    friend std::ostream& operator<<(std::ostream& out, const BigInt& value) { return out << value.to_string(); }
private:
    std::vector<std::uint32_t> limbs_;
    bool negative_ = false;
    void normalize() { while (!limbs_.empty() && !limbs_.back()) limbs_.pop_back(); if (is_zero()) negative_ = false; }
    static int compare_magnitude(const BigInt& a, const BigInt& b) noexcept {
        if (a.limbs_.size() != b.limbs_.size()) return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
        for (auto i = a.limbs_.size(); i > 0; --i)
            if (a.limbs_[i - 1] != b.limbs_[i - 1]) return a.limbs_[i - 1] < b.limbs_[i - 1] ? -1 : 1;
        return 0;
    }
    void multiply_small(std::uint32_t value) {
        std::uint64_t carry = 0;
        for (auto& limb : limbs_) { auto product = std::uint64_t(limb) * value + carry; limb = static_cast<std::uint32_t>(product); carry = product >> 32; }
        if (carry) limbs_.push_back(static_cast<std::uint32_t>(carry));
        normalize();
    }
    void add_small(std::uint32_t value) {
        std::uint64_t carry = value;
        for (auto& limb : limbs_) { auto sum = std::uint64_t(limb) + carry; limb = static_cast<std::uint32_t>(sum); carry = sum >> 32; if (!carry) break; }
        if (carry) limbs_.push_back(static_cast<std::uint32_t>(carry));
    }
    std::uint32_t divide_small(std::uint32_t value) {
        if (!value) throw std::invalid_argument("division by zero");
        std::uint64_t remainder = 0;
        for (auto i = limbs_.size(); i > 0; --i) {
            auto dividend = (remainder << 32) | limbs_[i - 1];
            limbs_[i - 1] = static_cast<std::uint32_t>(dividend / value); remainder = dividend % value;
        }
        normalize(); return static_cast<std::uint32_t>(remainder);
    }
};

inline BigInt pow(BigInt base, std::size_t exponent) {
    BigInt result(1);
    while (exponent) { if (exponent & 1) result *= base; exponent >>= 1; if (exponent) base *= base; }
    return result;
}
inline BigInt gcd(BigInt a, BigInt b) {
    a = a.abs(); b = b.abs();
    while (!b.is_zero()) { auto remainder = a % b; a = std::move(b); b = std::move(remainder); }
    return a;
}

} // namespace firestone::bignum
