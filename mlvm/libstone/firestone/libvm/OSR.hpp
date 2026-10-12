#pragma once
#include "Guard.hpp"

namespace firestone::vm {

struct SlotTransfer { SlotLocation source, destination; };
struct OSRPlan {
    CodeAddress target;
    std::vector<SlotTransfer> transfers;
    std::vector<SlotLocation> operands;
};
// All reads use the old activation; transfers therefore have parallel-copy semantics.
inline ExecutionState transfer_osr(const Module& module, const ExecutionState& state, const OSRPlan& plan,
                                   const ExecutionLimits& limits = {}) {
    require_valid(module); validate_state(module, state, limits);
    if (state.complete()) throw VMError("vm.osr", "cannot transfer a completed state");
    if (plan.target.function >= module.size()) throw VMError("vm.osr", "unknown OSR target function");
    const auto& old = state.frames.top(); const auto& fn = module.function(plan.target.function);
    if (fn.signature.result != module.function(old.function).signature.result) throw VMError("vm.osr", "OSR result kind mismatch");
    CallFrame frame; frame.function = plan.target.function; frame.pc = plan.target.instruction;
    frame.return_destination = old.return_destination;
    frame.registers.resize(fn.register_count); frame.locals.resize(fn.local_count);
    std::set<std::pair<SlotKind, Index>> destinations;
    for (const auto& move : plan.transfers) {
        auto value = snapshot_value(old, move.source);
        if (!value) throw VMError("vm.osr", "unavailable OSR source");
        if (!destinations.emplace(move.destination.kind, move.destination.index).second) throw VMError("vm.osr", "duplicate OSR destination");
        if (move.destination.kind == SlotKind::Register && move.destination.index < frame.registers.size()) frame.registers[move.destination.index] = *value;
        else if (move.destination.kind == SlotKind::Local && move.destination.index < frame.locals.size()) frame.locals[move.destination.index] = *value;
        else throw VMError("vm.osr", "invalid OSR destination");
    }
    for (auto location : plan.operands) {
        auto value = snapshot_value(old, location);
        if (!value) throw VMError("vm.osr", "unavailable OSR operand");
        frame.operands.push(*value, limits.operand_depth);
    }
    ExecutionState result = state; result.frames.top() = std::move(frame); validate_state(module, result, limits); return result;
}

} // namespace firestone::vm
