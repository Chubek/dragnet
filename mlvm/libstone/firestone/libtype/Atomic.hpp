#pragma once
#include "../libir/Common.hpp"

namespace firestone::type {

using Atomic = ir::Type;
using AtomicKind = ir::TypeKind;

inline bool valid_atomic(const Atomic& type, bool allow_void = false) {
    return type.valid_value() || (allow_void && type == Atomic::void_type());
}

inline std::string atomic_name(const Atomic& type) {
    switch (type.kind) {
    case AtomicKind::Unknown: return "unknown";
    case AtomicKind::Void: return "void";
    case AtomicKind::Integer: return "i" + std::to_string(type.bits);
    case AtomicKind::Float: return "f" + std::to_string(type.bits);
    case AtomicKind::Pointer: return "ptr" + (type.name.empty() ? std::string{} : "<" + type.name + ">");
    case AtomicKind::Memory: return "memory";
    case AtomicKind::Function: return "function";
    }
    throw std::invalid_argument("invalid atomic type kind");
}

} // namespace firestone::type
