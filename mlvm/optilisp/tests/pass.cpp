#include "../../tests/test.hpp"
#include <mlvm/mlvm.hpp>
#include <mlvm/optilisp/pass.hpp>

using namespace mlvm::optilisp;
class TestOptimizer final : public ModuleOptimizer {
public:
    bool supported = true, fail = false, malformed = false;
    Fact condition = Fact::proven_true;
    int calls = 0;
    bool supports(const Kernel&) const override { return supported; }
    Stage stage(const mlvm::Module&) const override { return Stage::hir; }
    Fact fact(std::string_view key, const Kernel& kernel, const mlvm::Module&, mlvm::AnalysisManager&) const override {
        for (const auto& forbidden : kernel.forbids) if (key == "condition:" + forbidden) return Fact::proven_false;
        return condition;
    }
    bool apply(const Kernel&, mlvm::Module& module, mlvm::AnalysisManager&, mlvm::DiagnosticEngine&) override {
        ++calls;
        if (malformed) module.body.blocks[0].operations[0].name = "unknown.op";
        else module.body.blocks[0].operations[0].attributes["value"] = "0";
        return !fail;
    }
};
int main() { return test_main([] {
    const std::filesystem::path root = MLVM_SOURCE_ROOT;
    auto registry = load_registry(root / "libstone/blackstone/optilisp/stdlib"); CHECK(registry.ok());
    const std::filesystem::path paths[] = {root / "tools/blackstone/kernels/hir/bitwise/and-self.olisp"};
    auto loaded = Loader(registry.registry).load(paths); CHECK(loaded.ok());
    mlvm::Context context;
    mlvm::DiagnosticEngine diagnostics;
    CHECK(mlvm::register_builtin_dialects(context, diagnostics));
    mlvm::Module module(context);
    auto& block = module.add_block(module.body);
    const auto i32 = context.intern("i32");
    module.append(block, "arith.constant", {}, std::span(&i32, 1)).attributes["value"] = "42";
    CHECK(mlvm::verify(module, diagnostics));
    auto optimizer = std::make_shared<TestOptimizer>();
    for (Fact blocked : {Fact::unknown, Fact::not_computed, Fact::invalidated, Fact::proven_false}) {
        optimizer->condition = blocked;
        mlvm::PassManager manager;
        manager.add(kernel_pass(loaded.kernels[0], optimizer));
        CHECK(manager.run(module, diagnostics)); CHECK(optimizer->calls == 0);
    }
    optimizer->condition = Fact::proven_true;
    auto snapshot = mlvm::print_ir(module);
    for (bool malformed : {false, true}) {
        optimizer->malformed = malformed; optimizer->fail = !malformed;
        mlvm::PassManager manager;
        manager.add(kernel_pass(loaded.kernels[0], optimizer));
        CHECK(!manager.run(module, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot); diagnostics.clear();
    }
    optimizer->malformed = optimizer->fail = false;
    optimizer->supported = false;
    mlvm::PassManager unsupported;
    unsupported.add(kernel_pass(loaded.kernels[0], optimizer));
    CHECK(!unsupported.run(module, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot); diagnostics.clear();
    optimizer->supported = true;
    mlvm::PassManager manager;
    int computations = 0;
    CHECK(manager.analyses.register_analysis("range-analysis", [&](const mlvm::Module&) -> std::any { ++computations; return 1; }));
    manager.add({"prime", {}, {}, {"range-analysis"}, {"range-analysis"}, [](auto&, auto&, auto&) { return true; }});
    manager.add(kernel_pass(loaded.kernels[0], optimizer));
    manager.add({"recompute", {}, {}, {"range-analysis"}, {}, [](auto&, auto&, auto&) { return true; }});
    CHECK(manager.run(module, diagnostics)); CHECK(computations == 2);
    CHECK(module.body.blocks[0].operations[0].attributes.at("value") == "0");
}); }
