#pragma once
#include "ControlFlowGraph.hpp"

namespace firestone::ir {

enum class MemoryAccessKind { LiveOnEntry, Use, Def, Phi };
struct MemoryAccess {
    MemoryAccessKind kind = MemoryAccessKind::LiveOnEntry;
    BlockId block = invalid_id;
    std::size_t instruction = invalid_id;
    std::vector<NodeId> incoming;
    std::vector<BlockId> incoming_blocks;
};

// Conservative single-partition MemorySSA: stores and calls clobber all memory.
class MemorySSAGraph {
public:
    static MemorySSAGraph from_cfg(const ControlFlowGraph& cfg) {
        auto errors = cfg.verify();
        if (!errors.empty()) throw std::invalid_argument(errors.front().message);
        if (!cfg.predecessors(cfg.entry()).empty()) throw std::invalid_argument("MemorySSA entry must have no predecessors");
        MemorySSAGraph out;
        out.entry_ = out.graph_.add_node({MemoryAccessKind::LiveOnEntry, cfg.entry(), invalid_id, {}, {}});
        std::map<BlockId, NodeId> heads, tails;
        for (auto b : cfg.blocks()) {
            const auto pred = cfg.predecessors(b);
            heads[b] = pred.empty() ? out.entry_ : out.graph_.add_node({MemoryAccessKind::Phi, b, invalid_id, {}, pred});
        }
        for (auto b : cfg.blocks()) {
            NodeId memory = heads[b];
            const auto& ins = cfg.block(b).instructions;
            for (std::size_t n = 0; n < ins.size(); ++n) {
                if (!is_effectful(ins[n].opcode)) continue;
                auto kind = ins[n].opcode == Opcode::Load ? MemoryAccessKind::Use : MemoryAccessKind::Def;
                auto id = out.graph_.add_node({kind, b, n, {memory}, {}});
                out.graph_.add_edge(memory, id, EdgeKind::Memory); out.accesses_[{b, n}] = id;
                if (kind == MemoryAccessKind::Def) memory = id;
            }
            tails[b] = memory;
        }
        for (auto b : cfg.blocks()) if (heads[b] != out.entry_) {
            auto& phi = out.graph_.node(heads[b]);
            for (std::size_t p = 0; p < phi.incoming_blocks.size(); ++p) {
                auto memory = tails.at(phi.incoming_blocks[p]); phi.incoming.push_back(memory);
                out.graph_.add_edge(memory, heads[b], EdgeKind::Memory, p);
            }
        }
        return out;
    }
    const DirectedGraph<MemoryAccess>& graph() const { return graph_; }
    NodeId live_on_entry() const { if (entry_ == invalid_id) throw std::logic_error("memory graph has no entry"); return entry_; }
    std::optional<NodeId> access(BlockId block, std::size_t instruction) const {
        auto it = accesses_.find({block, instruction}); return it == accesses_.end() ? std::optional<NodeId>{} : it->second;
    }
    std::set<NodeId> reaching_definitions(NodeId access) const {
        graph_.node(access);
        std::set<NodeId> out, seen;
        std::vector<NodeId> pending = graph_.node(access).incoming;
        while (!pending.empty()) {
            auto id = pending.back(); pending.pop_back(); if (!seen.insert(id).second) continue;
            const auto& node = graph_.node(id);
            if (node.kind == MemoryAccessKind::Def || node.kind == MemoryAccessKind::LiveOnEntry) out.insert(id);
            else pending.insert(pending.end(), node.incoming.begin(), node.incoming.end());
        }
        return out;
    }
private:
    DirectedGraph<MemoryAccess> graph_;
    NodeId entry_ = invalid_id;
    std::map<std::pair<BlockId, std::size_t>, NodeId> accesses_;
};

} // namespace firestone::ir
