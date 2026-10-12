#pragma once
#include "Atomic.hpp"

namespace firestone::type {

enum class Conversion { Identity, IntegerWidening, FloatWidening, Explicit, Invalid };

// Width-only classification. Signedness and integer/float precision are caller policy.
inline Conversion classify_conversion(const Atomic& from, const Atomic& to) {
    if (!valid_atomic(from) || !valid_atomic(to)) return Conversion::Invalid;
    if (from == to) return Conversion::Identity;
    if (from.kind == to.kind && from.kind == AtomicKind::Integer && from.bits < to.bits) return Conversion::IntegerWidening;
    if (from.kind == to.kind && from.kind == AtomicKind::Float && from.bits < to.bits) return Conversion::FloatWidening;
    if (from.numeric() && to.numeric()) return Conversion::Explicit;
    if (from.kind == AtomicKind::Pointer && to.kind == AtomicKind::Pointer && from.name == to.name)
        return Conversion::Explicit;
    return Conversion::Invalid;
}

inline bool implicit_conversion(const Atomic& from, const Atomic& to) {
    auto conversion = classify_conversion(from, to);
    return conversion == Conversion::Identity || conversion == Conversion::IntegerWidening || conversion == Conversion::FloatWidening;
}

} // namespace firestone::type
