#pragma once
#include "DataDependenceGraph.hpp"

namespace firestone::ir {

class ProgramDependenceGraph : public DataDependenceGraph {
public:
    static ProgramDependenceGraph from_ssa(const SimpleSSAGraph& ssa) {
        ProgramDependenceGraph out;
        static_cast<DataDependenceGraph&>(out) = DataDependenceGraph::from_ssa(ssa);
        std::map<BlockId, NodeId> terminators;
        for (auto id : out.nodes()) {
            const auto& node = out.node(id);
            if (node.block != invalid_id && is_terminator(ssa.cfg().block(node.block).instructions[node.instruction].opcode))
                terminators[node.block] = id;
        }
        for (auto dep : ssa.cfg().control_dependencies()) {
            auto found = terminators.find(dep.first);
            if (found == terminators.end()) continue;
            for (auto id : out.nodes())
                if (out.node(id).block == dep.second) out.add_edge(found->second, id, EdgeKind::Control);
        }
        return out;
    }
};

} // namespace firestone::ir
