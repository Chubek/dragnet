#pragma once
#include "Typed3AC.hpp"

namespace firestone::ir {

// Ordered let-bindings with atomic operands and one final atomic result.
class ANFGraph : public Typed3AC {
public:
    ValueId bind(Opcode opcode, Type type, std::vector<Operand> operands) {
        if (result_) throw std::logic_error("ANF body already has a result");
        return compute(opcode, std::move(type), std::move(operands));
    }
    void set_result(Operand operand) { result_ = std::move(operand); }
    const std::optional<Operand>& result() const { return result_; }
    Diagnostics verify() const {
        auto out = Typed3AC::verify();
        std::set<ValueId> defined;
        for (ValueId v = 0; v < types_.size(); ++v) if (parameters_[v]) defined.insert(v);
        for (std::size_t n = 0; n < instructions_.size(); ++n) {
            const auto& i = instructions_[n];
            if (is_terminator(i.opcode)) diagnose(out, "anf", "ANF binding cannot transfer control", invalid_id, n);
            if (i.result && !defined.insert(*i.result).second) diagnose(out, "anf", "ANF binding redefines a value", invalid_id, n);
        }
        if (!result_) diagnose(out, "anf", "ANF body has no final result");
        else {
            if (result_->kind == OperandKind::Value && !defined.count(result_->value)) diagnose(out, "undefined", "ANF result is undefined");
            auto resolve = [&](ValueId v) { return v < types_.size() ? types_[v] : Type{}; };
            Instruction final{Opcode::Copy, 0, operand_type(*result_, resolve), {*result_}, {}, {}, {}};
            append_diagnostics(out, verify_instruction(final, resolve), invalid_id, instructions_.size());
        }
        return out;
    }
private:
    std::optional<Operand> result_;
};

} // namespace firestone::ir
