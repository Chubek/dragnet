#pragma once
#include "EGraph.hpp"

namespace firestone::eqsat {

struct ExtractedNode { std::string op; std::vector<std::size_t> children; };
struct DAGExtraction {
    std::vector<ExtractedNode> nodes; // Topologically ordered, children precede parents.
    std::size_t root = 0;
    double tree_cost = 0;
};

// Sharing is recovered after minimum-tree-cost extraction; this is not a
// minimum-DAG-cost solver (a different global optimization problem).
inline DAGExtraction extract_dag(EGraph& graph, EClassId root, std::function<double(const ENode&)> local_cost = {}) {
    auto extracted = graph.extract(root, std::move(local_cost));
    DAGExtraction result; result.tree_cost = extracted.cost;
    std::map<rewrite::Term, std::size_t> memo;
    std::function<std::size_t(const rewrite::Term&)> build = [&](const rewrite::Term& term) {
        auto found = memo.find(term);
        if (found != memo.end()) return found->second;
        std::vector<std::size_t> children;
        for (const auto& child : term.children) children.push_back(build(child));
        auto id = result.nodes.size(); result.nodes.push_back({term.op, std::move(children)}); memo.emplace(term, id); return id;
    };
    result.root = build(extracted.term); return result;
}

inline rewrite::Term expand(const DAGExtraction& extraction) {
    if (extraction.root >= extraction.nodes.size()) throw std::invalid_argument("invalid extraction root");
    std::vector<rewrite::Term> terms;
    for (std::size_t id = 0; id < extraction.nodes.size(); ++id) {
        const auto& node = extraction.nodes[id];
        std::vector<rewrite::Term> children;
        for (auto child : node.children) {
            if (child >= id) throw std::invalid_argument("extraction nodes must be topologically ordered");
            children.push_back(terms[child]);
        }
        terms.emplace_back(node.op, std::move(children));
    }
    return terms[extraction.root];
}

} // namespace firestone::eqsat
