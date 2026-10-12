#pragma once
#include "Imperative.hpp"
#include "../libmemory/Alloc.hpp"

namespace firestone::type {

struct Layout {
    std::size_t size = 0, alignment = 1;
    std::vector<std::size_t> offsets;
};
struct LayoutPolicy {
    std::size_t pointer_size = 8, pointer_alignment = 8, maximum_scalar_alignment = 8;
};

inline Layout layout(const Type& type, LayoutPolicy policy = {}) {
    validate(type);
    if (!policy.pointer_size || !memory::valid_alignment(policy.pointer_alignment) ||
        !memory::valid_alignment(policy.maximum_scalar_alignment)) throw std::invalid_argument("invalid layout policy");
    auto scalar = [&](std::size_t bytes) {
        std::size_t alignment = 1;
        while (alignment < bytes && alignment < policy.maximum_scalar_alignment) alignment *= 2;
        return Layout{memory::align_up(bytes, alignment), alignment, {}};
    };
    if (type.kind == Kind::Pointer || type.kind == Kind::Function)
        return {memory::align_up(policy.pointer_size, policy.pointer_alignment), policy.pointer_alignment, {}};
    if (type.kind == Kind::Atomic) {
        if (type.scalar.numeric()) return scalar(type.scalar.bits / 8 + (type.scalar.bits % 8 != 0));
        if (type.scalar.kind == AtomicKind::Pointer || type.scalar.kind == AtomicKind::Function)
            return {memory::align_up(policy.pointer_size, policy.pointer_alignment), policy.pointer_alignment, {}};
        throw std::invalid_argument("atomic type has no storage layout");
    }
    if (type.kind == Kind::Variable) throw std::invalid_argument("unresolved type has no storage layout");
    if (type.kind == Kind::Array) {
        auto element = layout(type.parts[0], policy);
        if (element.size && type.count > std::numeric_limits<std::size_t>::max() / element.size)
            throw std::overflow_error("array layout overflows");
        return {element.size * type.count, element.alignment, {}};
    }
    Layout result;
    for (const auto& field : type.parts) {
        auto field_layout = layout(field, policy);
        auto offset = memory::align_up(result.size, field_layout.alignment);
        if (field_layout.size > std::numeric_limits<std::size_t>::max() - offset) throw std::overflow_error("record layout overflows");
        result.offsets.push_back(offset); result.size = offset + field_layout.size;
        result.alignment = std::max(result.alignment, field_layout.alignment);
    }
    result.size = memory::align_up(result.size, result.alignment);
    if (type.kind == Kind::Record && result.size == 0) result.size = 1;
    return result;
}

} // namespace firestone::type
