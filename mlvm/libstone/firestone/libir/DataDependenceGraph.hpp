#pragma once
#include "SimpleSSAGraph.hpp"

namespace firestone::ir {

struct DependenceNode {
    BlockId block = invalid_id;
    std::size_t instruction = invalid_id;
    std::optional<ValueId> value;
};

class DataDependenceGraph : public DirectedGraph<DependenceNode> {
public:
    static DataDependenceGraph from_ssa(const SimpleSSAGraph& ssa) {
        auto errors = ssa.verify();
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        DataDependenceGraph out;
        std::map<ValueId, NodeId> values;
        std::map<std::pair<BlockId, std::size_t>, NodeId> positions;
        for (const auto& d : ssa.definitions()) if (d.second.parameter)
            values[d.first] = out.add_node({invalid_id, invalid_id, d.first});
        for (auto b : ssa.cfg().blocks()) {
            const auto& ins = ssa.cfg().block(b).instructions;
            for (std::size_t n = 0; n < ins.size(); ++n) {
                auto id = out.add_node({b, n, ins[n].result}); positions[{b, n}] = id;
                if (ins[n].result) values[*ins[n].result] = id;
            }
        }
        for (const auto& item : positions) {
            const auto& i = ssa.cfg().block(item.first.first).instructions[item.first.second];
            for (std::size_t p = 0; p < i.operands.size(); ++p) if (i.operands[p].kind == OperandKind::Value)
                out.add_edge(values.at(i.operands[p].value), item.second, EdgeKind::Data, p);
        }
        return out;
    }
    std::set<NodeId> backward_slice(NodeId root) const { return reachable(root, true); }
    std::set<NodeId> forward_slice(NodeId root) const { return reachable(root); }
};

} // namespace firestone::ir
