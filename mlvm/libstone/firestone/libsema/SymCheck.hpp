#pragma once
#include "SymbolTable.hpp"
#include "../libir/ControlFlowGraph.hpp"

namespace firestone::sema {

inline ir::Diagnostics check_symbols(const ir::Instruction& instruction, const SymbolTable& symbols, ScopeId scope = 0) {
    symbols.visible(scope); // Validate even an operand-free instruction's scope.
    ir::Diagnostics result;
    for (const auto& operand : instruction.operands) if (operand.kind == ir::OperandKind::Symbol) {
        auto symbol = symbols.lookup(scope, operand.symbol);
        if (!symbol) ir::diagnose(result, "symbol.undefined", "unknown symbol: " + operand.symbol);
        else if (operand.type != symbol->type) ir::diagnose(result, "symbol.type", "symbol type differs from declaration: " + operand.symbol);
    }
    return result;
}

inline ir::Diagnostics check_symbols(const ir::ControlFlowGraph& cfg, const SymbolTable& symbols,
                                     const std::map<ir::BlockId, ScopeId>& scopes = {}) {
    ir::Diagnostics result;
    for (auto block : cfg.blocks()) {
        const auto& instructions = cfg.block(block).instructions;
        for (std::size_t n = 0; n < instructions.size(); ++n)
            ir::append_diagnostics(result, check_symbols(instructions[n], symbols, scopes.count(block) ? scopes.at(block) : symbols.root()), block, n);
    }
    return result;
}

} // namespace firestone::sema
