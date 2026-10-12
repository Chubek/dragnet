#pragma once
#include "OperandStack.hpp"
#include "Module.hpp"

namespace firestone::vm {

struct CallFrame {
    Index function = no_index, pc = 0;
    std::vector<std::optional<Value>> registers, locals;
    OperandStack operands;
    Index return_destination = no_index;
};
class CallStack {
public:
    void push(CallFrame frame, Index limit = 1024) {
        if (frames_.size() >= limit) throw VMError("vm.call.limit", "call stack limit exceeded");
        frames_.push_back(std::move(frame));
    }
    CallFrame pop() {
        if (frames_.empty()) throw VMError("vm.call.underflow", "empty call stack");
        auto result = std::move(frames_.back()); frames_.pop_back(); return result;
    }
    CallFrame& top() { if (frames_.empty()) throw VMError("vm.call.underflow", "empty call stack"); return frames_.back(); }
    const CallFrame& top() const { if (frames_.empty()) throw VMError("vm.call.underflow", "empty call stack"); return frames_.back(); }
    CallFrame& at(Index index) { return frames_.at(index); }
    const CallFrame& at(Index index) const { return frames_.at(index); }
    Index size() const { return frames_.size(); }
private:
    std::vector<CallFrame> frames_;
};
inline CallFrame make_frame(const Module& module, Index function, const std::vector<Value>& arguments, Index destination = no_index) {
    const auto& fn = module.function(function); check_arguments(fn.signature, arguments);
    CallFrame frame; frame.function = function; frame.return_destination = destination;
    frame.registers.resize(fn.register_count); frame.locals.resize(fn.local_count);
    auto& slots = fn.model == VMModel::Register ? frame.registers : frame.locals;
    for (Index i = 0; i < arguments.size(); ++i) slots.at(i) = arguments[i];
    return frame;
}

} // namespace firestone::vm
