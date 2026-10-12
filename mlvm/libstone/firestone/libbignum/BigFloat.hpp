#pragma once
#include "BigInt.hpp"

namespace firestone::bignum {

// Finite decimal floating point, rounded to significant digits, ties to even.
class BigFloat {
public:
    static constexpr std::size_t default_precision = 50;
    static constexpr int exponent_limit = 100000;
    BigFloat() = default;
    BigFloat(std::int64_t value, std::size_t precision = default_precision)
        : coefficient_(value), precision_(check_precision(precision)) { normalize(); }
    explicit BigFloat(std::string_view text, std::size_t precision = default_precision)
        : precision_(check_precision(precision)) {
        if (text.empty()) throw std::invalid_argument("empty decimal");
        std::string digits; bool dot = false; bool seen = false; std::int64_t fractional = 0;
        std::size_t i = 0;
        if (text[i] == '+' || text[i] == '-') digits += text[i++];
        for (; i < text.size() && text[i] != 'e' && text[i] != 'E'; ++i) {
            char c = text[i];
            if (c == '.' && !dot) { dot = true; continue; }
            if (c < '0' || c > '9') throw std::invalid_argument("invalid decimal digit");
            seen = true; digits += c; if (dot) ++fractional;
            if (digits.size() > std::size_t(exponent_limit)) throw std::length_error("decimal literal too long");
        }
        if (!seen) throw std::invalid_argument("missing decimal digits");
        std::int64_t exponent = 0;
        if (i < text.size()) {
            ++i; bool negative = false;
            if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
            if (i == text.size()) throw std::invalid_argument("missing decimal exponent");
            for (; i < text.size(); ++i) {
                if (text[i] < '0' || text[i] > '9') throw std::invalid_argument("invalid decimal exponent");
                exponent = exponent * 10 + text[i] - '0';
                if (exponent > exponent_limit) throw std::out_of_range("decimal exponent");
            }
            if (negative) exponent = -exponent;
        }
        coefficient_ = BigInt(digits); exponent_ = check_exponent(exponent - fractional); normalize();
    }
    std::size_t precision() const noexcept { return precision_; }
    const BigInt& coefficient() const noexcept { return coefficient_; }
    int exponent() const noexcept { return exponent_; }
    bool is_zero() const noexcept { return coefficient_.is_zero(); }
    std::string to_string() const {
        if (is_zero()) return "0";
        auto digits = coefficient_.abs().to_string();
        std::string result = coefficient_.negative() ? "-" : "";
        result += digits[0];
        if (digits.size() > 1) result += "." + digits.substr(1);
        auto exponent = std::int64_t(exponent_) + static_cast<std::int64_t>(digits.size()) - 1;
        if (exponent) result += "e" + std::to_string(exponent);
        return result;
    }
    BigFloat with_precision(std::size_t digits) const { return make(coefficient_, exponent_, digits); }
    BigFloat operator-() const { auto result = *this; result.coefficient_ = -result.coefficient_; return result; }
    friend BigFloat operator+(const BigFloat& a, const BigFloat& b) {
        auto exponent = std::min(a.exponent_, b.exponent_);
        return make(a.coefficient_ * power10(a.exponent_ - exponent) + b.coefficient_ * power10(b.exponent_ - exponent),
            exponent, std::max(a.precision_, b.precision_));
    }
    friend BigFloat operator-(const BigFloat& a, const BigFloat& b) { return a + (-b); }
    friend BigFloat operator*(const BigFloat& a, const BigFloat& b) {
        return make(a.coefficient_ * b.coefficient_, std::int64_t(a.exponent_) + b.exponent_, std::max(a.precision_, b.precision_));
    }
    friend BigFloat operator/(const BigFloat& a, const BigFloat& b) {
        if (b.is_zero()) throw std::domain_error("decimal division by zero");
        auto precision = std::max(a.precision_, b.precision_);
        if (a.is_zero()) return BigFloat(0, precision);
        auto numerator = a.coefficient_.abs(), denominator = b.coefficient_.abs();
        auto order = static_cast<int>(numerator.to_string().size()) - static_cast<int>(denominator.to_string().size());
        if (order >= 0 ? numerator < denominator * power10(order) : numerator * power10(-order) < denominator) --order;
        auto scale = static_cast<int>(precision) - 1 - order;
        if (scale >= 0) numerator *= power10(scale); else denominator *= power10(-scale);
        auto qr = divmod(numerator, denominator);
        round_quotient(qr.first, qr.second, denominator);
        if (a.coefficient_.negative() != b.coefficient_.negative()) qr.first = -qr.first;
        return make(std::move(qr.first), std::int64_t(a.exponent_) - b.exponent_ - scale, precision);
    }
    friend int compare(const BigFloat& a, const BigFloat& b) {
        auto exponent = std::min(a.exponent_, b.exponent_);
        return compare(a.coefficient_ * power10(a.exponent_ - exponent), b.coefficient_ * power10(b.exponent_ - exponent));
    }
    friend bool operator==(const BigFloat& a, const BigFloat& b) { return compare(a, b) == 0; }
    friend bool operator!=(const BigFloat& a, const BigFloat& b) { return compare(a, b) != 0; }
    friend bool operator<(const BigFloat& a, const BigFloat& b) { return compare(a, b) < 0; }
    friend bool operator>(const BigFloat& a, const BigFloat& b) { return compare(a, b) > 0; }
    friend bool operator<=(const BigFloat& a, const BigFloat& b) { return compare(a, b) <= 0; }
    friend bool operator>=(const BigFloat& a, const BigFloat& b) { return compare(a, b) >= 0; }
    BigFloat& operator+=(const BigFloat& b) { return *this = *this + b; }
    BigFloat& operator-=(const BigFloat& b) { return *this = *this - b; }
    BigFloat& operator*=(const BigFloat& b) { return *this = *this * b; }
    BigFloat& operator/=(const BigFloat& b) { return *this = *this / b; }
    friend std::ostream& operator<<(std::ostream& out, const BigFloat& value) { return out << value.to_string(); }
private:
    BigInt coefficient_;
    int exponent_ = 0;
    std::size_t precision_ = default_precision;
    static std::size_t check_precision(std::size_t value) {
        if (!value || value > std::size_t(exponent_limit)) throw std::invalid_argument("decimal precision must be 1..100000");
        return value;
    }
    static int check_exponent(std::int64_t value) {
        if (value < -exponent_limit || value > exponent_limit) throw std::out_of_range("decimal exponent");
        return static_cast<int>(value);
    }
    static BigInt power10(int exponent) {
        if (exponent < 0) throw std::invalid_argument("negative decimal power");
        return pow(BigInt(10), static_cast<std::size_t>(exponent));
    }
    static void round_quotient(BigInt& quotient, const BigInt& remainder, const BigInt& divisor) {
        auto twice = remainder * BigInt(2);
        if (twice > divisor || (twice == divisor && quotient.test_bit(0))) quotient += BigInt(1);
    }
    static BigFloat make(BigInt coefficient, std::int64_t exponent, std::size_t precision) {
        BigFloat result; result.coefficient_ = std::move(coefficient); result.precision_ = check_precision(precision);
        result.exponent_ = result.is_zero() ? 0 : check_exponent(exponent); result.normalize(); return result;
    }
    void normalize() {
        if (is_zero()) { exponent_ = 0; return; }
        auto magnitude = coefficient_.abs(); auto digits = magnitude.to_string().size();
        bool negative = coefficient_.negative();
        if (digits > precision_) {
            auto shift = digits - precision_; auto divisor = pow(BigInt(10), shift);
            auto qr = divmod(magnitude, divisor); round_quotient(qr.first, qr.second, divisor);
            magnitude = std::move(qr.first); exponent_ = check_exponent(std::int64_t(exponent_) + static_cast<std::int64_t>(shift));
        }
        while ((magnitude % BigInt(10)).is_zero()) {
            magnitude /= BigInt(10); exponent_ = check_exponent(std::int64_t(exponent_) + 1);
        }
        coefficient_ = negative ? -magnitude : magnitude;
    }
};

} // namespace firestone::bignum
