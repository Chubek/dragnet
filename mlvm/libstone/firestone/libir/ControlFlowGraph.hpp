#pragma once
#include "Graph.hpp"

namespace firestone::ir {

struct BasicBlock {
    std::string name;
    std::vector<Instruction> instructions;
};

class ControlFlowGraph {
public:
    BlockId create_block(std::string name = {}) {
        auto id = graph_.add_node({std::move(name), {}});
        if (!entry_) entry_ = id;
        return id;
    }
    void set_entry(BlockId id) { graph_.node(id); entry_ = id; }
    BlockId entry() const { if (!entry_) throw std::logic_error("CFG has no entry"); return *entry_; }
    const BasicBlock& block(BlockId id) const { return graph_.node(id); }
    const DirectedGraph<BasicBlock>& graph() const { return graph_; }
    std::vector<BlockId> blocks() const { return graph_.nodes(); }
    std::vector<BlockId> successors(BlockId id) const { return graph_.successors(id); }
    std::vector<BlockId> predecessors(BlockId id) const { return graph_.predecessors(id); }
    std::vector<BlockId> reverse_postorder() const { return graph_.reverse_postorder(entry(), false, EdgeKind::Control); }
    std::set<BlockId> unreachable_blocks() const {
        auto reachable = graph_.reachable(entry());
        std::set<BlockId> out;
        for (auto id : blocks()) if (!reachable.count(id)) out.insert(id);
        return out;
    }
    std::size_t append(BlockId id, Instruction inst) {
        if (is_terminator(inst.opcode)) throw std::invalid_argument("use terminate for a CFG terminator");
        auto& b = graph_.node(id);
        if (!b.instructions.empty() && is_terminator(b.instructions.back().opcode)) throw std::logic_error("block is already terminated");
        b.instructions.push_back(std::move(inst)); return b.instructions.size() - 1;
    }
    void terminate(BlockId id, Instruction inst) {
        graph_.node(id);
        if (!is_terminator(inst.opcode)) throw std::invalid_argument("expected a terminator");
        auto errors = verify_instruction(inst, [](ValueId) { return Type{}; }, false);
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        for (auto target : inst.targets) graph_.node(target);
        auto& b = graph_.node(id);
        if (!b.instructions.empty() && is_terminator(b.instructions.back().opcode)) throw std::logic_error("block is already terminated");
        for (std::size_t p = 0; p < inst.targets.size(); ++p) graph_.add_edge(id, inst.targets[p], EdgeKind::Control, p);
        b.instructions.push_back(std::move(inst));
    }
    void jump(BlockId from, BlockId to) { Instruction i; i.opcode = Opcode::Jump; i.targets = {to}; terminate(from, std::move(i)); }
    void branch(BlockId from, Operand condition, BlockId yes, BlockId no) {
        Instruction i; i.opcode = Opcode::Branch; i.operands = {std::move(condition)}; i.targets = {yes, no}; terminate(from, std::move(i));
    }
    void return_value(BlockId from, std::optional<Operand> value = {}) {
        Instruction i; i.opcode = Opcode::Return; if (value) i.operands.push_back(std::move(*value)); terminate(from, std::move(i));
    }
    DominatorSets dominators() const { return compute_dominators(graph_, entry()); }
    DominatorSets postdominators() const {
        // A synthetic exit makes multiple returns comparable. Non-exiting SCCs
        // have only self-postdominance (exit-conditioned postdominance).
        auto copy = graph_;
        auto exit = copy.add_node({"$exit", {}});
        for (auto id : blocks()) if (successors(id).empty()) copy.add_edge(id, exit, EdgeKind::Control);
        auto out = compute_dominators(copy, exit, true);
        out.erase(exit);
        for (auto& item : out) item.second.erase(exit);
        return out;
    }
    std::map<BlockId, std::set<BlockId>> dominance_frontiers() const {
        auto dom = dominators();
        std::map<BlockId, std::set<BlockId>> result;
        for (auto x : blocks()) {
            result[x];
            for (auto y : blocks()) {
                for (auto p : predecessors(y)) {
                    if (dom[p].count(x) && (x == y || !dom[y].count(x))) result[x].insert(y);
                }
            }
        }
        return result;
    }
    std::vector<std::pair<BlockId, BlockId>> control_dependencies() const {
        auto post = postdominators();
        std::set<std::pair<BlockId, BlockId>> out;
        for (auto a : blocks()) for (auto b : successors(a)) {
            if (post[a].count(b)) continue;
            auto stop = immediate_dominator(a, post);
            std::set<BlockId> seen;
            std::optional<BlockId> runner = b;
            while (runner && runner != stop && seen.insert(*runner).second) {
                out.emplace(a, *runner); runner = immediate_dominator(*runner, post);
            }
        }
        return {out.begin(), out.end()};
    }
    Diagnostics verify() const {
        Diagnostics out;
        if (!entry_) { diagnose(out, "entry", "CFG has no entry"); return out; }
        for (auto id : blocks()) {
            const auto& ins = block(id).instructions;
            if (ins.empty() || !is_terminator(ins.back().opcode)) diagnose(out, "terminator", "block has no terminator", id);
            for (std::size_t i = 0; i < ins.size(); ++i) {
                append_diagnostics(out, verify_instruction(ins[i], [](ValueId) { return Type{}; }, false), id, i);
                if (is_terminator(ins[i].opcode) && i + 1 != ins.size()) diagnose(out, "terminator", "terminator is not last", id, i);
            }
        }
        return out;
    }
private:
    DirectedGraph<BasicBlock> graph_;
    std::optional<BlockId> entry_;
};

} // namespace firestone::ir
