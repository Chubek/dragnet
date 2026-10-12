#pragma once
#include "../libir/ControlFlowGraph.hpp"

namespace firestone::sema {

struct BlockPolicy { bool require_reachable = false, require_entry_without_predecessors = false; };

inline ir::Diagnostics check_blocks(const ir::ControlFlowGraph& cfg, BlockPolicy policy = {}) {
    auto result = cfg.verify();
    if (cfg.blocks().empty()) return result;
    if (policy.require_reachable) for (auto block : cfg.unreachable_blocks()) ir::diagnose(result, "block.unreachable", "block is unreachable from entry", block);
    if (policy.require_entry_without_predecessors && !cfg.predecessors(cfg.entry()).empty())
        ir::diagnose(result, "block.entry", "entry block has predecessors", cfg.entry());
    return result;
}

inline ir::Diagnostics check_returns(const ir::ControlFlowGraph& cfg, const ir::Type& expected,
                                     const std::function<ir::Type(ir::ValueId)>& resolve) {
    if (!expected.valid_value() && expected != ir::Type::void_type()) throw std::invalid_argument("invalid return signature");
    ir::Diagnostics result;
    for (auto block : cfg.blocks()) {
        const auto& instructions = cfg.block(block).instructions;
        for (std::size_t n = 0; n < instructions.size(); ++n) if (instructions[n].opcode == ir::Opcode::Return) {
            const auto& operands = instructions[n].operands;
            if (expected == ir::Type::void_type() ? !operands.empty() : operands.size() != 1)
                ir::diagnose(result, "return.arity", "return value presence differs from signature", block, n);
            else if (!operands.empty() && ir::operand_type(operands[0], resolve) != expected)
                ir::diagnose(result, "return.type", "return type differs from signature", block, n);
        }
    }
    return result;
}

} // namespace firestone::sema
