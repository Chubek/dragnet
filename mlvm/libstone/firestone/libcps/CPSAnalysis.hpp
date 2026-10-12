#pragma once
#include "Continuation.hpp"

namespace firestone::cps {

struct CPSLiveness {
    std::map<ir::NodeId, std::set<ir::ValueId>> required_parameters;
    std::map<ir::NodeId, std::set<std::size_t>> dead_bindings;
};

// Demand propagation through tail arguments, including recursive continuations.
inline CPSLiveness analyze_liveness(const CPSGraph& graph) {
    auto errors = graph.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    CPSLiveness result;
    for (auto id : graph.graph().nodes()) result.required_parameters[id];
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto id : graph.graph().nodes()) {
            const auto& c = graph.continuation(id);
            std::set<ir::ValueId> live;
            auto use = [&](const ir::Operand& operand) { if (operand.kind == ir::OperandKind::Value) live.insert(operand.value); };
            if (c.tail->value) use(*c.tail->value);
            for (std::size_t edge = 0; edge < c.tail->targets.size(); ++edge) {
                const auto& target = graph.continuation(c.tail->targets[edge]);
                for (std::size_t p = 0; p < target.parameters.size(); ++p)
                    if (result.required_parameters.at(c.tail->targets[edge]).count(target.parameters[p])) use(c.tail->arguments[edge][p]);
            }
            std::set<std::size_t> dead;
            for (std::size_t n = c.bindings.size(); n > 0; --n) {
                const auto& binding = c.bindings[n - 1];
                if (ir::is_effectful(binding.opcode) || (binding.result && live.count(*binding.result))) {
                    if (binding.result) live.erase(*binding.result);
                    for (const auto& operand : binding.operands) use(operand);
                } else dead.insert(n - 1);
            }
            std::set<ir::ValueId> parameters;
            for (auto parameter : c.parameters) if (live.count(parameter)) parameters.insert(parameter);
            if (parameters != result.required_parameters.at(id)) { result.required_parameters[id] = std::move(parameters); changed = true; }
            result.dead_bindings[id] = std::move(dead);
        }
    }
    return result;
}

} // namespace firestone::cps
