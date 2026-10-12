#include "../../tests/test.hpp"
#include <mlvm/optilisp/rewrite.hpp>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace mlvm::optilisp;
namespace fs = std::filesystem;
static const fs::path root = MLVM_SOURCE_ROOT;
static Expr expression(std::string_view source) {
    auto result = parse(source);
    CHECK(result.ok()); CHECK(result.forms.size() == 1);
    return result.forms[0];
}
static RewriteContext context(const Kernel& kernel) {
    RewriteContext result;
    result.stage = kernel.stage;
    for (const auto& req : kernel.requirements) result.facts.set(req.kind + ":" + req.id, Fact::proven_true);
    // Synthetic test provider: semantic proofs are supplied by a host, not inferred.
    result.prove = [](const ProofRequest& request, std::string_view condition) {
        if (std::find(request.kernel.forbids.begin(), request.kernel.forbids.end(), condition) != request.kernel.forbids.end()) return Fact::proven_false;
        return Fact::proven_true;
    };
    return result;
}
static bool diagnostic(const Diagnostics& diagnostics, std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) { return d.code == code; });
}

int main() { return test_main([] {
    // sfsexp reader: locations, comments, typed literals, escapes and multiple forms.
    auto parsed = parse("; comment\n(foo :id \"line\\nquote\\\" slash\\\\ nul\\0\" -7 true nil) ()", "sample.olisp");
    CHECK(parsed.ok()); CHECK(parsed.forms.size() == 2);
    CHECK(parsed.forms[0].location.line == 2);
    CHECK(parsed.forms[0].items[2].kind == ExprKind::string);
    CHECK(parsed.forms[0].items[2].value.find('\n') != std::string::npos);
    CHECK(parsed.forms[0].items[2].value.back() == '\0');
    CHECK(expression(parsed.forms[0].print()) == parsed.forms[0]);
    CHECK(expression("1e1000").kind == ExprKind::number); // Text retained; no host arithmetic.
    CHECK(expression(std::string(10000, '1')).kind == ExprKind::number);
    for (const auto* bad : {"(", ")", "\"bad\\q\"", "\"unterminated", "'quoted", "#b#3#abc", "foo\"bar", ":"}) {
        auto result = parse(bad); CHECK(!result.ok()); CHECK(result.forms.empty());
    }
    CHECK(!parse(std::string(129, '(') + std::string(129, ')')).ok());
    CHECK(!parse("(a b)", "bounded", {100, 10, 2}).ok());
    CHECK(!parse(std::string("a\0b", 3)).ok());
    CHECK(!parse("123", "bounded", {2, 10, 10}).ok());
    // Independent calls are safe despite sfsexp global state.
    std::vector<std::thread> threads;
    std::vector<int> outcomes(8, 0);
    for (std::size_t i = 0; i < outcomes.size(); ++i) threads.emplace_back([&, i] {
        outcomes[i] = parse("(a (b \"c\"))").ok() ? 1 : 0;
    });
    for (auto& thread : threads) thread.join();
    CHECK(std::all_of(outcomes.begin(), outcomes.end(), [](int value) { return value == 1; }));

    auto registry = load_registry(root / "libstone/blackstone/optilisp/stdlib");
    for (const auto& d : registry.diagnostics) std::cerr << d.render() << '\n';
    CHECK(registry.ok()); CHECK(registry.registry.capabilities.size() == 113); CHECK(registry.registry.analyses.size() == 22);
    Loader loader(registry.registry);
    auto supplement = loader.load_directory(root / "tools/blackstone/kernels");
    for (const auto& d : supplement.diagnostics) std::cerr << d.render() << '\n';
    CHECK(supplement.ok()); CHECK(supplement.kernels.size() == 50);
    CHECK(std::is_sorted(supplement.kernels.begin(), supplement.kernels.end(), [](const Kernel& a, const Kernel& b) { return a.id < b.id; }));
    auto upstream = loader.load_directory(root / "libstone/blackstone/kernels");
    CHECK(!upstream.ok()); CHECK(upstream.kernels.empty()); CHECK(diagnostic(upstream.diagnostics, "optilisp.reference"));
    CHECK(!loader.load_directory(root / "optilisp/nonexistent").ok());
    auto and_kernel = *std::find_if(supplement.kernels.begin(), supplement.kernels.end(), [](const Kernel& k) { return k.id.ends_with("and-all-ones"); });
    LoadResult selected{{and_kernel}, {}};
    RewriteSystem system;
    Diagnostics diagnostics;
    CHECK(loader.install(selected, system, diagnostics)); CHECK(system.kernels().size() == 1);
    CHECK(!loader.install(selected, system, diagnostics)); CHECK(system.kernels().size() == 1); diagnostics.clear();
    auto input = expression("(and (value y) 255)");
    auto state = context(and_kernel);
    auto applied = system.optimize(input, state);
    CHECK(applied.success); CHECK(applied.rewrites == 1); CHECK(input == expression("(value y)"));
    CHECK(state.facts.get("analysis:range-analysis") == Fact::invalidated);
    CHECK(state.facts.get("history:" + and_kernel.id) == Fact::proven_true);
    for (Fact blocked : {Fact::proven_false, Fact::unknown, Fact::not_computed, Fact::invalidated}) {
        input = expression("(and x 255)"); state = context(and_kernel);
        state.facts.set("capability:typed-expressions", blocked);
        auto result = system.optimize(input, state);
        CHECK(result.success); CHECK(!result.rewrites); CHECK(input == expression("(and x 255)"));
        state = context(and_kernel);
        const auto provider = state.prove;
        state.prove = [=](const ProofRequest& req, std::string_view condition) {
            return condition == req.kernel.preconditions[0] ? blocked : provider(req, condition);
        };
        CHECK(system.optimize(input, state).rewrites == 0);
    }
    for (Fact blocked : {Fact::proven_true, Fact::unknown, Fact::not_computed, Fact::invalidated}) {
        input = expression("(and x 255)"); state = context(and_kernel);
        const auto provider = state.prove;
        state.prove = [=](const ProofRequest& req, std::string_view condition) {
            return condition == req.kernel.forbids[0] ? blocked : provider(req, condition);
        };
        CHECK(system.optimize(input, state).rewrites == 0);
    }
    input = expression("(and x 255)"); state = context(and_kernel); state.stage = Stage::lir;
    CHECK(system.optimize(input, state).rewrites == 0);
    state = context(and_kernel); state.prove = {};
    CHECK(system.optimize(input, state).rewrites == 0);
    state = context(and_kernel);
    state.prove = [](const ProofRequest&, std::string_view) -> Fact { throw std::runtime_error("proof failure"); };
    CHECK(!system.optimize(input, state).success); CHECK(input == expression("(and x 255)"));
    state = context(and_kernel);
    CHECK(!system.optimize(input, state, 0).success); CHECK(input == expression("(and x 255)"));
    CHECK(state.facts.get("history:" + and_kernel.id) == Fact::not_computed);

    // Backends receive owned descriptors transactionally; unsupported replacements
    // do not leave earlier members of an installation batch installed.
    RewriteSystem empty;
    auto invalid_output = and_kernel;
    invalid_output.id = "blackstone.hir.tests.unbound";
    invalid_output.transformation = expression("(rewrite (f x) missing)");
    CHECK(!loader.install({{and_kernel, invalid_output}, {}}, empty, diagnostics));
    CHECK(empty.kernels().empty()); diagnostics.clear();
    auto non_rewrite = and_kernel;
    non_rewrite.id = "blackstone.hir.tests.fold";
    non_rewrite.transformation = expression("(fold (integer x) (evaluated x))");
    CHECK(!loader.install({{non_rewrite}, {}}, empty, diagnostics)); diagnostics.clear();

    auto forward = and_kernel, backward = and_kernel;
    forward.id = "blackstone.hir.tests.forward"; backward.id = "blackstone.hir.tests.backward";
    forward.transformation = expression("(rewrite (f x) (g x))");
    backward.transformation = expression("(rewrite (g x) (f x))");
    CHECK(loader.install({{forward, backward}, {}}, empty, diagnostics));
    state = context(forward); input = expression("(f 42)");
    CHECK(!empty.optimize(input, state).success); CHECK(input == expression("(f 42)"));
    CHECK(state.facts.get("history:" + forward.id) == Fact::not_computed);
    forward.prerequisites.insert(backward.id); backward.prerequisites.insert(forward.id);
    RewriteSystem dependencies;
    CHECK(!loader.install({{forward, backward}, {}}, dependencies, diagnostics));
    CHECK(diagnostic(diagnostics, "optilisp.cycle")); diagnostics.clear();

    // Repeated operands must unify; consumed analysis facts cannot remain stale.
    const auto select_kernel = *std::find_if(supplement.kernels.begin(), supplement.kernels.end(), [](const Kernel& k) { return k.id.ends_with("select-same-value"); });
    RewriteSystem selects;
    CHECK(loader.install({{select_kernel}, {}}, selects, diagnostics));
    input = expression("(select true a b)"); state = context(select_kernel);
    CHECK(selects.optimize(input, state).rewrites == 0);
    input = expression("(pair (select true a a) (select true b b))"); state = context(select_kernel);
    CHECK(selects.optimize(input, state).rewrites == 1);
    CHECK(input == expression("(pair a (select true b b))"));
    CHECK(state.facts.get("analysis:dominator-analysis") == Fact::invalidated);

    // Metadata diagnostics test original source (no silent truncation).
    auto source_file = root / "tools/blackstone/kernels/hir/bitwise/and-all-ones.olisp";
    std::ifstream source_stream(source_file);
    const std::string source((std::istreambuf_iterator<char>(source_stream)), {});
    const auto temporary = fs::temp_directory_path() / ("mlvm-optilisp-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".olisp");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove(path, ec); } } cleanup{temporary};
    for (const auto& insertion : {"(category duplicate)", ":id \"duplicate\"", "123", "(unknown-field data)", "(transform (rewrite x x))"}) {
        auto broken = source;
        const auto position = broken.find("  (category");
        broken.insert(position, std::string(insertion) + '\n');
        { std::ofstream output(temporary); output << broken; }
        const fs::path paths[] = {temporary};
        auto result = loader.load(paths);
        CHECK(!result.ok()); CHECK(result.kernels.empty());
    }
    { std::ofstream output(temporary); output << source << source; }
    const fs::path paths[] = {temporary};
    CHECK(!loader.load(paths).ok());
}); }
