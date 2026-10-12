#pragma once

#include <mlvm/optilisp/loader.hpp>
#include <mlvm/NodeIR/Commo.hpp>
#include <functional>

namespace mlvm::optilisp {

using Bindings = std::map<std::string, Expr>;
struct ProofRequest {
    const Kernel& kernel;
    const Expr& input;
    const Expr& replacement;
    const Bindings& bindings;
};
// Candidate-specific proof queries. Missing callback or proof means unknown.
// Callbacks must be side-effect-free and preserve their own state on exceptions.
using ConditionProver = std::function<Fact(const ProofRequest&, std::string_view)>;
struct RewriteContext {
    Stage stage = Stage::hir;
    Facts facts;
    ConditionProver prove;
};
struct OptimizeResult {
    bool success = false;
    std::size_t rewrites = 0;
    Diagnostics diagnostics;
};

// Executable tree rewriting adapter, not NodeIR. Constructor heads are literal;
// operand symbols are bindings; numbers, strings, booleans, keywords and nil are
// literals. Repeated bindings must agree. Replacement symbols must be bound.
class RewriteSystem final : public KernelConsumer {
public:
    std::unique_ptr<Installation> begin_installation(Diagnostics&) override;
    std::span<const Kernel> kernels() const { return kernels_; }
    // Preorder/ID-order fixed-point rewriting. Term and facts commit together on
    // success; exceptions, cycles, resource limits, and budget exhaustion roll back.
    OptimizeResult optimize(Expr& term, RewriteContext& context,
                            std::size_t rewrite_budget = 64,
                            ParseLimits limits = {}) const;
private:
    class Batch;
    std::vector<Kernel> kernels_;
};

namespace detail {
inline nodeir::Pattern node_pattern(const Expr& expression, ParseLimits limits = {}) {
    std::size_t nodes = 0, bytes = 0;
    std::function<nodeir::Pattern(const Expr&, std::size_t)> convert = [&](const Expr& e, std::size_t depth) {
        if (++nodes > limits.max_nodes || depth > limits.max_depth ||
            e.value.size() > limits.max_bytes - std::min(bytes, limits.max_bytes))
            throw std::runtime_error("NodeIR pattern limits exceeded");
        bytes += e.value.size();
        if (e.kind == ExprKind::list) {
            if (e.items.empty() || e.items[0].kind != ExprKind::symbol || e.items[0].value.empty())
                throw std::runtime_error("NodeIR pattern requires a constructor head");
            std::vector<nodeir::Pattern> operands;
            for (std::size_t i = 1; i < e.items.size(); ++i) operands.push_back(convert(e.items[i], depth + 1));
            return nodeir::Pattern::operation(e.items[0].value, std::move(operands));
        }
        if (!e.items.empty()) throw std::runtime_error("NodeIR atom has child expressions");
        if (e.kind == ExprKind::symbol) return nodeir::Pattern::bind(e.value);
        nodeir::Kind kind;
        switch (e.kind) {
        case ExprKind::keyword: kind = nodeir::Kind::keyword; break;
        case ExprKind::string: kind = nodeir::Kind::string; break;
        case ExprKind::number: kind = nodeir::Kind::number; break;
        case ExprKind::boolean: kind = nodeir::Kind::boolean; break;
        case ExprKind::nil: kind = nodeir::Kind::nil; break;
        default: throw std::runtime_error("NodeIR unsupported atom kind");
        }
        return nodeir::Pattern::literal(kind, e.value);
    };
    return convert(expression, 1);
}
inline Stage node_stage(nodeir::Stage stage) {
    switch (stage) {
    case nodeir::Stage::hir: return Stage::hir;
    case nodeir::Stage::mir: return Stage::mir;
    case nodeir::Stage::lir: return Stage::lir;
    }
    throw std::runtime_error("invalid NodeIR stage");
}
} // namespace detail

// Public compiler for the constructor/variable/literal language used by kernels.
inline nodeir::Pattern compile_node_pattern(const Expr& expression, ParseLimits limits = {}) {
    auto pattern = detail::node_pattern(expression, limits);
    std::set<std::string> variables;
    pattern.validate(variables, {limits.max_nodes, limits.max_depth, limits.max_bytes});
    return pattern;
}
template<class IR> struct NodeProofRequest {
    const Kernel& kernel;
    const IR& original;
    const IR& replacement_graph;
    nodeir::NodeId input, replacement;
    const nodeir::Bindings& bindings;
};
template<class IR> struct NodeRewriteContext {
    Facts facts;
    // Optional host constructor/evaluator for the result pattern. Operates only
    // on a private candidate graph; supplies explicit types/effects/attributes.
    // Without it, result constructors have unknown type and effects.
    std::function<nodeir::NodeId(const Kernel&, const nodeir::Pattern&, IR&,
                               const nodeir::Bindings&, nodeir::NodeId)> materialize;
    // Must prove legality for ALL uses of the replaced definition, including
    // types, effects, provenance and stage invariants of newly constructed nodes.
    std::function<Fact(const NodeProofRequest<IR>&, std::string_view)> prove;
};
template<class External> struct TranslatedOptimizeResult {
    std::optional<External> value;
    OptimizeResult optimization;
    bool ok() const { return value.has_value() && optimization.success; }
};

// Stage-typed graph backend installed through the same Loader transaction API.
// Bound IDs preserve sharing. Expression rewrites cannot replace control nodes.
template<class IR> class NodeRewriteSystem final : public KernelConsumer {
    struct Rule { Kernel kernel; nodeir::Pattern pattern, replacement; };
    std::vector<Rule> rules_;
    class Batch final : public Installation {
        NodeRewriteSystem& owner_;
        std::vector<Rule> rules_;
        bool finished_ = false;
    public:
        explicit Batch(NodeRewriteSystem& owner) : owner_(owner), rules_(owner.rules_) {}
        bool add(const Kernel& kernel, Diagnostics& diagnostics) override {
            try {
                if (finished_) throw std::runtime_error("installation already committed");
                if (kernel.preconditions.empty() || kernel.forbids.empty() || kernel.requirements.empty())
                    throw std::runtime_error("incomplete NodeIR applicability contract");
                if (kernel.stage != detail::node_stage(IR::stage)) throw std::runtime_error("kernel stage differs from NodeIR stage");
                const auto& transform = kernel.transformation;
                if (transform.kind != ExprKind::list || transform.items.size() != 3 || !transform.items[0].symbol("rewrite"))
                    throw std::runtime_error("NodeIR backend requires rewrite(pattern, result); other verbs need a host implementation");
                auto pattern = compile_node_pattern(transform.items[1]);
                auto replacement = compile_node_pattern(transform.items[2]);
                if (pattern.variable || pattern.kind != nodeir::Kind::operation) throw std::runtime_error("NodeIR pattern must match an operation");
                std::set<std::string> input_variables, output_variables;
                pattern.validate(input_variables); replacement.validate(output_variables);
                for (const auto& variable : output_variables)
                    if (!input_variables.contains(variable)) throw std::runtime_error("unbound NodeIR result variable: " + variable);
                for (const auto& rule : rules_)
                    if (rule.kernel.id == kernel.id) throw std::runtime_error("kernel already installed");
                rules_.push_back({kernel, std::move(pattern), std::move(replacement)});
                return true;
            } catch (const std::exception& error) {
                diagnostics.push_back({"optilisp.nodeir", kernel.id + ": " + error.what(), kernel.location});
                return false;
            }
        }
        bool commit(Diagnostics& diagnostics) override {
            if (finished_) { diagnostics.push_back({"optilisp.nodeir", "installation already committed", {}}); return false; }
            std::sort(rules_.begin(), rules_.end(), [](const Rule& a, const Rule& b) { return a.kernel.id < b.kernel.id; });
            owner_.rules_.swap(rules_); finished_ = true;
            return true;
        }
    };
public:
    std::unique_ptr<Installation> begin_installation(Diagnostics&) override { return std::make_unique<Batch>(*this); }
    std::size_t size() const { return rules_.size(); }
    OptimizeResult optimize(IR& graph, NodeRewriteContext<IR>& context, std::size_t budget = 64,
                            nodeir::Limits limits = {}) const {
        OptimizeResult result;
        try {
            graph.verify(limits);
            IR candidate = graph;
            Facts facts = context.facts;
            std::set<std::string> seen{candidate.fingerprint()};
            std::size_t rewrites = 0;
            bool changed;
            do {
                changed = false;
                for (auto id : candidate.reachable()) {
                    for (const auto& rule : rules_) {
                        const auto& kernel = rule.kernel;
                        if (!context.prove) continue;
                        bool eligible = true;
                        for (const auto& req : kernel.requirements)
                            eligible &= facts.get(req.kind + ":" + req.id) == Fact::proven_true;
                        for (const auto& prior : kernel.prerequisites) eligible &= facts.get("history:" + prior) == Fact::proven_true;
                        for (const auto& prior : kernel.forbidden_prior) eligible &= facts.get("history:" + prior) == Fact::proven_false;
                        if (!eligible) continue;
                        nodeir::Bindings bindings;
                        if (!nodeir::match(rule.pattern, candidate, id, bindings, limits)) continue;
                        IR proposed = candidate;
                        auto replacement = context.materialize
                            ? context.materialize(kernel, rule.replacement, proposed, bindings, id)
                            : nodeir::instantiate(rule.replacement, proposed, bindings, candidate.at(id).metadata.location, limits);
                        (void)proposed.at(replacement); // Reject invalid host result IDs even for no-ops.
                        if (replacement == id) continue;
                        proposed.replace(id, replacement);
                        proposed.verify(limits);
                        auto fingerprint = proposed.fingerprint();
                        if (fingerprint == candidate.fingerprint()) continue;
                        const NodeProofRequest<IR> request{kernel, candidate, proposed, id, replacement, bindings};
                        for (const auto& condition : kernel.preconditions)
                            if (context.prove(request, condition) != Fact::proven_true) { eligible = false; break; }
                        if (!eligible) continue;
                        for (const auto& condition : kernel.forbids)
                            if (context.prove(request, condition) != Fact::proven_false) { eligible = false; break; }
                        if (!eligible) continue;
                        if (++rewrites > budget) throw std::runtime_error("NodeIR rewrite budget exhausted; changes discarded");
                        if (!seen.insert(std::move(fingerprint)).second) throw std::runtime_error("NodeIR rewrite cycle; changes discarded");
                        candidate = std::move(proposed);
                        facts.invalidate(kernel.analyses);
                        facts.set("history:" + kernel.id, Fact::proven_true);
                        changed = true; break;
                    }
                    if (changed) break;
                }
            } while (changed);
            graph = std::move(candidate); context.facts = std::move(facts);
            result.success = true; result.rewrites = rewrites;
        } catch (const std::exception& error) {
            result.diagnostics.push_back({"optilisp.nodeir", error.what(), {}});
        }
        return result;
    }
    // Full import/optimize/export transaction. Failed export also discards facts;
    // the source IR is const and the returned value is a newly owned external IR.
    template<class External, class Importer, class Exporter>
    TranslatedOptimizeResult<External> optimize_translated(const External& source, NodeRewriteContext<IR>& context,
                                                          Importer&& importer, Exporter&& exporter,
                                                          std::size_t budget = 64, nodeir::Limits limits = {}) const {
        auto imported = nodeir::translate_to<IR>(source, std::forward<Importer>(importer), limits);
        auto failure = [](const std::vector<std::string>& messages) {
            TranslatedOptimizeResult<External> result;
            for (const auto& message : messages) result.optimization.diagnostics.push_back({"optilisp.translation", message, {}});
            return result;
        };
        if (!imported.ok()) return failure(imported.diagnostics);
        auto proposed_context = context;
        auto result = optimize(*imported.value, proposed_context, budget, limits);
        if (!result.success) return {std::nullopt, std::move(result)};
        auto exported = nodeir::translate_from<External>(*imported.value, std::forward<Exporter>(exporter), limits);
        if (!exported.ok()) return failure(exported.diagnostics);
        context.facts = std::move(proposed_context.facts);
        return {std::move(exported.value), std::move(result)};
    }
};

} // namespace mlvm::optilisp
