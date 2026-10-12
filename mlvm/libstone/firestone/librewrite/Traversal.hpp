#pragma once
#include "Term.hpp"

namespace firestone::rewrite {

using Position = std::vector<std::size_t>;

inline const Term& term_at(const Term& term, const Position& position) {
    const Term* current = &term;
    for (auto index : position) current = &current->children.at(index);
    return *current;
}
inline Term& term_at(Term& term, const Position& position) {
    Term* current = &term;
    for (auto index : position) current = &current->children.at(index);
    return *current;
}

template<class Visitor>
void walk(const Term& term, Visitor visitor) {
    validate_term(term);
    Position position;
    std::function<void(const Term&)> visit = [&](const Term& current) {
        visitor(position, current);
        for (std::size_t i = 0; i < current.children.size(); ++i) {
            position.push_back(i); visit(current.children[i]); position.pop_back();
        }
    };
    visit(term);
}

inline std::vector<Position> positions(const Term& term, bool include_variables = true) {
    std::vector<Position> result;
    walk(term, [&](const Position& position, const Term& current) { if (include_variables || !is_variable(current)) result.push_back(position); });
    return result;
}

inline Term replace_at(Term term, const Position& position, Term replacement) {
    validate_term(term); validate_term(replacement);
    term_at(term, position) = std::move(replacement); return term;
}

template<class Transform>
Term transform_bottom_up(Term term, Transform transform) {
    validate_term(term);
    for (auto& child : term.children) child = transform_bottom_up(std::move(child), transform);
    auto result = transform(term); validate_term(result); return result;
}

} // namespace firestone::rewrite
