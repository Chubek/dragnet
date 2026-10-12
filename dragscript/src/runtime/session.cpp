#include "session.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace dragscript::detail {
namespace {
std::u16string widen(const std::string& text) {
    std::u16string result; result.reserve(text.size());
    for (char c : text) result.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    return result;
}
// ECMAScript whitespace (WhiteSpace + LineTerminator) that StringNumericLiteral trims.
bool is_trim_space(char16_t c) {
    return c <= 0x0d || c == 0x20 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}
// Canonical array index: decimal digits without redundant leading zeros below 2^32-1.
bool array_index(const std::u16string& key, std::size_t& index) {
    if (key.empty() || key.size() > 10) return false;
    if (key.size() > 1 && key.front() == '0') return false;
    std::size_t result = 0;
    for (char16_t c : key) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + static_cast<std::size_t>(c - '0');
    }
    if (result >= 0xffffffffu) return false;
    index = result; return true;
}
} // namespace
std::u16string number_to_string(double value) {
    if (std::isnan(value)) return u"NaN";
    if (std::isinf(value)) return value < 0 ? u"-Infinity" : u"Infinity";
    if (value == 0) return u"0"; // Both zero signs stringify as "0".
    char buffer[64];
    const auto end = buffer + sizeof buffer;
    const auto result = std::to_chars(buffer, end, value, std::chars_format::scientific);
    // to_chars returns the position just past the written text and does not terminate it,
    // so every byte read below stays inside that range.
    const auto* exponent_marker = std::find(buffer, result.ptr, 'e');
    std::string digits;
    for (auto p = buffer; p != exponent_marker; ++p)
        if (*p != '.' && *p != '-') digits.push_back(*p); // The sign is applied separately.
    int exponent = 0;
    if (exponent_marker != result.ptr) {
        auto p = exponent_marker + 1;
        int sign = 1;
        if (p != result.ptr && (*p == '+' || *p == '-')) { if (*p == '-') sign = -1; ++p; }
        for (; p != result.ptr && *p >= '0' && *p <= '9'; ++p) exponent = exponent * 10 + (*p - '0');
        exponent *= sign;
    }
    const int point = exponent + 1; // Value == 0.<digits> * 10^point.
    std::string result_text;
    const int count = static_cast<int>(digits.size());
    if (count <= point && point <= 21) result_text = digits + std::string(point - count, '0');
    else if (0 < point && point <= 21) result_text = digits.substr(0, point) + "." + digits.substr(point);
    else if (-6 < point && point <= 0) result_text = "0." + std::string(-point, '0') + digits;
    else {
        result_text = digits.substr(0, 1);
        if (count > 1) result_text += "." + digits.substr(1);
        result_text += 'e';
        result_text += point - 1 >= 0 ? '+' : '-';
        result_text += std::to_string(std::abs(point - 1));
    }
    if (std::signbit(value)) result_text.insert(result_text.begin(), '-');
    return widen(result_text);
}
std::u16string Session::to_string(SourceSpan span, const RuntimeValue& value) {
    if (const auto* p = std::get_if<Value>(&value)) {
        switch (p->tag()) {
        case Value::Tag::undefined: return u"undefined";
        case Value::Tag::null: return u"null";
        case Value::Tag::boolean: return p->boolean() ? u"true" : u"false";
        case Value::Tag::number: return number_to_string(p->number());
        case Value::Tag::string: return p->string();
        }
    }
    if (std::holds_alternative<ObjectId>(value)) return u"[object Object]";
    if (const auto* id = std::get_if<ArrayId>(&value)) {
        const auto& elements = arrays[id->index].elements;
        std::u16string result;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (i) result += u',';
            // Array join renders nullish elements as empty, recursively for nested arrays.
            if (is_nullish(elements[i])) continue;
            result += to_string(span, elements[i]);
            if (result.size() > limits.max_string_units)
                fail(span, "DS_LIMIT", "Joined string exceeds limit", "string_units");
        }
        return result;
    }
    return u"function"; // Source text is not reproduced; see docs/runtime-semantics.md.
}
double Session::to_number(SourceSpan span, const RuntimeValue& value) {
    if (const auto* p = std::get_if<Value>(&value)) {
        switch (p->tag()) {
        case Value::Tag::number: return p->number();
        case Value::Tag::boolean: return p->boolean() ? 1 : 0;
        case Value::Tag::null: return 0;
        case Value::Tag::undefined: return std::numeric_limits<double>::quiet_NaN();
        case Value::Tag::string: break;
        }
        const auto& text = p->string();
        std::size_t begin = 0, end = text.size();
        while (begin < end && is_trim_space(text[begin])) ++begin;
        while (end > begin && is_trim_space(text[end - 1])) --end;
        if (begin == end) return 0; // Empty String is 0.
        const std::u16string trimmed = text.substr(begin, end - begin);
        if (trimmed == u"Infinity") return std::numeric_limits<double>::infinity();
        if (trimmed == u"+Infinity") return std::numeric_limits<double>::infinity();
        if (trimmed == u"-Infinity") return -std::numeric_limits<double>::infinity();
        int sign = 1;
        std::size_t offset = 0;
        if (trimmed.front() == '+' || trimmed.front() == '-') { sign = trimmed.front() == '-' ? -1 : 1; offset = 1; }
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        int base = 10;
        // NonDecimalIntegerLiteral has no sign, so "-0x10" is not a numeric string.
        if (offset == 0 && trimmed.size() > 1 && trimmed[0] == '0') {
            const char16_t prefix = trimmed[offset + 1];
            if (prefix == 'x' || prefix == 'X') base = 16;
            else if (prefix == 'o' || prefix == 'O') base = 8;
            else if (prefix == 'b' || prefix == 'B') base = 2;
        }
        const auto start = offset + (base == 10 ? 0 : 2);
        if (start >= trimmed.size()) return nan;
        double parsed = 0;
        if (base == 10) {
            std::string ascii;
            for (std::size_t i = start; i < trimmed.size(); ++i) {
                if (trimmed[i] > 0x7f) return nan;
                ascii.push_back(static_cast<char>(trimmed[i]));
            }
            // Reject spellings from_chars accepts but StringNumericLiteral does not.
            if (ascii.find_first_of("iInN") != std::string::npos) return nan;
            const auto result = std::from_chars(ascii.data(), ascii.data() + ascii.size(), parsed);
            if (result.ec != std::errc{} || result.ptr != ascii.data() + ascii.size()) return nan;
        } else {
            // Radix literals are parsed directly; a partial or invalid digit run is NaN.
            unsigned long long value = 0;
            for (std::size_t i = start; i < trimmed.size(); ++i) {
                if (trimmed[i] > 0x7f) return nan;
                unsigned digit = 0;
                const char c = static_cast<char>(trimmed[i]);
                if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
                else return nan;
                if (digit >= static_cast<unsigned>(base)) return nan;
                if (value > (0xffffffffffffffffull - digit) / static_cast<unsigned>(base)) return nan;
                value = value * static_cast<unsigned>(base) + digit;
            }
            return sign * static_cast<double>(value);
        }
        return sign * parsed;
    }
    if (is_object(value)) return to_number(span, to_primitive(span, value, true));
    return std::numeric_limits<double>::quiet_NaN();
}
RuntimeValue Session::to_primitive(SourceSpan span, const RuntimeValue& value, bool prefer_number) {
    (void)prefer_number; // No user-defined conversions exist yet, so both hints end at toString.
    if (is_object(value)) return Value(to_string(span, value));
    return value;
}
RuntimeValue Session::binary(SourceSpan span, std::string op, const RuntimeValue& left, const RuntimeValue& right) {
    // Compound forms such as "+=" reuse the binary contract of their operator.
    if (op.size() > 1 && op.back() == '=' && op != "===" && op != "!==" && op != "<=" && op != ">=") op.pop_back();
    if (op == "===") return Value(strict_equals(left, right));
    if (op == "!==") return Value(!strict_equals(left, right));
    if (op == "+") {
        const auto a = to_primitive(span, left, false), b = to_primitive(span, right, false);
        const auto textual = [&](const RuntimeValue& v) {
            const auto* p = std::get_if<Value>(&v); return p && p->tag() == Value::Tag::string;
        };
        if (textual(a) || textual(b)) {
            const auto first = to_string(span, a), second = to_string(span, b);
            if (first.size() > limits.max_string_units || second.size() > limits.max_string_units - first.size())
                fail(span, "DS_LIMIT", "Concatenated string exceeds limit", "string_units");
            allocate(span, (first.size() + second.size()) * sizeof(char16_t));
            std::u16string joined = first; joined += second; return Value(std::move(joined));
        }
        return Value(to_number(span, a) + to_number(span, b));
    }
    if (op == "<" || op == "<=" || op == ">" || op == ">=") {
        const auto a = to_primitive(span, left, true), b = to_primitive(span, right, true);
        if (std::holds_alternative<Value>(a) && std::get<Value>(a).tag() == Value::Tag::string &&
            std::holds_alternative<Value>(b) && std::get<Value>(b).tag() == Value::Tag::string) {
            const auto& first = std::get<Value>(a).string(), second = std::get<Value>(b).string();
            if (op == "<") return Value(first < second);
            if (op == "<=") return Value(first <= second);
            if (op == ">") return Value(first > second);
            return Value(first >= second);
        }
        const auto x = to_number(span, a), y = to_number(span, b);
        if (op == "<") return Value(x < y);
        if (op == "<=") return Value(x <= y);
        if (op == ">") return Value(x > y);
        return Value(x >= y);
    }
    const auto x = to_number(span, left), y = to_number(span, right);
    if (op == "-") return Value(x - y);
    if (op == "*") return Value(x * y);
    if (op == "/") return Value(x / y);
    if (op == "%") return Value(std::fmod(x, y));
    fail(span, "DS_INTERNAL", "Unknown binary operator: " + op);
}
RuntimeValue Session::get_property(SourceSpan span, const RuntimeValue& base, const std::u16string& key) {
    if (const auto* p = std::get_if<Value>(&base)) {
        if (p->tag() == Value::Tag::null || p->tag() == Value::Tag::undefined)
            fail(span, "DS_TYPE", "Cannot read a property of " + p->display());
        if (p->tag() == Value::Tag::string) {
            if (key == u"length") return Value(static_cast<double>(p->string().size()));
            std::size_t index = 0;
            if (array_index(key, index) && index < p->string().size())
                return Value(std::u16string(1, p->string()[index]));
            return Value{};
        }
        return Value{}; // Primitives expose no other properties in this subset.
    }
    if (const auto* id = std::get_if<ObjectId>(&base)) {
        for (const auto& property : objects[id->index].properties)
            if (property.key == key) return property.value;
        return Value{};
    }
    if (const auto* id = std::get_if<ArrayId>(&base)) {
        if (key == u"length") return Value(static_cast<double>(arrays[id->index].elements.size()));
        std::size_t index = 0;
        if (array_index(key, index) && index < arrays[id->index].elements.size()) return arrays[id->index].elements[index];
        return Value{};
    }
    return Value{}; // Callables carry no properties yet.
}
void Session::array_push(SourceSpan span, ArrayId array, RuntimeValue value) {
    auto& elements = arrays[array.index].elements;
    if (elements.size() >= limits.max_array_length) fail(span, "DS_LIMIT", "Array length limit exceeded", "array_length");
    allocate(span, sizeof(RuntimeValue)); elements.push_back(std::move(value));
}
void Session::array_set(SourceSpan span, ArrayId array, std::size_t position, RuntimeValue value) {
    auto& elements = arrays[array.index].elements;
    if (position >= limits.max_array_length) fail(span, "DS_LIMIT", "Array length limit exceeded", "array_length");
    if (position >= elements.size()) { // Dense padding: absent indices read as undefined.
        allocate(span, position + 1 - elements.size(), sizeof(RuntimeValue));
        elements.resize(position + 1, RuntimeValue(Value{}));
    }
    elements[position] = std::move(value);
}
void Session::set_property(SourceSpan span, const RuntimeValue& base, const std::u16string& key, RuntimeValue value) {
    if (const auto* id = std::get_if<ObjectId>(&base)) { set_own_property(span, *id, key, std::move(value)); return; }
    if (const auto* id = std::get_if<ArrayId>(&base)) {
        if (key == u"length") fail(span, "DS_TYPE", "Assigning array length is not supported");
        std::size_t position = 0;
        if (array_index(key, position)) { array_set(span, *id, position, std::move(value)); return; }
        fail(span, "DS_TYPE", "Only index assignment is supported for arrays");
    }
    if (const auto* p = std::get_if<Value>(&base)) fail(span, "DS_TYPE", "Cannot assign a property of " + p->display());
    fail(span, "DS_TYPE", "Cannot assign a property of a callable value");
}
RuntimeValue Session::call_host(SourceSpan span, HostReference host, const std::vector<RuntimeValue>& args) {
    const auto& binding = *host.binding;
    if (args.size() < binding.options.min_arguments || args.size() > binding.options.max_arguments)
        fail(span, "DS_ARITY", "Host call argument count is outside its contract");
    allocate(span, args.size(), sizeof(Value));
    std::vector<Value> values; values.reserve(args.size());
    for (const auto& arg : args) {
        if (!std::holds_alternative<Value>(arg)) fail(span, "DS_BOUNDARY", "Function values cannot cross the host boundary");
        values.push_back(std::get<Value>(arg));
    }
    HostContext context{
        [&](std::uint64_t count) { return consume(count) && !cancelled(); },
        [&] { return cancelled(); }};
    HostResult result;
    try { result = binding.callback(values, context); }
    catch (const std::exception& error) { fail(span, "DS_HOST", std::string("Host callback failed: ") + error.what()); }
    catch (...) { fail(span, "DS_HOST", "Host callback threw a non-standard exception"); }
    if (cancelled()) fail(span, "DS_CANCELLED", "Execution cancelled after host callback");
    if (budget_exhausted) fail(span, "DS_LIMIT", "Host callback exhausted the step budget", "steps");
    if (!result.error.empty()) fail(span, "DS_HOST", std::move(result.error));
    if (result.value.tag() == Value::Tag::string) {
        if (result.value.string().size() > limits.max_string_units)
            fail(span, "DS_LIMIT", "String size limit exceeded", "string_units");
        allocate(span, result.value.string().size(), sizeof(char16_t));
    }
    return result.value;
}
} // namespace dragscript::detail