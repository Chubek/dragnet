#pragma once
#include "ClassHierarchy.hpp"
#include "ObjectGraph.hpp"

namespace firestone::object {

struct DispatchTarget { ClassId receiver_class; std::size_t slot; Method method; };

inline DispatchTarget dispatch(const ClassHierarchy& hierarchy, ClassId dynamic_class, const std::string& method,
                               const std::vector<ir::Type>& arguments) {
    const auto& table = hierarchy.get(dynamic_class).vtable;
    if (!table.contains(method)) throw std::invalid_argument("unknown method: " + method);
    auto slot = table.slot(method);
    const auto& target = table.method(slot);
    if (target.parameters != arguments) throw std::invalid_argument("dynamic call argument signature differs");
    return {dynamic_class, slot, target};
}

inline DispatchTarget dispatch(const ClassHierarchy& hierarchy, const ObjectGraph& objects, ObjectId receiver,
                               const std::string& method, const std::vector<ir::Type>& arguments) {
    return dispatch(hierarchy, hierarchy.lookup(objects.get(receiver).class_name), method, arguments);
}

} // namespace firestone::object
