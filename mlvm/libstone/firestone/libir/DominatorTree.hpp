#pragma once
#include "ControlFlowGraph.hpp"

namespace firestone::ir {

// An immutable analysis snapshot; rebuild it after changing the CFG.
class DominatorTree {
public:
    explicit DominatorTree(const ControlFlowGraph& cfg, bool postdominance = false)
        : sets_(postdominance ? cfg.postdominators() : cfg.dominators()) {
        for (const auto& item : sets_) children_[item.first];
        for (const auto& item : sets_) {
            auto parent = immediate_dominator(item.first, sets_); parents_[item.first] = parent;
            if (parent) children_[*parent].push_back(item.first);
            else roots_.push_back(item.first);
        }
    }
    bool dominates(BlockId dominator, BlockId block) const {
        (void)sets_.at(dominator); return sets_.at(block).count(dominator) != 0;
    }
    std::optional<BlockId> parent(BlockId block) const { return parents_.at(block); }
    const std::vector<BlockId>& children(BlockId block) const { return children_.at(block); }
    const std::vector<BlockId>& roots() const { return roots_; }
    std::size_t depth(BlockId block) const {
        std::size_t result = 0;
        while (parent(block)) { block = *parent(block); ++result; }
        return result;
    }
    std::optional<BlockId> nearest_common_dominator(BlockId a, BlockId b) const {
        auto da = depth(a), db = depth(b);
        while (da > db) { a = *parent(a); --da; }
        while (db > da) { b = *parent(b); --db; }
        while (a != b) {
            auto pa = parent(a), pb = parent(b);
            if (!pa || !pb) return {};
            a = *pa; b = *pb;
        }
        return a;
    }
private:
    DominatorSets sets_;
    std::map<BlockId, std::optional<BlockId>> parents_;
    std::map<BlockId, std::vector<BlockId>> children_;
    std::vector<BlockId> roots_;
};

} // namespace firestone::ir
