#pragma once
#include "Continuation.hpp"
#include "../libir/SimpleSSAGraph.hpp"

namespace firestone::cps {

struct LoweredCPS {
    ir::SimpleSSAGraph ssa;
    std::map<ir::NodeId, ir::BlockId> blocks;
    std::map<ir::ValueId, ir::ValueId> values;
};

inline LoweredCPS lower_to_ssa(const CPSGraph& graph) {
    auto errors = graph.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    if (!graph.graph().predecessors(graph.entry()).empty()) throw std::invalid_argument("CPS entry must have no incoming transfers");
    LoweredCPS result;
    for (auto id : graph.graph().nodes()) result.blocks[id] = result.ssa.cfg().create_block(graph.continuation(id).name);
    result.ssa.cfg().set_entry(result.blocks.at(graph.entry()));
    for (auto id : graph.graph().nodes()) {
        const auto& continuation = graph.continuation(id);
        if (id != graph.entry() && !continuation.parameters.empty() && graph.graph().predecessors(id).empty())
            throw std::invalid_argument("nonentry continuation parameters require an incoming transfer");
        for (auto parameter : continuation.parameters)
            result.values[parameter] = id == graph.entry() ? result.ssa.parameter(graph.type(parameter)) : result.ssa.reserve_value(graph.type(parameter));
        for (const auto& binding : continuation.bindings) if (binding.result)
            result.values[*binding.result] = result.ssa.reserve_value(graph.type(*binding.result));
    }
    auto translate = [&](ir::Operand operand) {
        if (operand.kind == ir::OperandKind::Value) operand.value = result.values.at(operand.value);
        return operand;
    };
    std::map<std::pair<ir::NodeId, std::size_t>, ir::BlockId> edges;
    std::map<ir::ValueId, std::vector<ir::PhiInput>> incoming;
    // Splitting each tail edge preserves distinct arguments on same-target branch arms.
    for (auto id : graph.graph().nodes()) {
        const auto& tail = *graph.continuation(id).tail;
        for (std::size_t edge = 0; edge < tail.targets.size(); ++edge) {
            auto block = result.ssa.cfg().create_block("$cps.edge." + std::to_string(id) + "." + std::to_string(edge));
            edges[{id, edge}] = block;
            result.ssa.cfg().jump(block, result.blocks.at(tail.targets[edge]));
            const auto& parameters = graph.continuation(tail.targets[edge]).parameters;
            for (std::size_t p = 0; p < parameters.size(); ++p) incoming[parameters[p]].push_back({block, translate(tail.arguments[edge][p])});
        }
    }
    for (auto id : graph.graph().nodes()) {
        auto block = result.blocks.at(id);
        const auto& c = graph.continuation(id);
        if (id != graph.entry()) for (auto parameter : c.parameters)
            result.ssa.phi_reserved(block, result.values.at(parameter), incoming.at(parameter));
        for (const auto& binding : c.bindings) {
            std::vector<ir::Operand> operands;
            for (const auto& operand : binding.operands) operands.push_back(translate(operand));
            if (binding.result) result.ssa.define_reserved(block, result.values.at(*binding.result), binding.opcode, std::move(operands), binding.annotation);
            else result.ssa.cfg().append(block, {binding.opcode, {}, binding.type, std::move(operands), {}, {}, binding.annotation});
        }
        if (c.tail->kind == TailKind::Halt) result.ssa.cfg().return_value(block, c.tail->value ? std::optional<ir::Operand>{translate(*c.tail->value)} : std::nullopt);
        else if (c.tail->kind == TailKind::Jump) result.ssa.cfg().jump(block, edges.at({id, 0}));
        else result.ssa.cfg().branch(block, translate(*c.tail->value), edges.at({id, 0}), edges.at({id, 1}));
    }
    errors = result.ssa.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    return result;
}

} // namespace firestone::cps
