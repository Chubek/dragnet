#include <dragscript/value.hpp>
#include <charconv>
#include <cmath>
#include <limits>

namespace dragscript {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53,
              "Dragscript numbers require IEEE-754 binary64");
Value Value::null() { Value value; value.data_ = Null{}; return value; }
Value::Value(std::u16string value) : data_(std::make_shared<const std::u16string>(std::move(value))) {}
Value::Tag Value::tag() const noexcept { return static_cast<Tag>(data_.index()); }
bool Value::boolean() const { return std::get<bool>(data_); }
double Value::number() const { return std::get<double>(data_); }
const std::u16string& Value::string() const { return *std::get<std::shared_ptr<const std::u16string>>(data_); }
bool Value::truthy() const noexcept {
    switch (tag()) {
    case Tag::undefined: case Tag::null: return false;
    case Tag::boolean: return boolean();
    case Tag::number: return number() != 0 && !std::isnan(number());
    case Tag::string: return !string().empty();
    }
    return false;
}
bool Value::strictly_equals(const Value& other) const noexcept {
    if (tag() != other.tag()) return false;
    switch (tag()) {
    case Tag::undefined: case Tag::null: return true;
    case Tag::boolean: return boolean() == other.boolean();
    case Tag::number: return number() == other.number(); // NaN differs; both zero signs compare equal.
    case Tag::string: return string() == other.string();
    }
    return false;
}
std::string Value::display() const {
    switch (tag()) {
    case Tag::undefined: return "undefined";
    case Tag::null: return "null";
    case Tag::boolean: return boolean() ? "true" : "false";
    case Tag::number: {
        if (std::isnan(number())) return "NaN";
        if (std::isinf(number())) return number() < 0 ? "-Infinity" : "Infinity";
        if (number() == 0) return std::signbit(number()) ? "-0" : "0";
        char buffer[64];
        auto result = std::to_chars(buffer, buffer + sizeof buffer, number());
        return std::string(buffer, result.ptr);
    }
    case Tag::string: {
        // Escape code units so lone surrogates survive debug formatting.
        std::string result;
        constexpr char hex[] = "0123456789abcdef";
        for (auto unit : string()) {
            if (unit >= 32 && unit < 127 && unit != '\\') result += static_cast<char>(unit);
            else {
                result += "\\u";
                for (int shift = 12; shift >= 0; shift -= 4) result += hex[(unit >> shift) & 15];
            }
        }
        return result;
    }
    }
    return {};
}
} // namespace dragscript
