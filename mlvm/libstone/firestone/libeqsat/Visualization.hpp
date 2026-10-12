#pragma once
#include "EGraph.hpp"
#include <sstream>

namespace firestone::eqsat {

struct GraphStatistics { std::size_t classes = 0, nodes = 0, edges = 0, allocations = 0; };
inline GraphStatistics statistics(EGraph& graph) {
    graph.rebuild();
    GraphStatistics result{graph.classes().size(), graph.node_count(), 0, graph.allocated_nodes()};
    for (auto id : graph.classes()) for (const auto& node : graph.nodes(id)) result.edges += node.children.size();
    return result;
}

inline std::string to_dot(EGraph& graph) {
    graph.rebuild();
    auto escape = [](const std::string& text) {
        std::string result;
        for (char c : text) {
            if (c == '\n') result += "\\n";
            else if (c == '\r') result += "\\r";
            else { if (c == '"' || c == '\\') result += '\\'; result += c; }
        }
        return result;
    };
    std::ostringstream out; out << "digraph egraph {\n";
    for (auto id : graph.classes()) {
        out << "  c" << id << " [shape=box,label=\"class " << id << "\"];\n";
        const auto& nodes = graph.nodes(id);
        for (std::size_t n = 0; n < nodes.size(); ++n) {
            out << "  n" << id << '_' << n << " [label=\"" << escape(nodes[n].op) << "\"];\n";
            out << "  c" << id << " -> n" << id << '_' << n << " [style=dotted];\n";
            for (std::size_t p = 0; p < nodes[n].children.size(); ++p)
                out << "  n" << id << '_' << n << " -> c" << graph.find(nodes[n].children[p]) << " [label=\"" << p << "\"];\n";
        }
    }
    out << "}\n"; return out.str();
}

} // namespace firestone::eqsat
