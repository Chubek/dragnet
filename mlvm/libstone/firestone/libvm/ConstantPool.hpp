#pragma once
#include "Value.hpp"

namespace firestone::vm {

class ConstantPool {
public:
    Index add(Value value) {
        if (kind(value) == ValueKind::Address || kind(value) == ValueKind::Object)
            throw std::invalid_argument("heap references cannot be bytecode constants");
        // Preserve floating encodings, including signed zero and NaN payloads.
        if (kind(value) != ValueKind::Float)
            for (Index i = 0; i < values_.size(); ++i) if (values_[i] == value) return i;
        values_.push_back(std::move(value));
        return values_.size() - 1;
    }
    const Value& at(Index index) const { return values_.at(index); }
    Index size() const { return values_.size(); }
    const std::vector<Value>& values() const { return values_; }
private:
    std::vector<Value> values_;
};

} // namespace firestone::vm
