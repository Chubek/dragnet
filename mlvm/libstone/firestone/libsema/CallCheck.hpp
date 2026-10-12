#pragma once
#include "../libir/ControlFlowGraph.hpp"

namespace firestone::sema {

struct FunctionSignature {
    ir::Type result = ir::Type::void_type();
    std::vector<ir::Type> parameters;
    bool variadic = false;
};
using FunctionSignatures = std::map<std::string, FunctionSignature>;

inline void validate_signature(const FunctionSignature& signature) {
    if (!signature.result.valid_value() && signature.result != ir::Type::void_type()) throw std::invalid_argument("invalid function result type");
    for (const auto& parameter : signature.parameters) if (!parameter.valid_value()) throw std::invalid_argument("invalid function parameter type");
}

inline ir::Diagnostics check_call(const ir::Instruction& instruction, const FunctionSignatures& signatures,
                                  const std::function<ir::Type(ir::ValueId)>& resolve) {
    ir::Diagnostics result;
    if (instruction.opcode != ir::Opcode::Call) return result;
    auto found = signatures.find(instruction.annotation);
    if (found == signatures.end()) { ir::diagnose(result, "call.undefined", "unknown callee: " + instruction.annotation); return result; }
    const auto& signature = found->second;
    try { validate_signature(signature); }
    catch (const std::invalid_argument& error) { ir::diagnose(result, "call.signature", error.what()); return result; }
    if (instruction.type != signature.result) ir::diagnose(result, "call.result", "call result type differs from signature");
    if (instruction.operands.size() < signature.parameters.size() || (!signature.variadic && instruction.operands.size() != signature.parameters.size()))
        ir::diagnose(result, "call.arity", "call argument count differs from signature");
    for (std::size_t p = 0; p < std::min(instruction.operands.size(), signature.parameters.size()); ++p)
        if (ir::operand_type(instruction.operands[p], resolve) != signature.parameters[p]) ir::diagnose(result, "call.type", "call argument type differs from signature");
    return result;
}

inline ir::Diagnostics check_calls(const ir::ControlFlowGraph& cfg, const FunctionSignatures& signatures,
                                   const std::function<ir::Type(ir::ValueId)>& resolve) {
    ir::Diagnostics result;
    for (auto block : cfg.blocks()) {
        const auto& instructions = cfg.block(block).instructions;
        for (std::size_t n = 0; n < instructions.size(); ++n)
            ir::append_diagnostics(result, check_call(instructions[n], signatures, resolve), block, n);
    }
    return result;
}

} // namespace firestone::sema
