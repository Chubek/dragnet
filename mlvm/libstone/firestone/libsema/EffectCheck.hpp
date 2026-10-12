#pragma once
#include "../libir/ControlFlowGraph.hpp"

namespace firestone::sema {

enum class Effect { Read, Write, Call };
struct EffectPolicy {
    bool allow_reads = true, allow_writes = true, allow_unknown_calls = true;
    std::map<std::string, std::set<Effect>> calls;
    static EffectPolicy pure() { EffectPolicy policy; policy.allow_reads = policy.allow_writes = policy.allow_unknown_calls = false; return policy; }
};

inline std::set<Effect> effects(const ir::Instruction& instruction, const EffectPolicy& policy) {
    if (instruction.opcode == ir::Opcode::Load) return {Effect::Read};
    if (instruction.opcode == ir::Opcode::Store) return {Effect::Write};
    if (instruction.opcode != ir::Opcode::Call) return {};
    auto found = policy.calls.find(instruction.annotation);
    return found == policy.calls.end() ? std::set<Effect>{Effect::Read, Effect::Write, Effect::Call} : found->second;
}

inline ir::Diagnostics check_effects(const ir::ControlFlowGraph& cfg, const EffectPolicy& policy) {
    ir::Diagnostics result;
    for (auto block : cfg.blocks()) {
        const auto& instructions = cfg.block(block).instructions;
        for (std::size_t n = 0; n < instructions.size(); ++n) {
            const auto& instruction = instructions[n];
            auto summary = effects(instruction, policy);
            if (summary.count(Effect::Read) && !policy.allow_reads) ir::diagnose(result, "effect.read", "memory read is forbidden", block, n);
            if (summary.count(Effect::Write) && !policy.allow_writes) ir::diagnose(result, "effect.write", "memory write is forbidden", block, n);
            if (summary.count(Effect::Call) && !policy.allow_unknown_calls) ir::diagnose(result, "effect.call", "unsummarized call is forbidden", block, n);
        }
    }
    return result;
}

} // namespace firestone::sema
