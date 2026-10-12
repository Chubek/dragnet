#pragma once
#include "CallStack.hpp"
#include "Environment.hpp"

namespace firestone::vm {

struct ExecutionLimits { Index instructions = 1000000, call_depth = 1024, operand_depth = 65536; };
struct ExecutionState {
    CallStack frames;
    std::optional<Value> result;
    Index instructions = 0;
    bool complete() const { return result.has_value(); }
};
enum class ExecutionStop { Returned, Budget };
struct ExecutionResult { ExecutionStop stop; ExecutionState state; };
using Observer = std::function<void(CodeAddress, const Instruction&)>;

inline Value read_slot(const std::vector<std::optional<Value>>& slots, Index index) {
    if (index >= slots.size() || !slots[index]) throw VMError("vm.uninitialized", "unavailable runtime slot");
    return *slots[index];
}
inline void write_result(CallFrame& frame, const Function& function, Index destination, Value value, const ExecutionLimits& limits) {
    if (function.model == VMModel::Stack) frame.operands.push(std::move(value), limits.operand_depth);
    else {
        if (destination >= frame.registers.size()) throw VMError("vm.register", "invalid runtime destination");
        frame.registers[destination] = std::move(value);
    }
}

// One semantic implementation serves both execution models.
inline void step(const Module& module, Environment& environment, ExecutionState& state,
                 const ExecutionLimits& limits = {}, const Observer& observer = {}) {
    if (state.complete() || !state.frames.size()) throw VMError("vm.state", "no active execution frame");
    auto& frame = state.frames.top();
    const auto& fn = module.function(frame.function);
    CodeAddress position{frame.function, frame.pc};
    if (frame.pc >= fn.code.size()) throw VMError("vm.pc", "invalid program counter", position);
    const auto& i = fn.code.at(frame.pc);
    try {
        if (observer) observer(position, i);
        // Validate resource limits before consuming operands or entering a call.
        if (i.opcode == Opcode::Call && state.frames.size() >= limits.call_depth)
            throw VMError("vm.call.limit", "call stack limit exceeded");
        if (frame.operands.size() > limits.operand_depth) throw VMError("vm.stack.limit", "operand stack limit exceeded");
        std::vector<Value> values;
        if (fn.model == VMModel::Stack) values = frame.operands.take(input_count(i));
        else for (auto operand : i.operands) values.push_back(read_slot(frame.registers, operand));
        auto emit = [&](Value value) { write_result(frame, fn, i.destination, std::move(value), limits); };
        Index next = frame.pc + 1;
        if (arithmetic(i.opcode)) emit(scalar(i.opcode, values, i.bits));
        else switch (i.opcode) {
        case Opcode::Nop: break;
        case Opcode::Constant: emit(module.constants.at(i.index)); break;
        case Opcode::Move: emit(values.at(0)); break;
        case Opcode::LocalGet: emit(read_slot(frame.locals, i.index)); break;
        case Opcode::LocalSet: frame.locals.at(i.index) = values.at(0); break;
        case Opcode::Pop: break;
        case Opcode::Dup: emit(values.at(0)); emit(values.at(0)); break;
        case Opcode::Swap: emit(values.at(1)); emit(values.at(0)); break;
        case Opcode::Select: emit(require<bool>(values.at(0)) ? values.at(1) : values.at(2)); break;
        case Opcode::Load: emit(environment.load(require<Address>(values.at(0)))); break;
        case Opcode::Store: environment.store(require<Address>(values.at(0)), values.at(1)); break;
        case Opcode::GetGlobal: emit(environment.global(i.symbol)); break;
        case Opcode::SetGlobal: environment.set_global(i.symbol, values.at(0)); break;
        case Opcode::GetField: emit(environment.field(require<ObjectRef>(values.at(0)), i.symbol)); break;
        case Opcode::SetField: environment.set_field(require<ObjectRef>(values.at(0)), i.symbol, values.at(1)); break;
        case Opcode::Jump: {
            next = i.target;
            if (next >= fn.code.size()) throw VMError("vm.pc", "jump target is out of range");
            break;
        }
        case Opcode::Branch: {
            next = require<bool>(values.at(0)) ? i.target : i.alternate;
            if (next >= fn.code.size()) throw VMError("vm.pc", "branch target is out of range");
            break;
        }
        case Opcode::NativeCall: {
            const auto& signature = callee_signature(module, i);
            auto result = environment.call(i.symbol, signature, values);
            if (signature.result != ValueKind::Null) emit(std::move(result));
            break;
        }
        case Opcode::Call: {
            auto callee = make_frame(module, i.index, values, i.destination);
            frame.pc = next;
            state.frames.push(std::move(callee), limits.call_depth);
            ++state.instructions; return;
        }
        case Opcode::Return: {
            Value result = values.empty() ? Value{} : values[0]; check_result(fn.signature, result);
            auto finished = state.frames.pop();
            if (!state.frames.size()) state.result = std::move(result);
            else if (fn.signature.result != ValueKind::Null) {
                auto& caller = state.frames.top();
                write_result(caller, module.function(caller.function), finished.return_destination, std::move(result), limits);
            }
            ++state.instructions; return;
        }
        default: throw VMError("vm.opcode", "unsupported opcode");
        }
        frame.pc = next; ++state.instructions;
    } catch (VMError& error) { error.position = position; throw; }
      catch (std::exception& error) { throw VMError("vm.observer", std::string("host observer failed: ") + error.what(), position); }
      catch (...) { throw VMError("vm.observer", "host observer failed", position); }
}

inline void validate_state(const Module& module, const ExecutionState& state, const ExecutionLimits& limits = {}) {
    if (state.complete()) {
        if (state.frames.size()) throw VMError("vm.state", "completed state has active frames");
        return;
    }
    if (!state.frames.size() || state.frames.size() > limits.call_depth) throw VMError("vm.state", "invalid snapshot call depth");
    for (Index n = 0; n < state.frames.size(); ++n) {
        const auto& frame = state.frames.at(n);
        if (frame.function >= module.size()) throw VMError("vm.state", "unknown snapshot function");
        const auto& fn = module.function(frame.function);
        if (frame.pc >= fn.code.size() || frame.registers.size() != fn.register_count || frame.locals.size() != fn.local_count || frame.operands.size() > limits.operand_depth)
            throw VMError("vm.state", "invalid snapshot frame shape");
        if (fn.model == VMModel::Register && frame.operands.size()) throw VMError("vm.state", "register frame has stack operands");
        if (n && fn.signature.result != ValueKind::Null) {
            const auto& caller = state.frames.at(n - 1);
            const auto& caller_fn = module.function(caller.function);
            if (caller_fn.model == VMModel::Register && frame.return_destination >= caller.registers.size())
                throw VMError("vm.state", "invalid snapshot return destination");
            if (caller_fn.model == VMModel::Stack && frame.return_destination != no_index)
                throw VMError("vm.state", "stack caller must not carry a return destination");
        } else if (n && frame.return_destination != no_index) {
            throw VMError("vm.state", "null-result activation must not carry a return destination");
        }
    }
}
inline ExecutionResult resume(const Module& module, Environment& environment, ExecutionState state,
                              const ExecutionLimits& limits = {}, const Observer& observer = {}) {
    require_valid(module); validate_state(module, state, limits);
    Index executed = 0;
    while (!state.complete() && executed < limits.instructions) {
        step(module, environment, state, limits, observer); ++executed;
    }
    auto stop = state.complete() ? ExecutionStop::Returned : ExecutionStop::Budget;
    return {stop, std::move(state)};
}
inline ExecutionResult execute(const Module& module, Index function, const std::vector<Value>& arguments,
                               Environment& environment, const ExecutionLimits& limits = {}, const Observer& observer = {}) {
    require_valid(module);
    ExecutionState state; state.frames.push(make_frame(module, function, arguments), limits.call_depth);
    return resume(module, environment, std::move(state), limits, observer);
}

} // namespace firestone::vm
