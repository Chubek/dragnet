#include "../test.hpp"
#include <mlvm/mlvm.hpp>

int main() { return test_main([] {
    mlvm::Context context; mlvm::DiagnosticEngine diagnostics; mlvm::register_builtin_dialects(context, diagnostics);
    mlvm::Module module(context); auto& block = module.add_block(module.body);
    const auto i8 = context.intern("i8");
    auto& a = module.append(block, "arith.constant", {}, std::span(&i8, 1)); a.attributes["value"] = "255"; const auto av = a.results[0];
    auto& b = module.append(block, "arith.constant", {}, std::span(&i8, 1)); b.attributes["value"] = "2"; const auto bv = b.results[0];
    const mlvm::ValueId args[] = {av, bv}; module.append(block, "arith.addi", args, std::span(&i8, 1));
    CHECK(mlvm::verify(module, diagnostics));
    mlvm::PassManager manager;
    int produced = 0, events = 0;
    CHECK(manager.analyses.register_analysis("count", [&](const mlvm::Module& m) -> std::any { ++produced; return m.values.size(); }));
    manager.add({"analysis", {}, {}, {"count"}, {"count"}, [](auto& m, auto& a, auto&) {
        return std::any_cast<std::size_t>(*a.get("count", m)) == 3;
    }});
    manager.add({"preserved-analysis", {}, {}, {"count"}, {}, [](auto&, auto&, auto&) { return true; }});
    manager.add(mlvm::constant_fold_pass());
    manager.dump_ir = true;
    manager.instrumentation = [&](const mlvm::PassEvent& event) { CHECK(event.success); CHECK(!event.before.empty()); CHECK(!event.after.empty()); ++events; };
    CHECK(manager.run(module, diagnostics)); CHECK(produced == 1); CHECK(events == 3);
    CHECK(module.body.blocks[0].operations.back().attributes.at("value") == "1");
    CHECK(module.body.blocks[0].operations.back().operands.empty());
    const auto snapshot = mlvm::print_ir(module);
    for (bool return_success : {true, false}) {
        mlvm::PassManager failing;
        failing.add({"bad", {}, {}, {}, {}, [return_success](auto& m, auto&, auto&) { m.body.blocks[0].operations[0].name = "illegal.op"; return return_success; }});
        CHECK(!failing.run(module, diagnostics)); CHECK(mlvm::print_ir(module) == snapshot); diagnostics.clear();
    }
    mlvm::PassManager error_pass;
    error_pass.add({"reported-error", {}, {}, {}, {}, [](auto&, auto&, auto& d) { d.error("test", "failure"); return true; }});
    CHECK(!error_pass.run(module, diagnostics)); diagnostics.clear();
    mlvm::PassManager dce; dce.add(mlvm::dead_code_elimination_pass()); CHECK(dce.run(module, diagnostics)); CHECK(module.values.empty());
    // Unknown effects are retained even when all results are dead.
    CHECK(context.register_dialect("io", diagnostics));
    CHECK(context.register_operation({"io.write", 0, 0, 0, 0, 0, 0, mlvm::Effect::write}, diagnostics));
    module.append(module.body.blocks[0], "io.write"); CHECK(dce.run(module, diagnostics)); CHECK(module.body.blocks[0].operations.size() == 1);
    // Extended arithmetic folds; undefined division is left unfolded.
    {
        mlvm::Module m(context); auto& blk = m.add_block(m.body);
        const auto i32 = context.intern("i32");
        auto& x = m.append(blk, "arith.constant", {}, std::span(&i32, 1)); x.attributes["value"] = "20"; const auto xv = x.results[0];
        auto& y = m.append(blk, "arith.constant", {}, std::span(&i32, 1)); y.attributes["value"] = "6"; const auto yv = y.results[0];
        const mlvm::ValueId div_args[] = {xv, yv};
        m.append(blk, "arith.divsi", div_args, std::span(&i32, 1));
        m.append(blk, "arith.remsi", div_args, std::span(&i32, 1));
        m.append(blk, "arith.and", div_args, std::span(&i32, 1));
        CHECK(mlvm::verify(m, diagnostics));
        mlvm::PassManager fold; fold.add(mlvm::constant_fold_pass()); CHECK(fold.run(m, diagnostics));
        const auto& folded = m.body.blocks[0].operations;
        CHECK(folded[2].name == "arith.constant"); CHECK(folded[2].attributes.at("value") == "3");
        CHECK(folded[3].name == "arith.constant"); CHECK(folded[3].attributes.at("value") == "2");
        CHECK(folded[4].name == "arith.constant"); CHECK(folded[4].attributes.at("value") == "4");
        mlvm::Module z(context); auto& zblk = z.add_block(z.body);
        auto& one = z.append(zblk, "arith.constant", {}, std::span(&i32, 1)); one.attributes["value"] = "1"; const auto onev = one.results[0];
        auto& zero = z.append(zblk, "arith.constant", {}, std::span(&i32, 1)); zero.attributes["value"] = "0"; const auto zerov = zero.results[0];
        const mlvm::ValueId zargs[] = {onev, zerov};
        z.append(zblk, "arith.divsi", zargs, std::span(&i32, 1));
        mlvm::PassManager zfold; zfold.add(mlvm::constant_fold_pass()); CHECK(zfold.run(z, diagnostics));
        CHECK(z.body.blocks[0].operations[2].name == "arith.divsi");
    }
    // Canonicalization removes identity operations.
    {
        mlvm::Module m(context); auto& blk = m.add_block(m.body);
        const auto i32 = context.intern("i32");
        auto& zero = m.append(blk, "arith.constant", {}, std::span(&i32, 1)); zero.attributes["value"] = "0"; const auto zerov = zero.results[0];
        auto& one = m.append(blk, "arith.constant", {}, std::span(&i32, 1)); one.attributes["value"] = "1"; const auto onev = one.results[0];
        auto& x = m.append(blk, "arith.constant", {}, std::span(&i32, 1)); x.attributes["value"] = "41"; const auto xv = x.results[0];
        const mlvm::ValueId add_args[] = {xv, zerov};
        m.append(blk, "arith.addi", add_args, std::span(&i32, 1));
        const auto add_id = m.body.blocks[0].operations.back().results[0];
        const mlvm::ValueId mul_args[] = {add_id, onev};
        m.append(blk, "arith.muli", mul_args, std::span(&i32, 1));
        mlvm::PassManager canon; canon.add(mlvm::canonicalize_pass()); CHECK(canon.run(m, diagnostics));
        for (const auto& op : m.body.blocks[0].operations) CHECK(op.name == "arith.constant");
    }
}); }
