#include <mlvm/optilisp/rewrite.hpp>

#include <algorithm>
#include <stdexcept>

namespace mlvm::optilisp {
namespace {
bool pattern_shape(const Expr& expression, std::set<std::string>& variables) {
    if (expression.kind != ExprKind::list) {
        if (expression.kind == ExprKind::symbol) variables.insert(expression.value);
        return true;
    }
    if (expression.items.empty() || expression.items[0].kind != ExprKind::symbol) return false;
    for (std::size_t i = 1; i < expression.items.size(); ++i)
        if (!pattern_shape(expression.items[i], variables)) return false;
    return true;
}
bool result_shape(const Expr& expression, const std::set<std::string>& variables) {
    if (expression.kind != ExprKind::list)
        return expression.kind != ExprKind::symbol || variables.contains(expression.value);
    if (expression.items.empty() || expression.items[0].kind != ExprKind::symbol) return false;
    for (std::size_t i = 1; i < expression.items.size(); ++i)
        if (!result_shape(expression.items[i], variables)) return false;
    return true;
}
bool match(const Expr& pattern, const Expr& input, Bindings& bindings) {
    if (pattern.kind == ExprKind::symbol) {
        auto [found, inserted] = bindings.emplace(pattern.value, input);
        return inserted || found->second == input;
    }
    if (pattern.kind != ExprKind::list) return pattern == input;
    if (input.kind != ExprKind::list || input.items.size() != pattern.items.size() || pattern.items[0] != input.items[0]) return false;
    for (std::size_t i = 1; i < pattern.items.size(); ++i)
        if (!match(pattern.items[i], input.items[i], bindings)) return false;
    return true;
}
void measure(const Expr& expression, std::size_t depth, std::size_t& nodes, std::size_t& bytes, ParseLimits limits) {
    if (++nodes > limits.max_nodes || depth > limits.max_depth) throw std::runtime_error("rewrite term exceeds node/depth limit");
    // Bound data bytes as well as nodes, including copied bound strings.
    if (expression.value.size() > limits.max_bytes - std::min(bytes, limits.max_bytes)) throw std::runtime_error("rewrite term exceeds byte limit");
    bytes += expression.value.size();
    for (const auto& item : expression.items) measure(item, depth + 1, nodes, bytes, limits);
}
Expr instantiate(const Expr& expression, const Bindings& bindings, const Location& origin,
                 std::size_t depth, std::size_t& nodes, std::size_t& bytes, ParseLimits limits) {
    if (expression.kind == ExprKind::symbol) {
        const auto& bound = bindings.at(expression.value);
        measure(bound, depth, nodes, bytes, limits);
        return bound;
    }
    Expr output = expression;
    output.location = origin;
    output.items.clear();
    Expr leaf = output;
    measure(leaf, depth, nodes, bytes, limits);
    for (std::size_t i = 0; i < expression.items.size(); ++i) {
        if (!i) {
            const auto& constructor = expression.items[i];
            measure(constructor, depth + 1, nodes, bytes, limits);
            output.items.push_back(constructor);
            output.items.back().location = origin;
        } else output.items.push_back(instantiate(expression.items[i], bindings, origin, depth + 1, nodes, bytes, limits));
    }
    return output;
}
bool applicable(const ProofRequest& request, Stage stage, const Facts& facts, const ConditionProver& prove) {
    const auto& kernel = request.kernel;
    if (stage != kernel.stage || !prove) return false;
    for (const auto& requirement : kernel.requirements)
        if (facts.get(requirement.kind + ":" + requirement.id) != Fact::proven_true) return false;
    for (const auto& prior : kernel.prerequisites)
        if (facts.get("history:" + prior) != Fact::proven_true) return false;
    for (const auto& prior : kernel.forbidden_prior)
        if (facts.get("history:" + prior) != Fact::proven_false) return false;
    for (const auto& condition : kernel.preconditions)
        if (prove(request, condition) != Fact::proven_true) return false;
    for (const auto& condition : kernel.forbids)
        if (prove(request, condition) != Fact::proven_false) return false;
    return true;
}
} // namespace

class RewriteSystem::Batch final : public Installation {
public:
    explicit Batch(RewriteSystem& owner) : owner_(owner), candidate_(owner.kernels_) {}
    bool add(const Kernel& kernel, Diagnostics& diagnostics) override {
        auto fail = [&](const std::string& message) {
            diagnostics.push_back({"optilisp.backend", kernel.id + ": " + message, kernel.location}); return false;
        };
        if (finished_) return fail("installation already committed");
        const auto& transform = kernel.transformation;
        if (transform.kind != ExprKind::list || transform.items.size() != 3 || !transform.items[0].symbol("rewrite"))
            return fail("tree backend supports rewrite(pattern, result) only; use a host optimizer for other verbs");
        std::set<std::string> variables;
        if (transform.items[1].kind != ExprKind::list || !pattern_shape(transform.items[1], variables))
            return fail("pattern must be a constructor-headed tree");
        if (!result_shape(transform.items[2], variables))
            return fail("unbound result variable or nonconstructor result; computed values need a host adapter");
        if (kernel.preconditions.empty() || kernel.forbids.empty() || kernel.requirements.empty()) return fail("incomplete applicability contract");
        if (std::any_of(candidate_.begin(), candidate_.end(), [&](const Kernel& existing) { return existing.id == kernel.id; }))
            return fail("kernel already installed");
        candidate_.push_back(kernel);
        return true;
    }
    bool commit(Diagnostics& diagnostics) override {
        if (finished_) { diagnostics.push_back({"optilisp.install", "installation already committed", {}}); return false; }
        std::sort(candidate_.begin(), candidate_.end(), [](const Kernel& a, const Kernel& b) { return a.id < b.id; });
        owner_.kernels_.swap(candidate_);
        finished_ = true;
        return true;
    }
private:
    RewriteSystem& owner_;
    std::vector<Kernel> candidate_;
    bool finished_ = false;
};
std::unique_ptr<Installation> RewriteSystem::begin_installation(Diagnostics&) {
    return std::make_unique<Batch>(*this);
}

OptimizeResult RewriteSystem::optimize(Expr& term, RewriteContext& context, std::size_t rewrite_budget, ParseLimits limits) const {
    OptimizeResult result;
    try {
        std::size_t nodes = 0, bytes = 0;
        measure(term, 1, nodes, bytes, limits);
        Expr candidate = term;
        Facts facts = context.facts;
        std::set<std::string> seen{candidate.print()};
        std::size_t rewrites = 0;
        std::function<bool(Expr&)> visit = [&](Expr& input) {
            for (const auto& kernel : kernels_) {
                Bindings bindings;
                if (!match(kernel.transformation.items[1], input, bindings)) continue;
                std::size_t count = 0, size = 0;
                Expr replacement = instantiate(kernel.transformation.items[2], bindings, input.location, 1, count, size, limits);
                if (replacement == input) continue; // A syntactic no-op is not progress.
                if (!applicable({kernel, input, replacement, bindings}, context.stage, facts, context.prove)) continue;
                input = std::move(replacement);
                facts.invalidate(kernel.analyses);
                facts.set("history:" + kernel.id, Fact::proven_true);
                return true;
            }
            for (auto& item : input.items) if (visit(item)) return true;
            return false;
        };
        while (visit(candidate)) {
            if (++rewrites > rewrite_budget) throw std::runtime_error("rewrite budget exhausted; changes discarded");
            nodes = bytes = 0;
            measure(candidate, 1, nodes, bytes, limits);
            if (!seen.insert(candidate.print()).second) throw std::runtime_error("rewrite cycle detected; changes discarded");
        }
        term = std::move(candidate);
        context.facts = std::move(facts);
        result.success = true;
        result.rewrites = rewrites;
    } catch (const std::exception& failure) {
        result.diagnostics.push_back({"optilisp.rewrite", failure.what(), term.location});
    }
    return result;
}
} // namespace mlvm::optilisp
