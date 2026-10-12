#pragma once
#include "Address.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace firestone::vm {

// Runtime values and signatures are independent of object storage.
enum class ValueKind { Null, Boolean, Integer, Float, String, Address, Object };
using Value = std::variant<std::monostate, bool, std::int64_t, double, std::string, Address, ObjectRef>;
inline ValueKind kind(const Value& value) { return static_cast<ValueKind>(value.index()); }
inline const char* kind_name(ValueKind value) {
    static constexpr const char* names[] = {"null", "boolean", "integer", "float", "string", "address", "object"};
    auto i = static_cast<Index>(value);
    if (i >= 7) throw std::invalid_argument("invalid VM value kind");
    return names[i];
}
inline bool valid_kind(ValueKind value) { return static_cast<Index>(value) < 7; }

class VMError : public std::runtime_error {
public:
    VMError(std::string code, std::string message, CodeAddress position = {})
        : std::runtime_error(std::move(message)), code(std::move(code)), position(position) {}
    std::string code;
    CodeAddress position;
};

template<class T> inline const T& require(const Value& value) {
    auto result = std::get_if<T>(&value);
    if (!result) throw VMError("vm.type", "unexpected runtime value kind");
    return *result;
}

struct Signature {
    std::vector<ValueKind> parameters;
    ValueKind result = ValueKind::Null; // Null denotes a void result.
    friend bool operator==(const Signature& a, const Signature& b) {
        return a.parameters == b.parameters && a.result == b.result;
    }
};
inline void check_arguments(const Signature& signature, const std::vector<Value>& arguments) {
    if (arguments.size() != signature.parameters.size()) throw VMError("vm.arity", "argument count differs from signature");
    for (Index i = 0; i < arguments.size(); ++i)
        if (kind(arguments[i]) != signature.parameters[i]) throw VMError("vm.type", "argument kind differs from signature");
}
inline void check_result(const Signature& signature, const Value& result) {
    if (kind(result) != signature.result) throw VMError("vm.type", "result kind differs from signature");
}

} // namespace firestone::vm
