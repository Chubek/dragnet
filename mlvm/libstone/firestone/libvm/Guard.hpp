#pragma once
#include "Dispatch.hpp"

namespace firestone::vm {

enum class SlotKind { Register, Local, Operand };
struct SlotLocation { SlotKind kind = SlotKind::Register; Index index = 0; };
inline std::optional<Value> snapshot_value(const CallFrame& frame, SlotLocation location) {
    switch (location.kind) {
    case SlotKind::Register: return location.index < frame.registers.size() ? frame.registers[location.index] : std::optional<Value>{};
    case SlotKind::Local: return location.index < frame.locals.size() ? frame.locals[location.index] : std::optional<Value>{};
    case SlotKind::Operand: return location.index < frame.operands.size() ? std::optional<Value>{frame.operands.at(location.index)} : std::optional<Value>{};
    }
    return {};
}
struct Guard {
    SlotLocation location;
    ValueKind expected_kind = ValueKind::Integer;
    std::optional<Value> expected_value;
    bool accepts(const CallFrame& frame) const {
        auto value = snapshot_value(frame, location);
        return value && kind(*value) == expected_kind && (!expected_value || *value == *expected_value);
    }
};
inline bool check_guards(const ExecutionState& state, const std::vector<Guard>& guards) {
    if (!state.frames.size() || state.complete()) return false;
    return std::all_of(guards.begin(), guards.end(), [&](const Guard& guard) { return guard.accepts(state.frames.top()); });
}

} // namespace firestone::vm
