#pragma once
#include "../libir/Graph.hpp"
#include <variant>

namespace firestone::object {

using ObjectId = ir::NodeId;
struct ObjectRef {
    ObjectId id = ir::invalid_id;
    friend bool operator==(ObjectRef a, ObjectRef b) { return a.id == b.id; }
};
using FieldValue = std::variant<std::monostate, bool, std::int64_t, double, std::string, ObjectRef>;
struct Object {
    std::string class_name;
    std::map<std::string, FieldValue> fields;
};

class ObjectGraph {
public:
    ObjectId create(std::string class_name) {
        if (class_name.empty()) throw std::invalid_argument("object class name is empty");
        return graph_.add_node({std::move(class_name), {}});
    }
    const Object& get(ObjectId id) const { return graph_.node(id); }
    bool contains(ObjectId id) const { return graph_.contains(id); }
    const FieldValue& field(ObjectId id, const std::string& name) const { return get(id).fields.at(name); }
    void set(ObjectId id, std::string name, FieldValue value) {
        graph_.node(id);
        if (name.empty()) throw std::invalid_argument("object field name is empty");
        if (auto reference = std::get_if<ObjectRef>(&value)) graph_.node(reference->id);
        graph_.node(id).fields[std::move(name)] = std::move(value); rebuild_edges(id);
    }
    void erase_field(ObjectId id, const std::string& name) {
        auto& fields = graph_.node(id).fields;
        if (!fields.erase(name)) throw std::invalid_argument("object field does not exist");
        rebuild_edges(id);
    }
    void remove(ObjectId id) {
        graph_.node(id);
        for (auto predecessor : graph_.predecessors(id)) if (predecessor != id)
            throw std::logic_error("cannot remove a referenced object");
        graph_.remove_node(id);
    }
    std::set<ObjectId> reachable(ObjectId root) const { return graph_.reachable(root, false, ir::EdgeKind::Memory); }
    const ir::DirectedGraph<Object>& graph() const { return graph_; }
private:
    ir::DirectedGraph<Object> graph_;
    void rebuild_edges(ObjectId id) {
        auto edges = graph_.edges();
        for (const auto& edge : edges) if (edge.from == id) graph_.remove_edge(edge);
        std::size_t port = 0;
        for (const auto& field : get(id).fields) {
            if (auto reference = std::get_if<ObjectRef>(&field.second)) graph_.add_edge(id, reference->id, ir::EdgeKind::Memory, port);
            ++port;
        }
    }
};

} // namespace firestone::object
