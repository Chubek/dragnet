#include <mlvm/conversion.hpp>
#include <deque>

namespace mlvm {
bool ConversionGraph::add(ConversionEdge edge) {
    if (edge.source.empty() || edge.target.empty() || edge.source == edge.target || !edge.rewrite) return false;
    for (const auto& existing : edges_) if (existing.source == edge.source && existing.target == edge.target) return false;
    edges_.push_back(std::move(edge)); return true;
}
std::optional<std::vector<std::size_t>> ConversionGraph::path(std::string_view source, std::string_view target) const {
    std::deque<std::pair<std::string, std::vector<std::size_t>>> queue{{std::string(source), {}}};
    std::set<std::string> seen{std::string(source)};
    while (!queue.empty()) {
        auto [current, route] = std::move(queue.front()); queue.pop_front();
        if (current == target) return route;
        for (std::size_t i = 0; i < edges_.size(); ++i) if (edges_[i].source == current && seen.insert(edges_[i].target).second) {
            auto next = route; next.push_back(i); queue.emplace_back(edges_[i].target, std::move(next));
        }
    }
    return std::nullopt;
}
bool ConversionGraph::convert(Module& module, const std::set<std::string>& legal, DiagnosticEngine& diagnostics) const {
    if (!verify(module, diagnostics)) return false;
    for (const auto& dialect : legal) if (!module.context->has_dialect(dialect)) {
        diagnostics.error("conversion.dialect", "unregistered legal dialect: " + dialect); return false;
    }
    Module candidate = module;
    const auto diagnostic_start = diagnostics.diagnostics().size();
    bool ok = true;
    try {
        walk(candidate, [&](Operation& op) {
            if (!ok) return;
            const auto dialect = op.name.substr(0, op.name.find('.'));
            if (legal.contains(dialect)) return;
            std::optional<std::vector<std::size_t>> best;
            for (const auto& target : legal) {
                auto route = path(dialect, target);
                if (route && (!best || route->size() < best->size())) best = std::move(route);
            }
            if (!best) { diagnostics.error("conversion.path", "no legal conversion path for '" + op.name + "'", op.location); ok = false; return; }
            for (auto index : *best) {
                const auto& edge = edges_[index];
                if (!edge.rewrite(op, candidate, diagnostics) || op.name.substr(0, op.name.find('.')) != edge.target) {
                    diagnostics.error("conversion.rewrite", "conversion did not produce dialect '" + edge.target + "'", op.location); ok = false; break;
                }
            }
        });
        if (candidate.context != module.context) { diagnostics.error("conversion.context", "conversion changed context"); return false; }
        walk(candidate, [&](const Operation& op) {
            if (!legal.contains(op.name.substr(0, op.name.find('.')))) ok = false;
        });
        candidate.rebuild_uses();
        for (std::size_t i = diagnostic_start; i < diagnostics.diagnostics().size(); ++i) {
            const auto severity = diagnostics.diagnostics()[i].severity;
            if (severity == Severity::error || severity == Severity::fatal) ok = false;
        }
        if (!ok || !verify(candidate, diagnostics)) return false;
    } catch (const std::exception& error) { diagnostics.error("conversion.exception", error.what()); return false; }
    module = std::move(candidate); return true;
}
} // namespace mlvm
