#pragma once
#include "ControlFlowGraph.hpp"

namespace firestone::ir {

enum class FlowDirection { Forward, Backward };
template<class State>
struct DataflowResult {
    std::map<BlockId, State> in, out;
    std::size_t evaluations = 0;
    bool converged = true;
};

// Meet and transfer must be monotone over the caller's finite-height lattice.
// Only entry-reachable blocks participate. Boundary states replace the meet.
template<class State, class Meet, class Transfer>
DataflowResult<State> solve_dataflow(const ControlFlowGraph& cfg, State initial, Meet meet, Transfer transfer,
                                   std::map<BlockId, State> boundary = {}, FlowDirection direction = FlowDirection::Forward,
                                   std::size_t evaluation_limit = 100000) {
    auto errors = cfg.verify();
    if (!errors.empty()) throw std::invalid_argument(errors.front().message);
    auto reachable = cfg.graph().reachable(cfg.entry());
    for (const auto& item : boundary) if (!reachable.count(item.first)) throw std::invalid_argument("dataflow boundary is unreachable");
    DataflowResult<State> result;
    for (auto block : reachable) { result.in.emplace(block, initial); result.out.emplace(block, initial); }
    auto pending = reachable;
    while (!pending.empty()) {
        if (result.evaluations == evaluation_limit) { result.converged = false; return result; }
        auto block = *pending.begin(); pending.erase(pending.begin()); ++result.evaluations;
        const bool forward = direction == FlowDirection::Forward;
        std::vector<State> incoming;
        for (auto neighbor : forward ? cfg.predecessors(block) : cfg.successors(block)) if (reachable.count(neighbor))
            incoming.push_back(forward ? result.out.at(neighbor) : result.in.at(neighbor));
        State input = boundary.count(block) ? boundary.at(block) : meet(incoming);
        State output = transfer(block, input);
        State& old_input = forward ? result.in.at(block) : result.out.at(block);
        State& old_output = forward ? result.out.at(block) : result.in.at(block);
        const bool changed = !(old_output == output);
        old_input = std::move(input); old_output = std::move(output);
        if (changed) for (auto neighbor : forward ? cfg.successors(block) : cfg.predecessors(block))
            if (reachable.count(neighbor)) pending.insert(neighbor);
    }
    return result;
}

} // namespace firestone::ir
