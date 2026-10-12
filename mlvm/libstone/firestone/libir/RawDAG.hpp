#pragma once
#include "Graph.hpp"

namespace firestone::ir {

template<class Payload = Instruction>
class RawDAG : public DirectedGraph<Payload> {
public:
    bool add_edge(NodeId from, NodeId to, EdgeKind kind = EdgeKind::Data, std::size_t port = 0) {
        // Reject the mutation before it can violate acyclicity.
        this->node(from); this->node(to);
        if (from == to || this->reachable(to).count(from)) throw std::invalid_argument("DAG edge introduces a cycle");
        return DirectedGraph<Payload>::add_edge(from, to, kind, port);
    }
    Diagnostics verify() const {
        try { this->topological_order(); return {}; }
        catch (const std::logic_error&) { return {{"cycle", "DAG contains a cycle", invalid_id, invalid_id}}; }
    }
};

} // namespace firestone::ir
