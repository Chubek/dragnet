#pragma once
#include "DataDependenceGraph.hpp"

namespace firestone::ir {

class ValueDependenceGraph : public DirectedGraph<ValueId> {
public:
    static ValueDependenceGraph from_ssa(const SimpleSSAGraph& ssa) {
        auto errors = ssa.verify();
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        ValueDependenceGraph out;
        for (ValueId v = 0; v < ssa.value_count(); ++v) out.add_node(v);
        for (auto b : ssa.cfg().blocks()) for (const auto& i : ssa.cfg().block(b).instructions) {
            if (!i.result) continue;
            for (std::size_t p = 0; p < i.operands.size(); ++p) if (i.operands[p].kind == OperandKind::Value)
                out.add_edge(i.operands[p].value, *i.result, EdgeKind::Value, p);
        }
        return out;
    }
};

} // namespace firestone::ir
