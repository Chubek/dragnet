#pragma once
#include "Value.hpp"
#include "../libsema/OverflowCheck.hpp"
#include <array>

namespace firestone::vm {

// Name, input count (-1 = instruction/signature dependent), output count.
#define FIRESTONE_VM_OPCODES(X) \
    X(Nop, 0, 0) X(Constant, 0, 1) X(Move, 1, 1) \
    X(LocalGet, 0, 1) X(LocalSet, 1, 0) X(Pop, 1, 0) X(Dup, 1, 2) X(Swap, 2, 2) \
    X(Add, 2, 1) X(Sub, 2, 1) X(Mul, 2, 1) X(Div, 2, 1) X(Mod, 2, 1) \
    X(And, 2, 1) X(Or, 2, 1) X(Xor, 2, 1) X(Shl, 2, 1) X(Shr, 2, 1) \
    X(Neg, 1, 1) X(Not, 1, 1) X(Eq, 2, 1) X(Ne, 2, 1) X(Lt, 2, 1) \
    X(Le, 2, 1) X(Gt, 2, 1) X(Ge, 2, 1) X(Select, 3, 1) \
    X(Load, 1, 1) X(Store, 2, 0) X(GetGlobal, 0, 1) X(SetGlobal, 1, 0) \
    X(GetField, 1, 1) X(SetField, 2, 0) X(Call, -1, -1) X(NativeCall, -1, -1) \
    X(Jump, 0, 0) X(Branch, 1, 0) X(Return, -1, 0)

enum class Opcode : std::uint8_t {
#define FIRESTONE_VM_ENUM(name, inputs, outputs) name,
    FIRESTONE_VM_OPCODES(FIRESTONE_VM_ENUM)
#undef FIRESTONE_VM_ENUM
    Count
};
struct OpcodeInfo { const char* name; int inputs, outputs; };
inline constexpr Index opcode_count = static_cast<Index>(Opcode::Count);
inline constexpr std::array<OpcodeInfo, opcode_count> opcode_table = {{
#define FIRESTONE_VM_INFO(name, inputs, outputs) {#name, inputs, outputs},
    FIRESTONE_VM_OPCODES(FIRESTONE_VM_INFO)
#undef FIRESTONE_VM_INFO
}};
#undef FIRESTONE_VM_OPCODES
inline bool valid_opcode(Opcode opcode) { return static_cast<Index>(opcode) < opcode_count; }
inline const OpcodeInfo& opcode_info(Opcode opcode) {
    if (!valid_opcode(opcode)) throw std::invalid_argument("invalid VM opcode");
    return opcode_table[static_cast<Index>(opcode)];
}
inline bool arithmetic(Opcode opcode) { return opcode >= Opcode::Add && opcode <= Opcode::Ge; }
inline ir::Opcode ir_opcode(Opcode opcode) {
    if (!arithmetic(opcode)) throw std::invalid_argument("not a scalar VM operation");
    return static_cast<ir::Opcode>(static_cast<int>(ir::Opcode::Add) +
        static_cast<int>(opcode) - static_cast<int>(Opcode::Add));
}
inline std::int64_t signed_bits(std::uint64_t value) {
    const auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return value <= max ? static_cast<std::int64_t>(value) : -1 - static_cast<std::int64_t>(~value);
}
inline Value scalar(Opcode opcode, const std::vector<Value>& values, unsigned bits = 64) {
    auto op = ir_opcode(opcode);
    if (values.size() != static_cast<Index>(opcode_info(opcode).inputs)) throw VMError("vm.arity", "scalar operand count");
    if (!bits || bits > 64) throw VMError("vm.width", "integer width must be 1 through 64");
    const auto& a = values.at(0);
    const auto& b = values.size() > 1 ? values[1] : a;
    if (kind(a) != kind(b)) throw VMError("vm.type", "scalar operand kinds differ");
    if (auto x = std::get_if<bool>(&a)) {
        const auto y = require<bool>(b);
        switch (opcode) {
        case Opcode::And: return *x && y;
        case Opcode::Or: return *x || y;
        case Opcode::Xor: return *x != y;
        case Opcode::Not: return !*x;
        case Opcode::Eq: return *x == y;
        case Opcode::Ne: return *x != y;
        case Opcode::Lt: return *x < y;
        case Opcode::Le: return *x <= y;
        case Opcode::Gt: return *x > y;
        case Opcode::Ge: return *x >= y;
        default: throw VMError("vm.type", "boolean arithmetic is unsupported");
        }
    }
    if (auto x = std::get_if<std::int64_t>(&a)) {
        auto y = require<std::int64_t>(b);
        sema::SignedRange range(bits);
        const bool shift = opcode == Opcode::Shl || opcode == Opcode::Shr;
        if (!range.contains(*x) || (!shift && !range.contains(y))) throw VMError("vm.overflow", "operand exceeds signed width");
        switch (opcode) {
        case Opcode::Eq: return *x == y;
        case Opcode::Ne: return *x != y;
        case Opcode::Lt: return *x < y;
        case Opcode::Le: return *x <= y;
        case Opcode::Gt: return *x > y;
        case Opcode::Ge: return *x >= y;
        case Opcode::And: case Opcode::Or: case Opcode::Xor: case Opcode::Not: {
            auto u = static_cast<std::uint64_t>(*x), v = static_cast<std::uint64_t>(y);
            auto r = opcode == Opcode::And ? u & v : opcode == Opcode::Or ? u | v : opcode == Opcode::Xor ? u ^ v : ~u;
            if (bits < 64) {
                auto mask = (std::uint64_t{1} << bits) - 1;
                r &= mask;
                if (r & (std::uint64_t{1} << (bits - 1))) r |= ~mask;
            }
            return signed_bits(r);
        }
        default: {
            auto result = sema::checked_integer(op, *x, y, bits);
            if (!result) {
                const char* code = result.error == sema::ArithmeticError::DivisionByZero ? "vm.divzero" :
                    result.error == sema::ArithmeticError::InvalidShift ? "vm.shift" : "vm.overflow";
                throw VMError(code, "checked integer operation failed");
            }
            return *result.value;
        }
        }
    }
    auto x = require<double>(a), y = require<double>(b);
    switch (opcode) {
    case Opcode::Add: return x + y;
    case Opcode::Sub: return x - y;
    case Opcode::Mul: return x * y;
    case Opcode::Div: return x / y;
    case Opcode::Neg: return -x;
    case Opcode::Eq: return x == y;
    case Opcode::Ne: return x != y;
    case Opcode::Lt: return x < y;
    case Opcode::Le: return x <= y;
    case Opcode::Gt: return x > y;
    case Opcode::Ge: return x >= y;
    default: throw VMError("vm.type", "unsupported floating operation");
    }
}

} // namespace firestone::vm
