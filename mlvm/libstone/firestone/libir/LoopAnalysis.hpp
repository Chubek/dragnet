#pragma once
#include "DominatorTree.hpp"

namespace firestone::ir {

struct NaturalLoop {
    BlockId header = invalid_id;
    std::set<BlockId> blocks, latches;
    std::vector<std::pair<BlockId, BlockId>> exits;
    std::optional<BlockId> parent_header;
};

inline std::vector<NaturalLoop> natural_loops(const ControlFlowGraph& cfg) {
    auto errors = cfg.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    auto reachable = cfg.graph().reachable(cfg.entry());
    DominatorTree dom(cfg);
    std::map<BlockId, NaturalLoop> by_header;
    for (auto latch : reachable) for (auto header : cfg.successors(latch)) if (dom.dominates(header, latch)) {
        auto& loop = by_header[header]; loop.header = header; loop.blocks.insert(header); loop.latches.insert(latch);
        std::vector<BlockId> pending;
        if (loop.blocks.insert(latch).second) pending.push_back(latch);
        while (!pending.empty()) {
            auto block = pending.back(); pending.pop_back();
            for (auto predecessor : cfg.predecessors(block)) if (reachable.count(predecessor) && loop.blocks.insert(predecessor).second)
                pending.push_back(predecessor);
        }
    }
    std::vector<NaturalLoop> result;
    for (auto& item : by_header) {
        auto& loop = item.second;
        for (auto block : loop.blocks) for (auto successor : cfg.successors(block))
            if (!loop.blocks.count(successor)) loop.exits.emplace_back(block, successor);
        std::size_t parent_size = invalid_id;
        for (const auto& candidate : by_header) if (candidate.second.blocks.size() > loop.blocks.size() &&
            candidate.second.blocks.size() < parent_size && std::includes(candidate.second.blocks.begin(), candidate.second.blocks.end(),
                loop.blocks.begin(), loop.blocks.end())) {
            loop.parent_header = candidate.first; parent_size = candidate.second.blocks.size();
        }
        result.push_back(loop);
    }
    return result;
}

// Reports maximal cyclic SCCs with multiple reachable entry blocks.
inline std::vector<std::set<BlockId>> irreducible_regions(const ControlFlowGraph& cfg) {
    auto errors = cfg.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    auto reachable = cfg.graph().reachable(cfg.entry());
    std::vector<std::set<BlockId>> result;
    for (const auto& component : cfg.graph().strongly_connected_components()) {
        if (component.size() < 2 || !reachable.count(component.front())) continue;
        std::set<BlockId> members(component.begin(), component.end()), entries;
        for (auto block : members) {
            if (block == cfg.entry()) entries.insert(block);
            for (auto predecessor : cfg.predecessors(block)) if (reachable.count(predecessor) && !members.count(predecessor)) entries.insert(block);
        }
        if (entries.size() > 1) result.push_back(std::move(members));
    }
    return result;
}

} // namespace firestone::ir
