#include "../test.hpp"
#include <mlvm/mlvm.hpp>

int main() { return test_main([] {
    mlvm::Context context; mlvm::DiagnosticEngine diagnostics;
    for (auto dialect : {"high", "mid", "low"}) {
        CHECK(context.register_dialect(dialect, diagnostics));
        CHECK(context.register_operation({std::string(dialect) + ".op"}, diagnostics));
    }
    mlvm::Module module(context); auto& block = module.add_block(module.body); module.append(block, "high.op");
    mlvm::ConversionGraph graph;
    CHECK(graph.add({"high", "mid", [](auto& op, auto&, auto&) { op.name = "mid.op"; return true; }}));
    CHECK(graph.add({"mid", "low", [](auto& op, auto&, auto&) { op.name = "low.op"; return true; }}));
    CHECK(graph.path("high", "low")->size() == 2); CHECK(!graph.path("low", "high"));
    CHECK(graph.convert(module, {"low"}, diagnostics)); CHECK(module.body.blocks[0].operations[0].name == "low.op");
    auto snapshot = mlvm::print_ir(module);
    CHECK(!graph.convert(module, {"high"}, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot); diagnostics.clear();
    mlvm::ConversionGraph bad;
    CHECK(bad.add({"low", "high", [](auto& op, auto&, auto&) { op.name = "high.invalid"; return true; }}));
    CHECK(!bad.convert(module, {"high"}, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot);
    diagnostics.clear();
    mlvm::ConversionGraph error_graph;
    CHECK(error_graph.add({"low", "high", [](auto& op, auto&, auto& d) { op.name = "high.op"; d.error("test", "failed conversion"); return true; }}));
    CHECK(!error_graph.convert(module, {"high"}, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot);
}); }
