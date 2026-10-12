#pragma once
#include "../libir/Common.hpp"

namespace firestone::sema {

enum class ArithmeticError { None, Overflow, DivisionByZero, InvalidShift, Unsupported };
struct CheckedInteger {
    std::optional<std::int64_t> value;
    ArithmeticError error = ArithmeticError::None;
    explicit operator bool() const { return value.has_value(); }
};
struct SignedRange {
    std::int64_t minimum, maximum;
    explicit SignedRange(unsigned bits) {
        if (!bits || bits > 64) throw std::invalid_argument("checked signed arithmetic supports widths 1 through 64");
        minimum = bits == 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (bits - 1));
        maximum = bits == 64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (bits - 1)) - 1;
    }
    bool contains(std::int64_t value) const { return minimum <= value && value <= maximum; }
};

// No intermediate overflowing signed operation is evaluated. Shl rejects negative
// inputs; Shr uses arithmetic (sign-extending) semantics independent of the host.
inline CheckedInteger checked_integer(ir::Opcode opcode, std::int64_t a, std::int64_t b = 0, unsigned bits = 64) {
    SignedRange range(bits);
    const auto min = range.minimum, max = range.maximum;
    auto failure = [](ArithmeticError error) { return CheckedInteger{{}, error}; };
    if (opcode != ir::Opcode::Add && opcode != ir::Opcode::Sub && opcode != ir::Opcode::Mul &&
        opcode != ir::Opcode::Div && opcode != ir::Opcode::Mod && opcode != ir::Opcode::Neg &&
        opcode != ir::Opcode::Shl && opcode != ir::Opcode::Shr) return failure(ArithmeticError::Unsupported);
    const bool shift = opcode == ir::Opcode::Shl || opcode == ir::Opcode::Shr;
    const bool unary = opcode == ir::Opcode::Neg;
    if (!range.contains(a) || (!shift && !unary && !range.contains(b))) return failure(ArithmeticError::Overflow);
    switch (opcode) {
    case ir::Opcode::Add:
        if ((b > 0 && a > max - b) || (b < 0 && a < min - b)) return failure(ArithmeticError::Overflow);
        return {a + b, ArithmeticError::None};
    case ir::Opcode::Sub:
        if ((b < 0 && a > max + b) || (b > 0 && a < min + b)) return failure(ArithmeticError::Overflow);
        return {a - b, ArithmeticError::None};
    case ir::Opcode::Mul:
        if ((a > 0 && ((b > 0 && a > max / b) || (b < 0 && b < min / a))) ||
            (a < 0 && ((b > 0 && a < min / b) || (b < 0 && a < max / b)))) return failure(ArithmeticError::Overflow);
        return {a * b, ArithmeticError::None};
    case ir::Opcode::Div: case ir::Opcode::Mod:
        if (!b) return failure(ArithmeticError::DivisionByZero);
        if (a == min && b == -1) {
            if (opcode == ir::Opcode::Mod) return {0, ArithmeticError::None};
            return failure(ArithmeticError::Overflow);
        }
        return {opcode == ir::Opcode::Div ? a / b : a % b, ArithmeticError::None};
    case ir::Opcode::Neg:
        if (a == min) return failure(ArithmeticError::Overflow);
        return {-a, ArithmeticError::None};
    case ir::Opcode::Shl:
        if (b < 0 || b >= bits || a < 0) return failure(ArithmeticError::InvalidShift);
        if (a > (max >> b)) return failure(ArithmeticError::Overflow);
        return {a << b, ArithmeticError::None};
    case ir::Opcode::Shr:
        if (b < 0 || b >= bits) return failure(ArithmeticError::InvalidShift);
        return {a >= 0 ? a >> b : -((-(a + 1)) >> b) - 1, ArithmeticError::None};
    default: return failure(ArithmeticError::Unsupported);
    }
}

inline ir::Diagnostics check_overflow(const ir::Instruction& instruction) {
    ir::Diagnostics result;
    for (const auto& operand : instruction.operands) if (operand.kind == ir::OperandKind::Integer && operand.type.kind == ir::TypeKind::Integer) {
        auto bits = operand.type.bits;
        if (!bits) ir::diagnose(result, "integer.width", "integer literal has zero width");
        else if (bits > 64) ir::diagnose(result, "integer.width", "integer width above 64 is unsupported");
        else if (operand.type == ir::Type::boolean()) {
            if (operand.integer != 0 && operand.integer != 1) ir::diagnose(result, "integer.range", "boolean literal is outside zero/one");
        } else if (bits <= 64 && !SignedRange(bits).contains(operand.integer)) ir::diagnose(result, "integer.range", "integer literal exceeds its signed width");
    }
    if (instruction.type.kind != ir::TypeKind::Integer || !instruction.type.bits || instruction.type.bits > 64 || instruction.operands.empty()) return result;
    if (instruction.operands[0].kind != ir::OperandKind::Integer) return result;
    const bool unary = instruction.opcode == ir::Opcode::Neg;
    if (!unary && (instruction.operands.size() != 2 || instruction.operands[1].kind != ir::OperandKind::Integer)) return result;
    auto checked = checked_integer(instruction.opcode, instruction.operands[0].integer, unary ? 0 : instruction.operands[1].integer, instruction.type.bits);
    if (checked.error == ArithmeticError::Overflow) ir::diagnose(result, "integer.overflow", "constant arithmetic exceeds its signed width");
    if (checked.error == ArithmeticError::DivisionByZero) ir::diagnose(result, "integer.divzero", "constant divisor is zero");
    if (checked.error == ArithmeticError::InvalidShift) ir::diagnose(result, "integer.shift", "constant shift is invalid");
    return result;
}

} // namespace firestone::sema
