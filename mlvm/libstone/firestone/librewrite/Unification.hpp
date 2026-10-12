#pragma once
#include "Term.hpp"

namespace firestone::rewrite {

inline bool occurs(const std::string& variable, const Term& term) {
    if (is_variable(term) && term.op == variable) return true;
    for (const auto& child : term.children) if (occurs(variable, child)) return true;
    return false;
}

// Unlike one-way substitute, recursively resolves substitutions on both sides.
inline Term apply_substitution(const Term& term, const Substitution& substitution) {
    validate_term(term, true);
    std::set<std::string> active;
    std::function<Term(const Term&)> apply = [&](const Term& current) {
        if (is_variable(current) && substitution.count(current.op)) {
            if (!active.insert(current.op).second) throw std::invalid_argument("cyclic term substitution");
            validate_term(substitution.at(current.op), true);
            auto result = apply(substitution.at(current.op)); active.erase(current.op); return result;
        }
        auto result = current;
        for (auto& child : result.children) child = apply(child);
        return result;
    };
    return apply(term);
}

inline std::optional<Substitution> unify(const Term& left, const Term& right) {
    validate_term(left, true); validate_term(right, true);
    Substitution substitution;
    std::vector<std::pair<Term, Term>> pending{{left, right}};
    while (!pending.empty()) {
        auto pair = pending.back(); pending.pop_back();
        auto a = apply_substitution(pair.first, substitution), b = apply_substitution(pair.second, substitution);
        if (a == b) continue;
        if (!is_variable(a) && is_variable(b)) std::swap(a, b);
        if (is_variable(a)) {
            if (occurs(a.op, b)) return {};
            substitution[a.op] = std::move(b); continue;
        }
        if (a.op != b.op || a.children.size() != b.children.size()) return {};
        for (std::size_t i = 0; i < a.children.size(); ++i) pending.emplace_back(a.children[i], b.children[i]);
    }
    for (auto& binding : substitution) binding.second = apply_substitution(binding.second, substitution);
    return substitution;
}

} // namespace firestone::rewrite
