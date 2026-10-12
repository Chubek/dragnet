#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace firestone::ir {

using NodeId = std::size_t;
using BlockId = NodeId;
using ValueId = std::size_t;
inline constexpr std::size_t invalid_id = std::numeric_limits<std::size_t>::max();

enum class TypeKind { Unknown, Void, Integer, Float, Pointer, Memory, Function };

struct Type {
    TypeKind kind = TypeKind::Unknown;
    unsigned bits = 0;
    std::string name;

    static Type integer(unsigned bits = 32) {
        if (bits == 0) throw std::invalid_argument("integer width must be positive");
        return {TypeKind::Integer, bits, {}};
    }
    static Type floating(unsigned bits = 64) {
        if (bits == 0) throw std::invalid_argument("floating width must be positive");
        return {TypeKind::Float, bits, {}};
    }
    static Type boolean() { return integer(1); }
    static Type pointer(std::string pointee = {}) { return {TypeKind::Pointer, 0, std::move(pointee)}; }
    static Type memory() { return {TypeKind::Memory, 0, {}}; }
    static Type void_type() { return {TypeKind::Void, 0, {}}; }
    static Type function() { return {TypeKind::Function, 0, {}}; }
    bool numeric() const { return kind == TypeKind::Integer || kind == TypeKind::Float; }
    bool valid_value() const {
        if (kind == TypeKind::Unknown || kind == TypeKind::Void) return false;
        return numeric() ? bits > 0 : bits == 0;
    }
    friend bool operator==(const Type& a, const Type& b) {
        return a.kind == b.kind && a.bits == b.bits && a.name == b.name;
    }
    friend bool operator!=(const Type& a, const Type& b) { return !(a == b); }
};

enum class OperandKind { Value, Integer, Float, Symbol };

struct Operand {
    OperandKind kind = OperandKind::Value;
    ValueId value = invalid_id;
    std::int64_t integer = 0;
    double floating = 0;
    std::string symbol;
    Type type;

    static Operand ref(ValueId id) { if (id == invalid_id) throw std::invalid_argument("value reference is invalid"); Operand o; o.value = id; return o; }
    static Operand constant(std::int64_t n, Type type = Type::integer()) {
        Operand o; o.kind = OperandKind::Integer; o.integer = n; o.type = std::move(type); return o;
    }
    static Operand real(double n, Type type = Type::floating()) {
        Operand o; o.kind = OperandKind::Float; o.floating = n; o.type = std::move(type); return o;
    }
    static Operand named(std::string name, Type type = {}) {
        if (name.empty()) throw std::invalid_argument("operand symbol is empty");
        Operand o; o.kind = OperandKind::Symbol; o.symbol = std::move(name); o.type = std::move(type); return o;
    }
};

enum class Opcode {
    Constant, Copy, Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr, Neg, Not,
    Eq, Ne, Lt, Le, Gt, Ge, Phi, Select, Load, Store, Call, Jump, Branch, Return
};

inline bool is_terminator(Opcode op) { return op == Opcode::Jump || op == Opcode::Branch || op == Opcode::Return; }
inline bool is_effectful(Opcode op) { return op == Opcode::Load || op == Opcode::Store || op == Opcode::Call; }
inline bool is_comparison(Opcode op) { return op >= Opcode::Eq && op <= Opcode::Ge; }

struct Instruction {
    Opcode opcode = Opcode::Copy;
    std::optional<ValueId> result;
    Type type;
    std::vector<Operand> operands;
    std::vector<BlockId> targets;
    std::vector<BlockId> incoming_blocks;
    std::string annotation;
};

struct Diagnostic {
    std::string code;
    std::string message;
    NodeId node = invalid_id;
    std::size_t instruction = invalid_id;
};
using Diagnostics = std::vector<Diagnostic>;

inline void diagnose(Diagnostics& out, std::string code, std::string message,
                     NodeId node = invalid_id, std::size_t instruction = invalid_id) {
    out.push_back({std::move(code), std::move(message), node, instruction});
}

// A resolver supplies register types; immediate operands carry their own type.
inline Type operand_type(const Operand& o, const std::function<Type(ValueId)>& resolve) {
    return o.kind == OperandKind::Value ? resolve(o.value) : o.type;
}

inline Diagnostics verify_instruction(const Instruction& inst,
                                     const std::function<Type(ValueId)>& resolve,
                                     bool typed = true) {
    Diagnostics out;
    std::size_t min = 0, max = 0;
    switch (inst.opcode) {
    case Opcode::Constant: case Opcode::Copy: case Opcode::Neg: case Opcode::Not: case Opcode::Load:
        min = max = 1; break;
    case Opcode::Add: case Opcode::Sub: case Opcode::Mul: case Opcode::Div: case Opcode::Mod:
    case Opcode::And: case Opcode::Or: case Opcode::Xor: case Opcode::Shl: case Opcode::Shr:
    case Opcode::Eq: case Opcode::Ne: case Opcode::Lt: case Opcode::Le: case Opcode::Gt: case Opcode::Ge:
    case Opcode::Store: min = max = 2; break;
    case Opcode::Select: min = max = 3; break;
    case Opcode::Phi: min = 1; max = invalid_id; break;
    case Opcode::Call: max = invalid_id; break;
    case Opcode::Branch: min = max = 1; break;
    case Opcode::Return: max = 1; break;
    case Opcode::Jump: break;
    }
    if (inst.operands.size() < min || inst.operands.size() > max)
        diagnose(out, "arity", "invalid operand count");
    const bool produces = !is_terminator(inst.opcode) && inst.opcode != Opcode::Store &&
                          !(inst.opcode == Opcode::Call && inst.type.kind == TypeKind::Void);
    if (produces != inst.result.has_value()) diagnose(out, "result", "invalid result presence");
    const auto targets = inst.opcode == Opcode::Jump ? 1u : inst.opcode == Opcode::Branch ? 2u : 0u;
    if (inst.targets.size() != targets) diagnose(out, "targets", "invalid branch target count");
    if (inst.opcode == Opcode::Phi) {
        if (inst.incoming_blocks.size() != inst.operands.size()) diagnose(out, "phi", "phi labels and values differ");
    } else if (!inst.incoming_blocks.empty()) diagnose(out, "phi", "non-phi instruction has incoming labels");
    if (!typed) return out;
    std::vector<Type> types;
    for (const auto& o : inst.operands) {
        auto t = operand_type(o, resolve);
        if (!t.valid_value()) diagnose(out, "type", "operand has no valid value type");
        if (o.kind == OperandKind::Symbol && o.symbol.empty()) diagnose(out, "symbol", "operand symbol is empty");
        if (o.kind == OperandKind::Integer && t.kind != TypeKind::Integer) diagnose(out, "literal", "integer literal has noninteger type");
        if (o.kind == OperandKind::Float && t.kind != TypeKind::Float) diagnose(out, "literal", "floating literal has nonfloating type");
        if (o.kind == OperandKind::Integer && t == Type::boolean() && o.integer != 0 && o.integer != 1)
            diagnose(out, "literal", "boolean literal must be zero or one");
        types.push_back(std::move(t));
    }
    if (produces && !inst.type.valid_value()) diagnose(out, "type", "result has no valid value type");
    if (types.size() < min || types.size() > max) return out;
    if (inst.opcode >= Opcode::Add && inst.opcode <= Opcode::Shr) {
        if (!inst.type.numeric() || types[0] != inst.type || types[1] != inst.type)
            diagnose(out, "type", "binary operands must match the numeric result type");
        if (inst.opcode >= Opcode::Mod && inst.type.kind != TypeKind::Integer)
            diagnose(out, "type", "integer operation requires integer operands");
    } else if (is_comparison(inst.opcode)) {
        if (inst.type != Type::boolean() || types[0] != types[1] || !types[0].numeric())
            diagnose(out, "type", "comparison requires equal numeric operands and a boolean result");
    } else if (inst.opcode == Opcode::Select) {
        if (types[0] != Type::boolean() || types[1] != inst.type || types[2] != inst.type)
            diagnose(out, "type", "select requires a boolean condition and matching values");
    } else if (inst.opcode == Opcode::Phi || inst.opcode == Opcode::Copy || inst.opcode == Opcode::Constant) {
        for (const auto& t : types) if (t != inst.type) diagnose(out, "type", "operand and result types differ");
        if (inst.opcode == Opcode::Constant && inst.operands[0].kind == OperandKind::Value)
            diagnose(out, "constant", "constant instruction requires a literal or symbol");
    } else if (inst.opcode == Opcode::Neg || inst.opcode == Opcode::Not) {
        if (types[0] != inst.type || !inst.type.numeric() ||
            (inst.opcode == Opcode::Not && inst.type.kind != TypeKind::Integer))
            diagnose(out, "type", "invalid unary operand type");
    } else if (inst.opcode == Opcode::Branch && types[0] != Type::boolean()) {
        diagnose(out, "type", "branch condition must be boolean");
    } else if ((inst.opcode == Opcode::Load || inst.opcode == Opcode::Store) && types[0].kind != TypeKind::Pointer) {
        diagnose(out, "type", "memory address must be a pointer");
    }
    return out;
}

inline void append_diagnostics(Diagnostics& out, Diagnostics more, NodeId node, std::size_t index) {
    for (auto& d : more) { d.node = node; d.instruction = index; out.push_back(std::move(d)); }
}

} // namespace firestone::ir
