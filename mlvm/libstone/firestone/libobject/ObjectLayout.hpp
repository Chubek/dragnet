#pragma once
#include "ClassHierarchy.hpp"
#include "../libtype/TypeLayout.hpp"

namespace firestone::object {

struct ObjectLayout {
    std::size_t size = 0, alignment = 1;
    std::map<std::string, std::size_t> offsets;
    std::optional<std::size_t> vptr_offset;
};

// Single inheritance, a leading vptr for polymorphic objects, then inherited fields.
inline ObjectLayout object_layout(const ClassHierarchy& hierarchy, ClassId id, type::LayoutPolicy policy = {}) {
    auto fields = hierarchy.fields(id);
    bool polymorphic = hierarchy.get(id).vtable.size() != 0;
    if (polymorphic) fields.insert(fields.begin(), {"$vptr", type::Type::pointer(type::Type::atomic(ir::Type::void_type()))});
    auto storage = type::layout(type::Type::record(fields), policy);
    ObjectLayout result{storage.size, storage.alignment, {}, {}};
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].first == "$vptr") result.vptr_offset = storage.offsets[i];
        else result.offsets[fields[i].first] = storage.offsets[i];
    }
    return result;
}

} // namespace firestone::object
