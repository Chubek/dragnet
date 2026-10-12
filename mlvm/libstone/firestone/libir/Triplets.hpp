#pragma once
#include "Common.hpp"

namespace firestone::ir {

struct Triplet {
    Opcode opcode;
    std::vector<Operand> operands;
};

// References name instruction positions; every triplet produces its implicit result.
class Triplets {
public:
    ValueId emit(Opcode opcode, std::vector<Operand> operands) {
        if (operands.size() > 2 || is_terminator(opcode) || opcode == Opcode::Store || opcode == Opcode::Phi || opcode == Opcode::Call)
            throw std::invalid_argument("triplet requires a unary or binary value operation");
        const auto id = instructions_.size();
        for (const auto& o : operands) if (o.kind == OperandKind::Value && o.value >= id)
            throw std::invalid_argument("triplet reference must precede its use");
        Instruction inst{opcode, id, {}, operands, {}, {}, {}};
        if (!verify_instruction(inst, [](ValueId) { return Type{}; }, false).empty()) throw std::invalid_argument("invalid triplet arity");
        instructions_.push_back({opcode, std::move(operands)}); return id;
    }
    const std::vector<Triplet>& instructions() const { return instructions_; }
private:
    std::vector<Triplet> instructions_;
};

} // namespace firestone::ir
