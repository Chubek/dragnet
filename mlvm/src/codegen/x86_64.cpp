#include <mlvm/codegen.hpp>
#include <array>
#include <bit>
#include <sstream>

namespace mlvm::x86_64 {
namespace {
struct Fixed { Opcode opcode; std::array<std::uint8_t, 2> bytes; std::size_t size; const char* name; };
constexpr Fixed fixed[] = {
    {Opcode::ret, {0xc3, 0}, 1, "ret"},
    {Opcode::nop, {0x90, 0}, 1, "nop"},
    {Opcode::syscall, {0x0f, 0x05}, 2, "syscall"},
};
constexpr const char* registers[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
}
std::optional<std::vector<std::uint8_t>> encode(const Instruction& instruction, DiagnosticEngine& diagnostics) {
    if (instruction.source > 7 || instruction.destination > 7) {
        diagnostics.error("x86.register", "baseline backend requires an eax..edi register"); return std::nullopt;
    }
    for (const auto& entry : fixed) if (entry.opcode == instruction.opcode)
        return std::vector<std::uint8_t>(entry.bytes.begin(), entry.bytes.begin() + static_cast<std::ptrdiff_t>(entry.size));
    if (instruction.opcode == Opcode::mov_reg32)
        return std::vector<std::uint8_t>{0x89, static_cast<std::uint8_t>(0xc0 | (instruction.source << 3) | instruction.destination)};
    if (instruction.opcode == Opcode::push_reg32)
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(0x50 + instruction.source)};
    if (instruction.opcode == Opcode::pop_reg32)
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(0x58 + instruction.destination)};
    std::vector<std::uint8_t> result;
    if (instruction.opcode == Opcode::mov_imm32) result.push_back(static_cast<std::uint8_t>(0xb8 + instruction.destination));
    else if (instruction.opcode == Opcode::call_rel32) result.push_back(0xe8);
    else if (instruction.opcode == Opcode::jump_rel32) result.push_back(0xe9);
    else if (instruction.opcode == Opcode::add_imm32) result.insert(result.end(), {0x81, static_cast<std::uint8_t>(0xc0 | instruction.destination)});
    else if (instruction.opcode == Opcode::sub_imm32) result.insert(result.end(), {0x81, static_cast<std::uint8_t>(0xe8 | instruction.destination)});
    else { diagnostics.error("x86.opcode", "unsupported x86-64 machine opcode"); return std::nullopt; }
    for (unsigned i = 0; i < 4; ++i) result.push_back(static_cast<std::uint8_t>(instruction.immediate >> (8 * i)));
    return result;
}
std::optional<Decoded> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    for (const auto& entry : fixed) {
        if (bytes.size() < entry.size) continue;
        bool match = true;
        for (std::size_t i = 0; i < entry.size; ++i) if (bytes[i] != entry.bytes[i]) match = false;
        if (match) return Decoded{{entry.opcode}, entry.size};
    }
    if (bytes[0] == 0x89 && bytes.size() >= 2 && bytes[1] >= 0xc0)
        return Decoded{{Opcode::mov_reg32, static_cast<std::uint8_t>((bytes[1] >> 3) & 7), static_cast<std::uint8_t>(bytes[1] & 7), 0}, 2};
    if (bytes[0] >= 0x50 && bytes[0] <= 0x57)
        return Decoded{{Opcode::push_reg32, static_cast<std::uint8_t>(bytes[0] - 0x50), 0, 0}, 1};
    if (bytes[0] >= 0x58 && bytes[0] <= 0x5f)
        return Decoded{{Opcode::pop_reg32, 0, static_cast<std::uint8_t>(bytes[0] - 0x58), 0}, 1};
    if (bytes[0] == 0x81 && bytes.size() >= 6 && (bytes[1] & 0xc0) == 0xc0) {
        const unsigned reg = (bytes[1] >> 3) & 7;
        if (reg != 0 && reg != 5) return std::nullopt;
        Instruction instruction;
        instruction.opcode = reg == 5 ? Opcode::sub_imm32 : Opcode::add_imm32;
        instruction.destination = bytes[1] & 7;
        for (unsigned i = 0; i < 4; ++i) instruction.immediate |= std::uint32_t(bytes[i + 2]) << (8 * i);
        return Decoded{instruction, 6};
    }
    if (bytes.size() < 5) return std::nullopt;
    Instruction instruction;
    if (bytes[0] >= 0xb8 && bytes[0] <= 0xbf) { instruction.opcode = Opcode::mov_imm32; instruction.destination = bytes[0] - 0xb8; }
    else if (bytes[0] == 0xe8) instruction.opcode = Opcode::call_rel32;
    else if (bytes[0] == 0xe9) instruction.opcode = Opcode::jump_rel32;
    else return std::nullopt;
    for (unsigned i = 0; i < 4; ++i) instruction.immediate |= std::uint32_t(bytes[i + 1]) << (8 * i);
    return Decoded{instruction, 5};
}
std::string format(const Instruction& instruction) {
    for (const auto& entry : fixed) if (entry.opcode == instruction.opcode) return entry.name;
    if (instruction.source > 7 || instruction.destination > 7) return "<invalid register>";
    std::ostringstream out;
    if (instruction.opcode == Opcode::mov_reg32) out << "mov %" << registers[instruction.source] << ", %" << registers[instruction.destination];
    else if (instruction.opcode == Opcode::mov_imm32) out << "mov $0x" << std::hex << instruction.immediate << ", %" << registers[instruction.destination];
    else if (instruction.opcode == Opcode::add_imm32) out << "add $0x" << std::hex << instruction.immediate << ", %" << registers[instruction.destination];
    else if (instruction.opcode == Opcode::sub_imm32) out << "sub $0x" << std::hex << instruction.immediate << ", %" << registers[instruction.destination];
    else if (instruction.opcode == Opcode::push_reg32) out << "push %" << registers[instruction.source];
    else if (instruction.opcode == Opcode::pop_reg32) out << "pop %" << registers[instruction.destination];
    else if (instruction.opcode == Opcode::call_rel32 || instruction.opcode == Opcode::jump_rel32)
        out << (instruction.opcode == Opcode::call_rel32 ? "call" : "jmp") << " rel32(" << std::bit_cast<std::int32_t>(instruction.immediate) << ')';
    else out << "<invalid opcode>";
    return out.str();
}
} // namespace mlvm::x86_64
