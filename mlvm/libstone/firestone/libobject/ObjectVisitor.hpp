#pragma once
#include "ObjectGraph.hpp"

namespace firestone::object {

template<class Visitor>
void visit_reachable(const ObjectGraph& graph, ObjectId root, Visitor visitor) {
    for (auto id : graph.reachable(root)) visitor(id, graph.get(id));
}

struct ObjectClone { ObjectId root; std::map<ObjectId, ObjectId> mapping; };

// Two-phase cloning preserves cycles and shared references, also within one graph.
inline ObjectClone clone_reachable(const ObjectGraph& source, ObjectId root, ObjectGraph& destination) {
    std::map<ObjectId, Object> snapshot;
    for (auto id : source.reachable(root)) snapshot.emplace(id, source.get(id));
    ObjectClone result{ir::invalid_id, {}};
    for (const auto& item : snapshot) result.mapping[item.first] = destination.create(item.second.class_name);
    for (const auto& item : snapshot) for (const auto& field : item.second.fields) {
        auto value = field.second;
        if (auto reference = std::get_if<ObjectRef>(&value)) reference->id = result.mapping.at(reference->id);
        destination.set(result.mapping.at(item.first), field.first, std::move(value));
    }
    result.root = result.mapping.at(root); return result;
}

} // namespace firestone::object
