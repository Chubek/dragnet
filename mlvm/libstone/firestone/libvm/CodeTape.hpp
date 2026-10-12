#pragma once
#include "Opcode.hpp"

namespace firestone::vm {

enum class VMModel { Stack, Register };
struct Instruction {
    Opcode opcode = Opcode::Nop;
    Index destination = no_index;
    std::vector<Index> operands;
    Index index = no_index; // constant, local, or callee index
    Index target = no_index, alternate = no_index;
    Index argument_count = 0; // Call, NativeCall, Return
    unsigned bits = 64;
    std::string symbol;
};
class CodeTape {
public:
    Index emit(Instruction instruction) { instructions_.push_back(std::move(instruction)); return instructions_.size() - 1; }
    Index emit(Opcode opcode) { Instruction i; i.opcode = opcode; return emit(std::move(i)); }
    Instruction& at(Index index) { return instructions_.at(index); }
    const Instruction& at(Index index) const { return instructions_.at(index); }
    Index size() const { return instructions_.size(); }
    const std::vector<Instruction>& instructions() const { return instructions_; }
private:
    std::vector<Instruction> instructions_;
};

} // namespace firestone::vm
