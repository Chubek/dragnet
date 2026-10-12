#pragma once
#include "Untyped3AC.hpp"

namespace firestone::ir {

// Explicit destination plus at most two operands: (opcode, arg1, arg2, result).
class Quadruplets : public Untyped3AC {
public:
    std::size_t emit(Instruction inst) {
        if (inst.operands.size() > 2) throw std::invalid_argument("quadruplet has more than two operands");
        return Untyped3AC::emit(std::move(inst));
    }
    Diagnostics verify() const {
        auto out = Untyped3AC::verify();
        for (std::size_t n = 0; n < instructions_.size(); ++n) if (instructions_[n].operands.size() > 2)
            diagnose(out, "arity", "quadruplet has more than two operands", invalid_id, n);
        return out;
    }
};

} // namespace firestone::ir
