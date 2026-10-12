#pragma once
#include "Value.hpp"

namespace firestone::vm {

class OperandStack {
public:
    void push(Value value, Index limit = no_index) {
        if (values_.size() >= limit) throw VMError("vm.stack.limit", "operand stack limit exceeded");
        values_.push_back(std::move(value));
    }
    Value pop() {
        if (values_.empty()) throw VMError("vm.stack.underflow", "empty operand stack");
        auto value = std::move(values_.back()); values_.pop_back(); return value;
    }
    const Value& at(Index index) const { return values_.at(index); }
    const Value& top() const {
        if (values_.empty()) throw VMError("vm.stack.underflow", "empty operand stack");
        return values_.back();
    }
    Index size() const { return values_.size(); }
    std::vector<Value> take(Index count) {
        if (count > size()) throw VMError("vm.stack.underflow", "insufficient call operands");
        std::vector<Value> result(values_.end() - static_cast<std::ptrdiff_t>(count), values_.end());
        values_.resize(size() - count); return result;
    }
    const std::vector<Value>& values() const { return values_; }
private:
    std::vector<Value> values_;
};

} // namespace firestone::vm
