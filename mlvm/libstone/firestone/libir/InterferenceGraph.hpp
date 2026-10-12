#pragma once
#include "SimpleSSAGraph.hpp"

namespace firestone::ir {

struct Liveness {
    std::map<BlockId, std::set<ValueId>> live_in, live_out;
};

inline Liveness compute_liveness(const SimpleSSAGraph& ssa) {
    auto errors = ssa.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    const auto& cfg = ssa.cfg();
    std::map<BlockId, std::set<ValueId>> use, def, phi_def;
    std::map<std::pair<BlockId, BlockId>, std::set<ValueId>> phi_use;
    Liveness live;
    for (auto b : cfg.blocks()) {
        live.live_in[b]; live.live_out[b];
        for (const auto& i : cfg.block(b).instructions) {
            for (std::size_t p = 0; p < i.operands.size(); ++p) if (i.operands[p].kind == OperandKind::Value) {
                auto v = i.operands[p].value;
                if (i.opcode == Opcode::Phi) {
                    if (p >= i.incoming_blocks.size()) continue;
                    phi_use[{i.incoming_blocks[p], b}].insert(v);
                }
                else if (!def[b].count(v)) use[b].insert(v);
            }
            if (i.result) { def[b].insert(*i.result); if (i.opcode == Opcode::Phi) phi_def[b].insert(*i.result); }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto b : cfg.blocks()) {
            std::set<ValueId> out;
            for (auto successor : cfg.successors(b)) {
                for (auto v : live.live_in[successor]) if (!phi_def[successor].count(v)) out.insert(v);
                const auto& edge = phi_use[{b, successor}]; out.insert(edge.begin(), edge.end());
            }
            auto in = use[b]; for (auto v : out) if (!def[b].count(v)) in.insert(v);
            if (in != live.live_in[b] || out != live.live_out[b]) { live.live_in[b] = std::move(in); live.live_out[b] = std::move(out); changed = true; }
        }
    }
    return live;
}

class InterferenceGraph {
public:
    void add_value(ValueId value) { adjacency_[value]; }
    void interfere(ValueId a, ValueId b) {
        add_value(a); add_value(b);
        if (a != b) { adjacency_[a].insert(b); adjacency_[b].insert(a); }
    }
    const std::set<ValueId>& neighbors(ValueId v) const { return adjacency_.at(v); }
    bool interferes(ValueId a, ValueId b) const { auto it = adjacency_.find(a); return it != adjacency_.end() && it->second.count(b); }
    std::map<ValueId, std::size_t> color(std::size_t colors, std::map<ValueId, std::size_t> fixed = {}) const {
        for (const auto& item : fixed) {
            if (!adjacency_.count(item.first) || item.second >= colors) throw std::invalid_argument("invalid precolor");
            for (auto n : adjacency_.at(item.first)) if (fixed.count(n) && fixed.at(n) == item.second) throw std::invalid_argument("conflicting precolors");
        }
        std::vector<ValueId> order;
        for (const auto& item : adjacency_) if (!fixed.count(item.first)) order.push_back(item.first);
        std::sort(order.begin(), order.end(), [&](ValueId a, ValueId b) {
            return adjacency_.at(a).size() != adjacency_.at(b).size() ? adjacency_.at(a).size() > adjacency_.at(b).size() : a < b;
        });
        for (auto v : order) {
            std::set<std::size_t> used;
            for (auto n : adjacency_.at(v)) if (fixed.count(n)) used.insert(fixed.at(n));
            std::size_t c = 0; while (c < colors && used.count(c)) ++c;
            if (c == colors) throw std::runtime_error("greedy coloring requires a spill or more colors");
            fixed[v] = c;
        }
        return fixed;
    }
    static InterferenceGraph from_ssa(const SimpleSSAGraph& ssa) {
        auto live = compute_liveness(ssa);
        InterferenceGraph out;
        for (ValueId v = 0; v < ssa.value_count(); ++v) out.add_value(v);
        for (auto b : ssa.cfg().blocks()) {
            auto current = live.live_out[b];
            const auto& ins = ssa.cfg().block(b).instructions;
            for (auto it = ins.rbegin(); it != ins.rend(); ++it) {
                if (it->result) {
                    for (auto v : current) out.interfere(*it->result, v);
                    current.erase(*it->result);
                }
                if (it->opcode != Opcode::Phi) for (const auto& o : it->operands) if (o.kind == OperandKind::Value) current.insert(o.value);
            }
            // Parameters are simultaneously available on entry, including when
            // a parameter is used directly on an outgoing phi edge.
            if (b == ssa.cfg().entry()) for (const auto& d : ssa.definitions()) if (d.second.parameter)
                for (auto v : current) out.interfere(d.first, v);
        }
        return out;
    }
private:
    std::map<ValueId, std::set<ValueId>> adjacency_;
};

} // namespace firestone::ir
