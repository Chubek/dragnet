#pragma once
#include "Rewrite.hpp"
#include "Traversal.hpp"
#include "Unification.hpp"

namespace firestone::rewrite {

struct CriticalPair {
    std::string outer_rule, inner_rule;
    Position overlap;
    Term source, left, right;
};

inline std::vector<CriticalPair> critical_pairs(const Rule& outer, const Rule& inner) {
    validate_rule(outer.lhs, outer.rhs); validate_rule(inner.lhs, inner.rhs);
    if (outer.guard || inner.guard) throw std::invalid_argument("symbolic critical pairs require unguarded rules");
    auto rename = [](const Term& term, const std::string& prefix) {
        Substitution names;
        for (const auto& variable : variables(term)) names.emplace(variable, Term("?" + prefix + variable.substr(1)));
        return substitute(term, names);
    };
    auto lhs = rename(outer.lhs, "outer."), rhs = rename(outer.rhs, "outer.");
    auto inner_lhs = rename(inner.lhs, "inner."), inner_rhs = rename(inner.rhs, "inner.");
    std::vector<CriticalPair> result;
    for (const auto& position : positions(lhs, false)) {
        auto substitution = unify(term_at(lhs, position), inner_lhs);
        if (!substitution) continue;
        auto left = apply_substitution(rhs, *substitution);
        auto right = apply_substitution(replace_at(lhs, position, inner_rhs), *substitution);
        if (left != right) result.push_back({outer.name, inner.name, position, apply_substitution(lhs, *substitution), std::move(left), std::move(right)});
    }
    return result;
}

} // namespace firestone::rewrite
