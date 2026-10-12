#pragma once
#include "Term.hpp"
#include <functional>
#include <limits>

namespace firestone::rewrite {

inline void validate_rule(const Pattern& lhs, const Pattern& rhs) {
    auto bound = variables(lhs);
    for (const auto& v : variables(rhs)) if (!bound.count(v)) throw std::invalid_argument("unbound rule variable: " + v);
}

struct Rule {
    std::string name;
    Pattern lhs, rhs;
    std::function<bool(const Substitution&)> guard;
    Rule(std::string name, Pattern lhs, Pattern rhs, std::function<bool(const Substitution&)> guard = {})
        : name(std::move(name)), lhs(std::move(lhs)), rhs(std::move(rhs)), guard(std::move(guard)) {
        validate_rule(this->lhs, this->rhs);
    }
};

enum class RewriteStop { NormalForm, IterationLimit, ApplicationLimit };
enum class RewriteOrder { BottomUp, TopDown };
struct RewriteResult {
    Term term;
    RewriteStop stop = RewriteStop::NormalForm;
    std::size_t iterations = 0;
    std::size_t applications = 0;
    std::vector<std::string> applied_rules;
};

class Rewriter {
public:
    void add_rule(Rule rule) { validate_rule(rule.lhs, rule.rhs); rules_.push_back(std::move(rule)); }
    const std::vector<Rule>& rules() const { return rules_; }
    RewriteResult run(Term term, std::size_t iteration_limit = 100, std::size_t application_limit = 10000,
                      RewriteOrder order = RewriteOrder::BottomUp) const {
        validate_term(term);
        RewriteResult result; result.term = std::move(term);
        bool limited = false;
        auto apply = [&](Term& node) {
            for (const auto& rule : rules_) {
                auto subst = match(rule.lhs, node);
                if (!subst || (rule.guard && !rule.guard(*subst))) continue;
                auto replacement = substitute(rule.rhs, *subst);
                if (replacement == node) continue;
                if (result.applications == application_limit) { limited = true; return false; }
                node = std::move(replacement); ++result.applications; result.applied_rules.push_back(rule.name); return true;
            }
            return false;
        };
        std::function<bool(Term&)> visit = [&](Term& node) {
            bool changed = order == RewriteOrder::TopDown ? apply(node) : false;
            if (limited) return changed;
            for (auto& c : node.children) { changed = visit(c) || changed; if (limited) return changed; }
            if (order == RewriteOrder::BottomUp) changed = apply(node) || changed;
            return changed;
        };
        for (std::size_t n = 0; n < iteration_limit; ++n) {
            const bool changed = visit(result.term); ++result.iterations;
            if (limited) { result.stop = RewriteStop::ApplicationLimit; return result; }
            if (!changed) return result;
        }
        result.stop = RewriteStop::IterationLimit; return result;
    }
private:
    std::vector<Rule> rules_;
};

} // namespace firestone::rewrite
