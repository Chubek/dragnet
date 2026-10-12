#include "../../tests/test.hpp"
#include <mlvm/NodeIR/NodeHIR.hpp>
#include <mlvm/NodeIR/NodeMIR.hpp>
#include <mlvm/NodeIR/NodeLIR.hpp>
#include <mlvm/optilisp/rewrite.hpp>

namespace n = mlvm::nodeir;
namespace o = mlvm::optilisp;

static o::Expr expr(const std::string& text) {
    auto parsed = o::parse(text); CHECK(parsed.ok()); CHECK(parsed.forms.size() == 1);
    return parsed.forms[0];
}
template<class F> static bool rejects(F&& action) {
    try { action(); return false; } catch (const std::exception&) { return true; }
}
static o::Kernel kernel(const o::LoadResult& catalog, const std::string& suffix) {
    for (const auto& k : catalog.kernels) if (k.id.ends_with(suffix)) return k;
    throw std::runtime_error("missing fixture kernel");
}
template<class IR> static o::NodeRewriteContext<IR> proof_context(const o::Kernel& k) {
    o::NodeRewriteContext<IR> context;
    for (const auto& req : k.requirements) context.facts.set(req.kind + ":" + req.id, o::Fact::proven_true);
    context.prove = [](const o::NodeProofRequest<IR>& request, std::string_view condition) {
        for (const auto& forbidden : request.kernel.forbids) if (forbidden == condition) return o::Fact::proven_false;
        return o::Fact::proven_true; // Synthetic provider; not a semantic proof.
    };
    return context;
}

// Toy foreign SSA IR: one value and a redundant and-of-the-same-definition.
// Import/export preserve the width, value and provenance rather than guessing them.
struct Foreign {
    unsigned width;
    std::string value;
    bool redundant_and;
    bool operator==(const Foreign&) const = default;
};
static n::NodeHIR import_foreign(const Foreign& source) {
    if (source.width != 8) throw std::runtime_error("toy adapter supports i8 only");
    n::NodeHIR graph;
    n::Metadata metadata;
    metadata.type = "i8"; metadata.effects = n::Effects::pure;
    metadata.location = {"foreign.ir", 12, 4};
    auto value = graph.literal(n::Kind::symbol, source.value, metadata);
    graph.roots({source.redundant_and ? graph.operation("and", {value, value}, metadata) : value});
    return graph;
}
static Foreign export_foreign(const n::NodeHIR& graph) {
    if (graph.roots().size() != 1) throw std::runtime_error("toy adapter requires one root");
    const auto& root = graph.at(graph.roots()[0]);
    if (root.metadata.type != "i8" || root.metadata.effects != n::Effects::pure) throw std::runtime_error("unsupported foreign type/effects");
    if (root.kind == n::Kind::symbol) return {8, root.value, false};
    if (root.value != "and" || root.operands.size() != 2 || root.operands[0] != root.operands[1])
        throw std::runtime_error("unsupported foreign operation");
    return {8, graph.at(root.operands[0]).value, true};
}

int main() { return test_main([] {
    static_assert(!std::is_same_v<n::NodeHIR, n::NodeMIR>);
    static_assert(!std::is_same_v<n::NodeMIR, n::NodeLIR>);
    n::NodeHIR graph;
    auto x = graph.literal(n::Kind::symbol, "x");
    auto distinct_x = graph.literal(n::Kind::symbol, "x");
    auto self = graph.operation("and", {x, x});
    auto distinct = graph.operation("and", {x, distinct_x});
    graph.roots({self, distinct}); graph.verify();
    auto pattern = o::compile_node_pattern(expr("(and x x)"));
    n::Bindings bindings;
    CHECK(n::match(pattern, graph, self, bindings)); CHECK(bindings.at("x") == x);
    bindings.clear(); CHECK(!n::match(pattern, graph, distinct, bindings)); CHECK(bindings.empty());
    auto keyword = graph.literal(n::Kind::keyword, ":kind");
    auto name = graph.literal(n::Kind::string, "ADD");
    auto machine = graph.operation("machine-op", {keyword, name});
    CHECK(n::match(o::compile_node_pattern(expr("(machine-op :kind \"ADD\")")), graph, machine, bindings));
    CHECK(!n::match(o::compile_node_pattern(expr("(machine-op :kind \"SUB\")")), graph, machine, bindings));
    CHECK(rejects([&] { (void)o::compile_node_pattern(expr("(42 x)")); }));
    CHECK(rejects([&] { (void)o::compile_node_pattern(expr("(f (g x))"), {100, 1, 100}); }));
    const auto snapshot = graph;
    CHECK(rejects([&] { (void)n::instantiate(n::Pattern::bind("unbound"), graph, {}); }));
    CHECK(graph == snapshot);
    auto broken = graph; broken.at(self).operands.push_back(9999);
    CHECK(rejects([&] { broken.verify(); }));
    broken = graph; broken.at(x).id = 900;
    CHECK(rejects([&] { broken.verify(); }));
    broken = graph; broken.at(x).operands = {self};
    CHECK(rejects([&] { broken.verify(); }));

    // Control and data cycles retain graph identity through adapters and traversal.
    n::NodeMIR cyclic;
    auto block = cyclic.operation("block"), phi = cyclic.operation("phi");
    cyclic.at(block).successors = {block}; cyclic.at(phi).operands = {phi};
    cyclic.roots({block, phi}); cyclic.verify(); CHECK(cyclic.reachable().size() == 2);
    auto copied = n::translate_to<n::NodeMIR>(cyclic, [](const auto& source) { return source; });
    CHECK(copied.ok()); CHECK(*copied.value == cyclic);
    CHECK(rejects([&] { cyclic.replace(block, phi); }));

    const std::filesystem::path root = MLVM_SOURCE_ROOT;
    auto registry = o::load_registry(root / "libstone/blackstone/optilisp/stdlib"); CHECK(registry.ok());
    o::Loader loader(registry.registry);
    auto catalog = loader.load_directory(root / "tools/blackstone/kernels"); CHECK(catalog.ok());
    auto and_self = kernel(catalog, "and-self");
    o::NodeRewriteSystem<n::NodeHIR> system;
    o::Diagnostics diagnostics;
    CHECK(loader.install({{and_self}, {}}, system, diagnostics)); CHECK(system.size() == 1);
    auto input = import_foreign({8, "v", true});
    // Two uses of one definition must change together without copying the value.
    auto old_root = input.roots()[0];
    auto pair = input.operation("pair", {old_root, old_root}); input.roots({pair});
    auto context = proof_context<n::NodeHIR>(and_self);
    auto optimized = system.optimize(input, context);
    CHECK(optimized.success); CHECK(optimized.rewrites == 1);
    CHECK(input.at(pair).operands[0] == input.at(pair).operands[1]);
    auto result_id = input.at(pair).operands[0];
    CHECK(input.at(result_id).value == "v"); CHECK(input.at(result_id).metadata.type == "i8");
    CHECK(input.at(result_id).metadata.location.line == 12);
    CHECK(context.facts.get("analysis:range-analysis") == o::Fact::invalidated);
    for (auto blocked : {o::Fact::proven_false, o::Fact::unknown, o::Fact::not_computed, o::Fact::invalidated}) {
        input = import_foreign({8, "v", true}); const auto before = input;
        context = proof_context<n::NodeHIR>(and_self);
        context.facts.set("capability:typed-expressions", blocked);
        CHECK(system.optimize(input, context).rewrites == 0); CHECK(input == before);
        context = proof_context<n::NodeHIR>(and_self);
        auto provider = context.prove;
        context.prove = [=](const auto& request, std::string_view condition) {
            return condition == request.kernel.preconditions[0] ? blocked : provider(request, condition);
        };
        CHECK(system.optimize(input, context).rewrites == 0); CHECK(input == before);
    }
    input = import_foreign({8, "v", true}); auto before = input;
    context = proof_context<n::NodeHIR>(and_self);
    CHECK(!system.optimize(input, context, 0).success); CHECK(input == before);
    CHECK(context.facts.get("history:" + and_self.id) == o::Fact::not_computed);

    // Constructor rewrites preserve sharing; cycle fingerprint ignores new IDs.
    auto forward = and_self, backward = and_self;
    forward.id = "blackstone.hir.tests.forward"; backward.id = "blackstone.hir.tests.backward";
    forward.transformation = expr("(rewrite (f x) (g x))");
    backward.transformation = expr("(rewrite (g x) (f x))");
    o::NodeRewriteSystem<n::NodeHIR> cycle_system;
    CHECK(loader.install({{forward, backward}, {}}, cycle_system, diagnostics));
    input = {}; x = input.literal(n::Kind::symbol, "v"); input.roots({input.operation("f", {x})});
    before = input; context = proof_context<n::NodeHIR>(forward);
    auto cycle = cycle_system.optimize(input, context); CHECK(!cycle.success); CHECK(input == before);
    CHECK(cycle.diagnostics[0].message.find("cycle") != std::string::npos);
    CHECK(!cycle_system.optimize(input, context, 64, {2, 128, 1024}).success); CHECK(input == before);

    // Stage isolation and failed multi-kernel installation are transactional.
    o::NodeRewriteSystem<n::NodeMIR> mir;
    CHECK(!loader.install({{and_self}, {}}, mir, diagnostics)); CHECK(mir.size() == 0); diagnostics.clear();
    auto bad = and_self; bad.id = "blackstone.hir.tests.unbound"; bad.transformation = expr("(rewrite (f x) missing)");
    o::NodeRewriteSystem<n::NodeHIR> empty;
    CHECK(!loader.install({{and_self, bad}, {}}, empty, diagnostics)); CHECK(empty.size() == 0); diagnostics.clear();
    auto mir_kernel = kernel(catalog, "select-same-value");
    CHECK(loader.install({{mir_kernel}, {}}, mir, diagnostics));
    n::NodeMIR mid;
    auto condition = mid.literal(n::Kind::boolean, "true");
    auto value_a = mid.literal(n::Kind::symbol, "a"), value_b = mid.literal(n::Kind::symbol, "b");
    auto select_a = mid.operation("select", {condition, value_a, value_a});
    auto select_b = mid.operation("select", {condition, value_b, value_b});
    mid.roots({select_a, select_b});
    auto mid_context = proof_context<n::NodeMIR>(mir_kernel);
    CHECK(mir.optimize(mid, mid_context).rewrites == 1);
    CHECK(mid.roots()[0] == value_a); CHECK(mid.roots()[1] == select_b);
    CHECK(mid_context.facts.get("analysis:dominator-analysis") == o::Fact::invalidated);
    o::NodeRewriteSystem<n::NodeLIR> lir;
    auto lir_kernel = kernel(catalog, "population-count-recognition");
    CHECK(loader.install({{lir_kernel}, {}}, lir, diagnostics));
    n::NodeLIR low;
    auto register_value = low.literal(n::Kind::symbol, "r0");
    n::Metadata provenance; provenance.location = {"machine.ir", 9, 2};
    low.roots({low.operation("population-count-sequence", {register_value}, provenance)});
    auto low_context = proof_context<n::NodeLIR>(lir_kernel);
    CHECK(lir.optimize(low, low_context).rewrites == 1);
    const auto& target_node = low.at(low.roots()[0]);
    CHECK(target_node.value == "target-popcount"); CHECK(target_node.operands[0] == register_value);
    CHECK(target_node.metadata.location == provenance.location);
    CHECK(target_node.metadata.effects == n::Effects::unknown); CHECK(target_node.metadata.type.empty());
    low = {}; register_value = low.literal(n::Kind::symbol, "r0");
    low.roots({low.operation("population-count-sequence", {register_value}, provenance)});
    low_context = proof_context<n::NodeLIR>(lir_kernel);
    low_context.materialize = [](const o::Kernel&, const n::Pattern& output, n::NodeLIR& candidate,
                                 const n::Bindings& values, n::NodeId old) {
        auto id = n::instantiate(output, candidate, values, candidate.at(old).metadata.location);
        candidate.at(id).metadata.type = "i32";
        candidate.at(id).metadata.effects = n::Effects::pure;
        return id;
    };
    CHECK(lir.optimize(low, low_context).rewrites == 1);
    CHECK(low.at(low.roots()[0]).metadata.type == "i32");
    before = import_foreign({8, "v", true}); input = before;
    context = proof_context<n::NodeHIR>(and_self);
    context.materialize = [](const auto&, const auto&, auto&, const auto&, auto) -> n::NodeId { return 99999; };
    CHECK(!system.optimize(input, context).success); CHECK(input == before);

    // Bidirectional external translation and complete pipeline rollback on export failure.
    Foreign foreign{8, "foreign-value", true};
    auto imported = n::translate_to<n::NodeHIR>(foreign, import_foreign); CHECK(imported.ok());
    auto exported = n::translate_from<Foreign>(*imported.value, export_foreign); CHECK(exported.ok()); CHECK(*exported.value == foreign);
    CHECK(!n::translate_to<n::NodeHIR>(Foreign{64, "v", true}, import_foreign).ok());
    context = proof_context<n::NodeHIR>(and_self);
    auto translated = system.optimize_translated(foreign, context, import_foreign, export_foreign);
    CHECK(translated.ok()); CHECK(!translated.value->redundant_and); CHECK(translated.value->value == foreign.value);
    CHECK(foreign.redundant_and);
    context = proof_context<n::NodeHIR>(and_self);
    auto failed_export = system.optimize_translated(foreign, context, import_foreign,
        [](const n::NodeHIR&) -> Foreign { throw std::runtime_error("unsupported export"); });
    CHECK(!failed_export.ok()); CHECK(failed_export.optimization.rewrites == 0);
    CHECK(context.facts.get("history:" + and_self.id) == o::Fact::not_computed);
}); }
