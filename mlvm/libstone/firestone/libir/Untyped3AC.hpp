#pragma once
#include "Common.hpp"

namespace firestone::ir {

class Untyped3AC {
public:
    ValueId create_register(Type type = {}, bool parameter = false) {
        types_.push_back(std::move(type)); parameters_.push_back(parameter); return types_.size() - 1;
    }
    std::size_t emit(Instruction inst) { instructions_.push_back(std::move(inst)); return instructions_.size() - 1; }
    const std::vector<Instruction>& instructions() const { return instructions_; }
    std::size_t register_count() const { return types_.size(); }
    Type type(ValueId id) const { return types_.at(id); }
    Diagnostics verify() const { return verify_impl(false); }
protected:
    std::vector<Type> types_;
    std::vector<bool> parameters_;
    std::vector<Instruction> instructions_;
    Diagnostics verify_impl(bool typed) const {
        Diagnostics out;
        std::set<ValueId> defined;
        for (ValueId v = 0; v < types_.size(); ++v) if (parameters_[v]) defined.insert(v);
        auto resolve = [&](ValueId id) { return id < types_.size() ? types_[id] : Type{}; };
        for (std::size_t n = 0; n < instructions_.size(); ++n) {
            const auto& i = instructions_[n];
            append_diagnostics(out, verify_instruction(i, resolve, typed), invalid_id, n);
            if (i.opcode == Opcode::Phi) diagnose(out, "phi", "linear 3AC does not support phi nodes", invalid_id, n);
            for (const auto& o : i.operands) if (o.kind == OperandKind::Value && !defined.count(o.value))
                diagnose(out, "undefined", "register used before definition", invalid_id, n);
            if (i.result) {
                if (*i.result >= types_.size()) diagnose(out, "register", "unknown result register", invalid_id, n);
                else {
                    if (typed && types_[*i.result] != i.type) diagnose(out, "type", "result register type differs", invalid_id, n);
                    defined.insert(*i.result);
                }
            }
            for (auto target : i.targets) if (target >= instructions_.size()) diagnose(out, "target", "branch target is outside the program", invalid_id, n);
        }
        return out;
    }
};

} // namespace firestone::ir
