#pragma once
#include "Rewrite.hpp"

namespace firestone::rewrite {

using Strategy = std::function<std::optional<Term>(const Term&)>;

inline Strategy rule_strategy(Rule rule) {
    validate_rule(rule.lhs, rule.rhs);
    return [rule = std::move(rule)](const Term& term) -> std::optional<Term> {
        auto substitution = match(rule.lhs, term);
        if (!substitution || (rule.guard && !rule.guard(*substitution))) return {};
        auto result = substitute(rule.rhs, *substitution);
        return result == term ? std::nullopt : std::optional<Term>{std::move(result)};
    };
}

inline Strategy choice(Strategy first, Strategy second) {
    if (!first || !second) throw std::invalid_argument("empty rewrite strategy");
    return [first = std::move(first), second = std::move(second)](const Term& term) {
        auto result = first(term); return result ? result : second(term);
    };
}

inline Strategy sequence(Strategy first, Strategy second) {
    if (!first || !second) throw std::invalid_argument("empty rewrite strategy");
    return [first = std::move(first), second = std::move(second)](const Term& term) -> std::optional<Term> {
        auto middle = first(term);
        auto result = second(middle ? *middle : term);
        auto final = result ? result : middle;
        if (final && *final == term) return {};
        return final;
    };
}

inline Strategy bottom_up(Strategy strategy) {
    if (!strategy) throw std::invalid_argument("empty rewrite strategy");
    return [strategy = std::move(strategy)](const Term& term) -> std::optional<Term> {
        validate_term(term);
        std::function<Term(Term)> walk = [&](Term node) {
            for (auto& child : node.children) child = walk(std::move(child));
            auto replacement = strategy(node);
            if (replacement) { validate_term(*replacement); return std::move(*replacement); }
            return node;
        };
        auto result = walk(term); return result == term ? std::nullopt : std::optional<Term>{std::move(result)};
    };
}

struct StrategyResult { Term term; std::size_t steps = 0; bool normal_form = true; };
inline StrategyResult repeat(const Strategy& strategy, Term term, std::size_t step_limit = 100) {
    if (!strategy) throw std::invalid_argument("empty rewrite strategy");
    validate_term(term);
    StrategyResult result{std::move(term), 0, true};
    while (true) {
        auto replacement = strategy(result.term);
        if (!replacement || *replacement == result.term) return result;
        validate_term(*replacement);
        if (result.steps == step_limit) { result.normal_form = false; return result; }
        result.term = std::move(*replacement); ++result.steps;
    }
}

} // namespace firestone::rewrite
