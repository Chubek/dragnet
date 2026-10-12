#pragma once
#include "ControlFlowGraph.hpp"

namespace firestone::ir {

struct PhiInput { BlockId predecessor; Operand value; };
struct Definition { BlockId block = invalid_id; std::size_t instruction = invalid_id; bool parameter = false; };

class SimpleSSAGraph {
public:
    ControlFlowGraph& cfg() { return cfg_; }
    const ControlFlowGraph& cfg() const { return cfg_; }
    ValueId reserve_value(Type type) { types_.push_back(std::move(type)); parameters_.push_back(false); return types_.size() - 1; }
    ValueId parameter(Type type) { auto id = reserve_value(std::move(type)); parameters_[id] = true; return id; }
    std::size_t value_count() const { return types_.size(); }
    Type type(ValueId id) const { return types_.at(id); }
    ValueId define(BlockId block, Opcode opcode, Type type, std::vector<Operand> operands, std::string annotation = {}) {
        cfg_.block(block);
        if (opcode == Opcode::Phi) throw std::invalid_argument("use phi for phi definitions");
        auto id = reserve_value(std::move(type));
        try { define_reserved(block, id, opcode, std::move(operands), std::move(annotation)); }
        catch (...) { types_.pop_back(); parameters_.pop_back(); throw; }
        return id;
    }
    void define_reserved(BlockId block, ValueId value, Opcode opcode, std::vector<Operand> operands, std::string annotation = {}) {
        if (value >= types_.size()) throw std::invalid_argument("reserved value id is out of range");
        if (parameters_[value]) throw std::invalid_argument("value is already a parameter");
        if (is_terminator(opcode) || opcode == Opcode::Store || opcode == Opcode::Phi || type(value).kind == TypeKind::Void)
            throw std::invalid_argument("expected a value-producing instruction");
        for (const auto& o : operands) if (o.kind == OperandKind::Value && o.value == value)
            throw std::invalid_argument("value cannot use itself");
        cfg_.append(block, {opcode, value, type(value), std::move(operands), {}, {}, std::move(annotation)});
    }
    ValueId phi(BlockId block, Type type, std::vector<PhiInput> inputs) {
        auto value = reserve_value(std::move(type));
        try { phi_reserved(block, value, std::move(inputs)); }
        catch (...) { types_.pop_back(); parameters_.pop_back(); throw; }
        return value;
    }
    void phi_reserved(BlockId block, ValueId value, std::vector<PhiInput> inputs) {
        if (value >= types_.size()) throw std::invalid_argument("reserved value id is out of range");
        if (parameters_[value]) throw std::invalid_argument("value is already a parameter");
        if (inputs.empty()) throw std::invalid_argument("phi requires at least one input");
        for (const auto& inst : cfg_.block(block).instructions) if (inst.opcode != Opcode::Phi)
            throw std::logic_error("phis must precede other instructions");
        { std::set<BlockId> labels; for (const auto& in : inputs) if (!labels.insert(in.predecessor).second)
            throw std::invalid_argument("duplicate phi predecessor label"); }
        Instruction inst; inst.opcode = Opcode::Phi; inst.result = value; inst.type = type(value);
        for (auto& input : inputs) { cfg_.block(input.predecessor); inst.incoming_blocks.push_back(input.predecessor); inst.operands.push_back(std::move(input.value)); }
        cfg_.append(block, std::move(inst));
    }
    std::map<ValueId, Definition> definitions() const {
        std::map<ValueId, Definition> out;
        for (ValueId v = 0; v < parameters_.size(); ++v) if (parameters_[v]) out[v] = {invalid_id, invalid_id, true};
        for (auto b : cfg_.blocks()) {
            const auto& ins = cfg_.block(b).instructions;
            for (std::size_t n = 0; n < ins.size(); ++n) if (ins[n].result) out.emplace(*ins[n].result, Definition{b, n, false});
        }
        return out;
    }
    Diagnostics verify() const {
        auto out = cfg_.verify();
        if (cfg_.blocks().empty()) return out;
        auto defs = definitions(); auto dom = cfg_.dominators();
        std::set<ValueId> seen;
        for (ValueId v = 0; v < parameters_.size(); ++v) if (parameters_[v]) seen.insert(v);
        auto resolve = [&](ValueId v) { return v < types_.size() ? types_[v] : Type{}; };
        for (auto b : cfg_.blocks()) {
            bool past_phi = false;
            const auto& ins = cfg_.block(b).instructions;
            for (std::size_t n = 0; n < ins.size(); ++n) {
                const auto& i = ins[n];
                append_diagnostics(out, verify_instruction(i, resolve), b, n);
                if (i.result) {
                    if (*i.result >= types_.size()) diagnose(out, "value", "unknown result value", b, n);
                    else if (types_[*i.result] != i.type) diagnose(out, "type", "result type differs from its declaration", b, n);
                    if (!seen.insert(*i.result).second) diagnose(out, "ssa", "value has multiple definitions", b, n);
                }
                if (i.opcode == Opcode::Phi) {
                    if (past_phi) diagnose(out, "phi", "phi follows a non-phi instruction", b, n);
                    const auto pred = cfg_.predecessors(b);
                    std::set<BlockId> labels(i.incoming_blocks.begin(), i.incoming_blocks.end());
                    if (labels != std::set<BlockId>(pred.begin(), pred.end()) || labels.size() != i.incoming_blocks.size())
                        diagnose(out, "phi", "phi must cover every predecessor exactly once", b, n);
                } else past_phi = true;
                for (std::size_t p = 0; p < i.operands.size(); ++p) {
                    const auto& o = i.operands[p];
                    if (o.kind != OperandKind::Value) continue;
                    auto def = defs.find(o.value);
                    if (o.value >= types_.size() || def == defs.end()) { diagnose(out, "undefined", "value has no definition", b, n); continue; }
                    if (def->second.parameter) continue;
                    BlockId use_block = b;
                    std::size_t use_position = n;
                    if (i.opcode == Opcode::Phi) {
                        if (p >= i.incoming_blocks.size() || !cfg_.graph().contains(i.incoming_blocks[p])) continue;
                        use_block = i.incoming_blocks[p]; use_position = cfg_.block(use_block).instructions.size();
                    }
                    const auto& d = def->second;
                    if (!dom.at(use_block).count(d.block) || (d.block == use_block && d.instruction >= use_position))
                        diagnose(out, "dominance", "definition does not dominate its use", b, n);
                }
            }
        }
        for (ValueId v = 0; v < types_.size(); ++v) {
            if (!defs.count(v)) diagnose(out, "undefined", "reserved value was never defined", v);
            if (!types_[v].valid_value()) diagnose(out, "type", "value declaration has no valid value type", v);
        }
        return out;
    }
private:
    ControlFlowGraph cfg_;
    std::vector<Type> types_;
    std::vector<bool> parameters_;
};

} // namespace firestone::ir
