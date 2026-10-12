#pragma once
#include "EGraph.hpp"

namespace firestone::eqsat {

template<class Data>
struct AnalysisResult {
    std::map<EClassId, Data> data;
    std::size_t iterations = 0, graph_version = 0;
    bool converged = true;
    const Data& at(const EGraph& graph, EClassId id) const {
        if (graph.version() != graph_version) throw std::logic_error("e-graph analysis snapshot is stale");
        return data.at(graph.find(id));
    }
};

// make(node, child data) and join(old, contribution) must be monotone. Join must
// be associative, commutative and idempotent; callbacks must not mutate graph.
template<class Data, class Make, class Join>
AnalysisResult<Data> analyze(EGraph& graph, Data bottom, Make make, Join join, std::size_t iteration_limit = 1000) {
    graph.rebuild();
    AnalysisResult<Data> result; result.graph_version = graph.version();
    auto classes = graph.classes();
    for (auto id : classes) result.data.emplace(id, bottom);
    if (classes.empty()) return result;
    for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
        bool changed = false; ++result.iterations;
        for (auto id : classes) for (const auto& node : graph.nodes(id)) {
            std::vector<Data> children;
            for (auto child : node.children) children.push_back(result.data.at(graph.find(child)));
            Data next = join(result.data.at(id), make(node, children));
            if (!(next == result.data.at(id))) { result.data.at(id) = std::move(next); changed = true; }
        }
        if (!changed) return result;
    }
    result.converged = false; return result;
}

} // namespace firestone::eqsat
