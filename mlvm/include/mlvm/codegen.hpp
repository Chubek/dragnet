#pragma once
#include <mlvm/core.hpp>
#include <span>

namespace mlvm::x86_64 {
// Explicit machine instructions for the baseline backend, not semantic IR ops.
enum class Opcode { ret, nop, syscall, mov_imm32, mov_reg32, call_rel32, jump_rel32, add_imm32, sub_imm32, push_reg32, pop_reg32 };
struct Instruction {
    Opcode opcode = Opcode::nop;
    std::uint8_t source = 0, destination = 0; // eax..edi, architectural encoding order
    std::uint32_t immediate = 0; // raw imm32 or signed rel32 bit pattern
};
struct Decoded { Instruction instruction; std::size_t size; };
std::optional<std::vector<std::uint8_t>> encode(const Instruction& instruction, DiagnosticEngine& diagnostics);
// Unknown or truncated instructions return nullopt; callers must not guess lengths.
std::optional<Decoded> decode(std::span<const std::uint8_t> bytes);
std::string format(const Instruction& instruction);
} // namespace mlvm::x86_64
