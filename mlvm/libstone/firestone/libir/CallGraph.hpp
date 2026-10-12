#pragma once
#include "Graph.hpp"

namespace firestone::ir {

struct Function { std::string name; bool external = false; };

class CallGraph {
public:
    NodeId function(std::string name, bool external = false) {
        if (name.empty()) throw std::invalid_argument("function name is empty");
        auto found = names_.find(name);
        if (found != names_.end()) {
            if (graph_.node(found->second).external != external) throw std::invalid_argument("conflicting function declaration");
            return found->second;
        }
        auto id = graph_.add_node({name, external}); names_.emplace(std::move(name), id); return id;
    }
    void call(NodeId caller, NodeId callee, std::size_t site = 0) {
        if (!graph_.contains(caller)) throw std::invalid_argument("unknown call graph caller");
        if (!graph_.contains(callee)) throw std::invalid_argument("unknown call graph callee");
        graph_.add_edge(caller, callee, EdgeKind::Call, site);
    }
    const DirectedGraph<Function>& graph() const { return graph_; }
    std::optional<NodeId> find(const std::string& name) const {
        auto it = names_.find(name); return it == names_.end() ? std::optional<NodeId>{} : it->second;
    }
    std::set<NodeId> recursive_functions() const {
        std::set<NodeId> out;
        for (const auto& component : graph_.strongly_connected_components()) {
            if (component.size() > 1) out.insert(component.begin(), component.end());
            else { auto n = component.front(); if (has_self_call(n)) out.insert(n); }
        }
        return out;
    }
private:
    DirectedGraph<Function> graph_;
    std::map<std::string, NodeId> names_;
    bool has_self_call(NodeId n) const { for (const auto& e : graph_.edges()) if (e.from == n && e.to == n) return true; return false; }
};

} // namespace firestone::ir
