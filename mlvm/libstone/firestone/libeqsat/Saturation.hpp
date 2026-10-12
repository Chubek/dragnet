#pragma once
#include "EGraph.hpp"
#include "../librewrite/Rewrite.hpp"

namespace firestone::eqsat {

using ESubstitution = std::map<std::string, EClassId>;
struct Match { EClassId root; ESubstitution substitution; };
struct SearchResult { std::vector<Match> matches; bool limited = false; };

inline SearchResult search(EGraph& graph, const rewrite::Pattern& pattern, std::size_t limit = 10000) {
    rewrite::validate_term(pattern, true); graph.rebuild();
    using Callback = std::function<bool(const ESubstitution&)>;
    std::function<bool(const rewrite::Pattern&, EClassId, const ESubstitution&, const Callback&)> walk;
    walk = [&](const rewrite::Pattern& p, EClassId id, const ESubstitution& subst, const Callback& emit) {
        id = graph.find(id);
        if (rewrite::is_variable(p)) {
            auto found = subst.find(p.op);
            if (found != subst.end()) return graph.find(found->second) == id ? emit(subst) : true;
            auto next = subst; next[p.op] = id; return emit(next);
        }
        for (const auto& node : graph.nodes(id)) {
            if (node.op != p.op || node.children.size() != p.children.size()) continue;
            std::function<bool(std::size_t, const ESubstitution&)> children;
            children = [&](std::size_t index, const ESubstitution& current) {
                if (index == p.children.size()) return emit(current);
                return walk(p.children[index], node.children[index], current, [&](const ESubstitution& next) { return children(index + 1, next); });
            };
            if (!children(0, subst)) return false;
        }
        return true;
    };
    SearchResult result;
    std::set<std::pair<EClassId, ESubstitution>> seen;
    for (auto root : graph.classes()) {
        if (!walk(pattern, root, {}, [&](const ESubstitution& subst) {
            if (!seen.emplace(root, subst).second) return true;
            if (result.matches.size() == limit) { result.limited = true; return false; }
            result.matches.push_back({root, subst}); return true;
        })) break;
    }
    return result;
}

inline EClassId instantiate(EGraph& graph, const rewrite::Pattern& pattern, const ESubstitution& substitution,
                           std::size_t node_limit = std::numeric_limits<std::size_t>::max()) {
    rewrite::validate_term(pattern, true);
    if (rewrite::is_variable(pattern)) {
        auto it = substitution.find(pattern.op);
        if (it == substitution.end()) throw std::invalid_argument("unbound e-graph replacement variable");
        return graph.find(it->second);
    }
    std::vector<EClassId> children;
    for (const auto& c : pattern.children) children.push_back(instantiate(graph, c, substitution, node_limit));
    return graph.add(ENode{pattern.op, std::move(children)}, node_limit);
}

struct Rule {
    std::string name;
    rewrite::Pattern lhs, rhs;
    std::function<bool(const EGraph&, const ESubstitution&)> guard;
    Rule(std::string name, rewrite::Pattern lhs, rewrite::Pattern rhs,
         std::function<bool(const EGraph&, const ESubstitution&)> guard = {})
        : name(std::move(name)), lhs(std::move(lhs)), rhs(std::move(rhs)), guard(std::move(guard)) {
        rewrite::validate_rule(this->lhs, this->rhs);
    }
};

enum class SaturationStop { Saturated, IterationLimit, NodeLimit, MatchLimit, ApplicationLimit };
struct Limits {
    std::size_t iterations = 30;
    std::size_t nodes = 100000; // Monotonic allocations, including the initial graph.
    std::size_t matches_per_rule = 10000;
    std::size_t applications = 100000;
};
struct SaturationResult {
    SaturationStop stop = SaturationStop::Saturated;
    std::size_t iterations = 0;
    std::size_t applications = 0; // Accepted matches processed, including redundant equalities.
    std::map<std::string, std::size_t> rule_applications;
};

class Saturator {
public:
    void add_rule(Rule rule) { rewrite::validate_rule(rule.lhs, rule.rhs); rules_.push_back(std::move(rule)); }
    SaturationResult run(EGraph& graph, Limits limits = {}) const {
        graph.rebuild(); SaturationResult result;
        auto finish = [&](SaturationStop stop) { graph.rebuild(); result.stop = stop; return result; };
        if (graph.allocated_nodes() > limits.nodes) return finish(SaturationStop::NodeLimit);
        for (std::size_t iteration = 0; iteration < limits.iterations; ++iteration) {
            const auto before = graph.version(); ++result.iterations;
            for (const auto& rule : rules_) {
                auto matches = search(graph, rule.lhs, limits.matches_per_rule);
                // Match snapshots own their substitutions; mutations never
                // invalidate the traversal or substitute references.
                for (auto& match : matches.matches) {
                    graph.rebuild();
                    for (auto& binding : match.substitution) binding.second = graph.find(binding.second);
                    if (rule.guard && !rule.guard(graph, match.substitution)) continue;
                    if (result.applications == limits.applications) return finish(SaturationStop::ApplicationLimit);
                    try {
                        auto rhs = instantiate(graph, rule.rhs, match.substitution, limits.nodes);
                        graph.merge(match.root, rhs);
                    } catch (const NodeLimitReached&) { return finish(SaturationStop::NodeLimit); }
                    ++result.applications; ++result.rule_applications[rule.name];
                }
                if (matches.limited) return finish(SaturationStop::MatchLimit);
            }
            graph.rebuild();
            if (graph.version() == before) return finish(SaturationStop::Saturated);
        }
        return finish(SaturationStop::IterationLimit);
    }
private:
    std::vector<Rule> rules_;
};

} // namespace firestone::eqsat
