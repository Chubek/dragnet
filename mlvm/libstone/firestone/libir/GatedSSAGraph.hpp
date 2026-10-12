#pragma once
#include "SimpleSSAGraph.hpp"

namespace firestone::ir {

struct GatedPhiInput { BlockId predecessor; Operand value; Operand predicate; };

class GatedSSAGraph : public SimpleSSAGraph {
public:
    ValueId gated_phi(BlockId block, Type type, std::vector<GatedPhiInput> inputs) {
        std::vector<PhiInput> values;
        for (const auto& input : inputs) values.push_back({input.predecessor, input.value});
        auto id = phi(block, std::move(type), std::move(values)); gates_[id] = std::move(inputs); return id;
    }
    const std::map<ValueId, std::vector<GatedPhiInput>>& gates() const { return gates_; }
    Diagnostics verify() const {
        auto out = SimpleSSAGraph::verify();
        if (cfg().blocks().empty()) return out;
        auto defs = definitions(); auto dom = cfg().dominators();
        for (const auto& gate : gates_) for (const auto& input : gate.second) {
            const auto& p = input.predicate;
            Type t = p.kind == OperandKind::Value && p.value < value_count() ? type(p.value) : p.type;
            if (t != Type::boolean()) diagnose(out, "gate", "gate predicate must be boolean", gate.first);
            if (p.kind == OperandKind::Float) diagnose(out, "gate", "floating literal cannot be a gate predicate", gate.first);
            if (p.kind == OperandKind::Integer && p.integer != 0 && p.integer != 1) diagnose(out, "gate", "gate literal must be boolean", gate.first);
            if (p.kind == OperandKind::Value) {
                auto d = defs.find(p.value);
                if (d == defs.end()) diagnose(out, "gate", "gate predicate is undefined", gate.first);
                else if (!d->second.parameter && !dom.at(input.predecessor).count(d->second.block))
                    diagnose(out, "gate", "gate predicate does not dominate its incoming edge", gate.first);
            }
        }
        return out;
    }
private:
    std::map<ValueId, std::vector<GatedPhiInput>> gates_;
};

} // namespace firestone::ir
