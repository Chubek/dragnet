#pragma once
#include "../libir/DataflowAnalysis.hpp"

namespace firestone::sema {

struct DefiniteAssignment {
    ir::DataflowResult<std::set<ir::ValueId>> states;
    ir::Diagnostics diagnostics;
};

inline DefiniteAssignment check_definite_assignment(const ir::ControlFlowGraph& cfg, std::set<ir::ValueId> parameters = {}) {
    std::set<ir::ValueId> universe = parameters;
    for (auto block : cfg.blocks()) for (const auto& instruction : cfg.block(block).instructions) {
        if (instruction.result) universe.insert(*instruction.result);
        for (const auto& operand : instruction.operands) if (operand.kind == ir::OperandKind::Value) universe.insert(operand.value);
    }
    auto meet = [&](const std::vector<std::set<ir::ValueId>>& incoming) {
        auto result = universe;
        for (const auto& state : incoming) {
            std::set<ir::ValueId> common;
            std::set_intersection(result.begin(), result.end(), state.begin(), state.end(), std::inserter(common, common.end())); result = std::move(common);
        }
        return result;
    };
    auto transfer = [&](ir::BlockId block, std::set<ir::ValueId> state) {
        for (const auto& instruction : cfg.block(block).instructions) if (instruction.result) state.insert(*instruction.result);
        return state;
    };
    DefiniteAssignment result;
    result.states = ir::solve_dataflow(cfg, universe, meet, transfer, {{cfg.entry(), parameters}});
    if (!result.states.converged) { ir::diagnose(result.diagnostics, "assignment.limit", "definite assignment did not converge"); return result; }
    for (const auto& item : result.states.in) {
        auto block = item.first;
        auto available = item.second;
        const auto& instructions = cfg.block(block).instructions;
        // Phi results are simultaneously available on entry; operands live on edges.
        for (const auto& instruction : instructions) if (instruction.opcode == ir::Opcode::Phi && instruction.result) available.insert(*instruction.result);
        for (std::size_t n = 0; n < instructions.size(); ++n) {
            const auto& instruction = instructions[n];
            if (instruction.opcode == ir::Opcode::Phi) {
                auto predecessors = cfg.predecessors(block);
                std::set<ir::BlockId> labels(instruction.incoming_blocks.begin(), instruction.incoming_blocks.end());
                if (labels != std::set<ir::BlockId>(predecessors.begin(), predecessors.end()) || labels.size() != instruction.incoming_blocks.size())
                    ir::diagnose(result.diagnostics, "assignment.phi", "phi must label each predecessor once", block, n);
            }
            for (std::size_t p = 0; p < instruction.operands.size(); ++p) if (instruction.operands[p].kind == ir::OperandKind::Value) {
                bool assigned = available.count(instruction.operands[p].value) != 0;
                if (instruction.opcode == ir::Opcode::Phi) {
                    // Unreachable incoming edges contribute no executable path.
                    if (p < instruction.incoming_blocks.size() && cfg.graph().contains(instruction.incoming_blocks[p]) &&
                        !result.states.out.count(instruction.incoming_blocks[p])) continue;
                    assigned = p < instruction.incoming_blocks.size() && result.states.out.count(instruction.incoming_blocks[p]) &&
                               result.states.out.at(instruction.incoming_blocks[p]).count(instruction.operands[p].value);
                }
                if (!assigned) ir::diagnose(result.diagnostics, "assignment.undefined", "value is not definitely assigned at use", block, n);
            }
            if (instruction.result) available.insert(*instruction.result);
        }
    }
    return result;
}

} // namespace firestone::sema
