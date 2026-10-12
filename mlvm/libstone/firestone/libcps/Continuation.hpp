#pragma once
#include "../libir/CPSGraph.hpp"

namespace firestone::cps {

using ir::CPSGraph;
using ir::Continuation;
using ir::CPSTail;
using ir::TailKind;

struct Signature { std::vector<ir::Type> parameters; };
inline Signature signature(const CPSGraph& graph, ir::NodeId continuation) {
    Signature result;
    for (auto parameter : graph.continuation(continuation).parameters) result.parameters.push_back(graph.type(parameter));
    return result;
}

inline ir::Diagnostics check_local_operand(const CPSGraph& graph, ir::NodeId source, const ir::Operand& operand) {
    const auto& continuation = graph.continuation(source);
    std::set<ir::ValueId> available(continuation.parameters.begin(), continuation.parameters.end());
    for (const auto& binding : continuation.bindings) if (binding.result) available.insert(*binding.result);
    ir::Diagnostics out;
    auto resolve = [&](ir::ValueId value) { return value < graph.value_count() ? graph.type(value) : ir::Type{}; };
    if (operand.kind == ir::OperandKind::Value && !available.count(operand.value))
        ir::diagnose(out, "scope", "operand is outside continuation scope", source);
    auto type = ir::operand_type(operand, resolve);
    ir::Instruction copy{ir::Opcode::Copy, ir::ValueId{0}, type, {operand}, {}, {}, {}};
    ir::append_diagnostics(out, ir::verify_instruction(copy, resolve), source, ir::invalid_id);
    return out;
}

inline ir::Diagnostics check_transfer(const CPSGraph& graph, ir::NodeId source, ir::NodeId target,
                                      const std::vector<ir::Operand>& arguments) {
    graph.continuation(source);
    auto expected = signature(graph, target);
    ir::Diagnostics out;
    if (arguments.size() != expected.parameters.size()) ir::diagnose(out, "arity", "continuation argument count differs", source);
    auto resolve = [&](ir::ValueId value) { return value < graph.value_count() ? graph.type(value) : ir::Type{}; };
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        ir::append_diagnostics(out, check_local_operand(graph, source, arguments[i]), source, i);
        auto type = ir::operand_type(arguments[i], resolve);
        if (i < expected.parameters.size() && type != expected.parameters[i]) ir::diagnose(out, "type", "transfer argument type differs", source, i);
    }
    return out;
}

} // namespace firestone::cps
