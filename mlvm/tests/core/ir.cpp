#include "../test.hpp"
#include <mlvm/mlvm.hpp>

static constexpr auto program = R"(mlvm 1 {
  block() {
    "builtin.func"() ["sym"="answer", "result"="i32"] region {
      block() {
        %a:"i32" = "arith.constant"() ["value"="20"];
        %b:"i32" = "arith.constant"() ["value"="22"];
        %c:"i32" = "arith.addi"(%a, %b) [];
        "func.return"(%c) [];
      }
    };
  }
})";
int main() { return test_main([] {
    mlvm::Context context; mlvm::SourceManager sources; mlvm::DiagnosticEngine diagnostics;
    CHECK(mlvm::register_builtin_dialects(context, diagnostics));
    auto module = mlvm::parse_ir(context, sources, sources.add("test.mlvm", program), diagnostics);
    CHECK(module); CHECK(mlvm::verify(*module, diagnostics));
    const auto canonical = mlvm::print_ir(*module);
    auto again = mlvm::parse_ir(context, sources, sources.add("roundtrip", canonical), diagnostics);
    CHECK(again); CHECK(mlvm::print_ir(*again) == canonical);
    auto& block = module->body.blocks[0].operations[0].regions[0].blocks[0];
    CHECK(module->value(block.operations[0].results[0])->uses.size() == 1);
    CHECK(!module->erase(block, block.operations[0].id, diagnostics)); diagnostics.clear();
    auto broken = *module; broken.values.begin()->second.uses.clear(); CHECK(!mlvm::verify(broken, diagnostics)); diagnostics.clear();
    broken = *module;
    auto& ops = broken.body.blocks[0].operations[0].regions[0].blocks[0].operations;
    std::swap(ops[0], ops[2]); broken.rebuild_uses(); CHECK(!mlvm::verify(broken, diagnostics)); diagnostics.clear();
    broken = *module; broken.body.blocks[0].operations[0].attributes["result"] = "i64";
    CHECK(!mlvm::verify(broken, diagnostics)); diagnostics.clear();
    for (const auto* bad : {"mlvm 2 {}", "mlvm 1 {} \"\"", "mlvm 1 { block() { \"unknown.op\"() []; } }",
                           "mlvm 1 { block() { \"func.return\"() []; } }", "mlvm 1 { block() { %a:\"i32\" = \"arith.constant\"() []; } }"}) {
        CHECK(!mlvm::parse_ir(context, sources, sources.add("bad", bad), diagnostics));
        CHECK(diagnostics.has_error()); diagnostics.clear();
    }
    auto invalid = mlvm::parse_ir(context, sources, sources.add("bad", "mlvm 1 {\nblock(\n}"), diagnostics);
    CHECK(!invalid); CHECK(diagnostics.diagnostics()[0].location.line == 3);
}); }
