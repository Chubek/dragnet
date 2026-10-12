#include "harness.hpp"
#include "mlvm/lowering.hpp"
#include "semantic/dsir.hpp"

namespace {
// Helpers that damage a well-formed module in one specific way.
// Both helpers walk nested regions, where every semantic operation actually lives.
void retag(mlvm::Module& module, const char* operation, const char* key, const char* value) {
    mlvm::walk(module, [&](mlvm::Operation& op) { if (op.name == operation) op.attributes[key] = value; });
}
void drop_regions(mlvm::Module& module, const char* operation) {
    mlvm::walk(module, [&](mlvm::Operation& op) { if (op.name == operation) op.regions.clear(); });
}
std::optional<detail::LoweredModule> lower_source(const std::string& source, mlvm::DiagnosticEngine& diagnostics) {
    std::vector<Diagnostic> ignored;
    auto program = detail::frontend({"page.ts", source}, EngineOptions{}, CompileOptions{}, ignored);
    if (!program) return std::nullopt;
    if (!detail::validate(*program, ignored)) return std::nullopt;
    return detail::lower(*program, diagnostics);
}
void lowered_module_contract() {
    const std::string source = "function f(x) { let o = {v: x}; if (x > 1) { return o.v; } return -o.v; } f(true);";
    Harness harness(Backend::mlvm_ir);
    auto compiled = harness.engine.compile({"page.ts", source});
    CHECK(compiled.ok());
    const auto text = compiled.script.dump_mlvm();
    for (const char* op : {"ds.object", "ds.property", "ds.if", "ds.return", "ds.unary", "source_begin", "ds.yield"})
        CHECK(text.find(op) != std::string::npos);
    // Provenance is checked on the lowered module, before it is printed: the canonical
    // text is not a source map, and re-parsing it would describe the new buffer.
    std::vector<Diagnostic> ignored;
    auto program = detail::frontend({"page.ts", source}, EngineOptions{}, CompileOptions{}, ignored);
    CHECK(program);
    mlvm::DiagnosticEngine lowering;
    auto lowered = detail::lower(*program, lowering);
    CHECK(lowered);
    CHECK(lowered->text == text);
    bool provenance = false;
    mlvm::walk(lowered->module, [&](const mlvm::Operation& op) {
        if (op.name != "ds.binary") return;
        CHECK(op.location.file_id == lowered->file);
        CHECK(op.location.line == 1);
        CHECK(op.location.column == source.find("x > 1") + 1);
        CHECK(op.attributes.at("source_begin") == std::to_string(source.find("x > 1")));
        CHECK(op.attributes.at("source_end") == std::to_string(source.find("x > 1") + 5));
        CHECK(lowered->context->operation(op.name)->effect == mlvm::Effect::unknown);
        CHECK(!lowered->context->operation(op.name)->terminator);
        provenance = true;
    });
    CHECK(provenance);
    mlvm::Context context;
    mlvm::SourceManager sources;
    mlvm::DiagnosticEngine diagnostics;
    CHECK(dragscript::detail::register_dialect(context, diagnostics));
    auto reparsed = mlvm::parse_ir(context, sources, sources.add("dump", text), diagnostics);
    CHECK(reparsed);
    CHECK(mlvm::verify(*reparsed, diagnostics));
    CHECK(mlvm::print_ir(*reparsed) == text);
    // A copy of the module keeps verifying: the artifact owns its context.
    auto copied = lowered->module; // Copies borrow the same context.
    CHECK(mlvm::verify(copied, diagnostics));
}
void malformed_modules_are_rejected() {
    Harness harness(Backend::mlvm_ir);
    auto compiled = harness.engine.compile({"page.ts", "let x = 1 + 2 * 3; x + 0;"});
    CHECK(compiled.ok());
    mlvm::Context context;
    mlvm::SourceManager sources;
    mlvm::DiagnosticEngine diagnostics;
    CHECK(dragscript::detail::register_dialect(context, diagnostics));
    auto module = mlvm::parse_ir(context, sources, sources.add("dump", compiled.script.dump_mlvm()), diagnostics);
    CHECK(module);
    auto rejects = [&](auto mutate, const char* what, bool rebuild = true) {
        auto broken = *module;
        mutate(broken, context);
        if (rebuild) broken.rebuild_uses();
        if (mlvm::verify(broken, diagnostics)) throw std::runtime_error(std::string("Expected verifier rejection: ") + what);
        diagnostics.clear();
    };
    rejects([](mlvm::Module& m, mlvm::Context&) { m.body.blocks[0].operations[0].attributes.erase("source_begin"); },
            "missing source range");
    rejects([](mlvm::Module& m, mlvm::Context&) {
        m.body.blocks[0].operations[0].attributes["source_begin"] = "9";
        m.body.blocks[0].operations[0].attributes["source_end"] = "2";
    }, "reversed range");
    rejects([](mlvm::Module& m, mlvm::Context& c) { m.values.begin()->second.type = c.intern("i32"); }, "wrong result type");
    rejects([](mlvm::Module& m, mlvm::Context&) {
        m.body.blocks[0].operations[0].regions[0].blocks[0].operations.pop_back();
    }, "missing yield");
    rejects([](mlvm::Module& m, mlvm::Context&) {
        m.body.blocks[0].operations[0].regions[0].blocks[0].operations.back().operands[0] = 0;
    }, "dangling yield operand");
    rejects([](mlvm::Module& m, mlvm::Context&) {
        for (auto& [id, value] : m.values) if (!value.uses.empty()) { value.uses.clear(); return; }
        throw std::runtime_error("no used value to invalidate");
    }, "stale use list", false); // Rebuilding use lists would repair this mutation.
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.binary", "text", "**"); }, "unsupported operator");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.literal", "tag", "object"); }, "unknown value tag");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.identifier", "text", "1bad"); }, "invalid binding spelling");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.declaration", "text", "has space"); }, "binding with a space");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.identifier", "text", "let"); }, "reserved spelling");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.literal", "payload", "12x"); }, "trailing garbage in a number payload");
    // Operations that are absent from this module cannot be damaged; use one that has them.
    std::vector<Diagnostic> ignored;
    auto counters = detail::frontend(
        {"page.ts", "let n = 0; n++; n += 1; for (let i = 0; i < 2; i++) { n = n + i; }"}, EngineOptions{}, CompileOptions{}, ignored);
    CHECK(counters);
    mlvm::DiagnosticEngine counter_diagnostics;
    auto counter_module = detail::lower(*counters, counter_diagnostics);
    CHECK(counter_module);
    auto reject_counter = [&](auto mutate, const char* what) {
        auto broken = counter_module->module;
        mutate(broken);
        broken.rebuild_uses();
        if (mlvm::verify(broken, diagnostics)) throw std::runtime_error(std::string("Expected verifier rejection: ") + what);
        diagnostics.clear();
    };
    reject_counter([](mlvm::Module& m) { retag(m, "ds.update", "increment", "**"); }, "invalid increment operator");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.update", "prefix", "maybe"); }, "malformed prefix flag");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.compound", "operator", "<<="); }, "invalid compound operator");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.for", "test", "maybe"); }, "malformed for flag");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.for", "init", "false"); }, "for regions disagreeing with flags");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.for", "binding_0", "let"); }, "invalid for binding spelling");
    reject_counter([](mlvm::Module& m) { drop_regions(m, "ds.compound"); }, "compound without a target and value");
    reject_counter([](mlvm::Module& m) { drop_regions(m, "ds.update"); }, "update without its target");
    reject_counter([](mlvm::Module& m) { drop_regions(m, "ds.sequence"); }, "sequence with non-declaration children");
    // Literal payload shapes are checked by tag, not trusted as opaque text.
    reject_counter([](mlvm::Module& m) { retag(m, "ds.literal", "payload", ""); }, "number payload without digits");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.literal", "payload", "0x10"); }, "non-decimal number payload");
    reject_counter([](mlvm::Module& m) { retag(m, "ds.literal", "payload", "abc"); }, "string payload without hex units");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.literal", "payload", "0x10"); }, "malformed number payload");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.literal", "payload", "zz"); }, "malformed string payload");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.declaration", "immutable", "maybe"); }, "malformed flag");
    rejects([](mlvm::Module& m, mlvm::Context&) {
        drop_regions(m, "ds.declaration");
        retag(m, "ds.declaration", "immutable", "true"); // A const declaration must carry an initializer.
    }, "const without initializer");
    rejects([](mlvm::Module& m, mlvm::Context&) { drop_regions(m, "ds.sequence"); }, "sequence with a non-declaration");
    rejects([](mlvm::Module& m, mlvm::Context&) { retag(m, "ds.literal", "tag", "symbol"); }, "unsupported value tag");
    rejects([](mlvm::Module& m, mlvm::Context&) { m.body.blocks[0].operations[0].name = "ds.unknown"; }, "unregistered operation");
    // Ordering stays valid: an operation may not consume a later definition.
    auto broken = *module;
    auto& ops = broken.body.blocks[0].operations[0].regions[0].blocks[0].operations;
    std::swap(ops[0], ops[1]);
    broken.rebuild_uses();
    CHECK(!mlvm::verify(broken, diagnostics));
    diagnostics.clear();
    // The untouched module still verifies and prints identically.
    CHECK(mlvm::verify(*module, diagnostics));
    CHECK(mlvm::print_ir(*module) == compiled.script.dump_mlvm());
}
void structural_invariants() {
    mlvm::DiagnosticEngine diagnostics;
    const std::string source = "let a = [1, 2]; for (let i = 0; i < 2; i++) { let v = a[i]; v += 1; a[i] = v; } a.length;";
    const auto lowered = lower_source(source, diagnostics);
    CHECK(lowered);
    CHECK(lowered->module.context->has_dialect("ds"));
    CHECK(lowered->text == mlvm::print_ir(lowered->module));
    std::size_t operations = 0, yields = 0, terms = 0, regions = 0;
    mlvm::walk(lowered->module, [&](const mlvm::Operation& op) {
        ++operations;
        if (op.name == "ds.yield") ++yields;
        if (lowered->module.context->operation(op.name)->terminator) { ++terms; CHECK(op.name == "ds.yield"); }
        regions += op.regions.size();
        for (const auto& region : op.regions) {
            CHECK(region.blocks.size() == 1);
            CHECK(region.blocks[0].arguments.empty());
            CHECK(region.blocks[0].operations.back().name == "ds.yield");
        }
    });
    CHECK(lowered->module.body.blocks.size() == 1);
    // Every child region holds one semantic operation plus its terminating yield,
    // and ds.yield is the only terminator in the dialect.
    CHECK(regions + 1 == yields && terms == yields); // Plus the module body terminator.
    CHECK(operations == regions + yields + 1);       // One semantic op per region, plus the region-free program.
    CHECK(yields > 20);
    // Every semantic operation carries an ordered source range inside the owned buffer.
    mlvm::walk(lowered->module, [&](const mlvm::Operation& op) {
        if (op.name == "ds.yield") return; // Structural terminators carry no provenance.
        const auto begin = std::stoul(op.attributes.at("source_begin"));
        const auto end = std::stoul(op.attributes.at("source_end"));
        CHECK(begin <= end);
        CHECK(end <= source.size());
    });
    // Textual round trip through the real parser keeps the same canonical form.
    mlvm::Context context;
    mlvm::SourceManager sources;
    CHECK(detail::register_dialect(context, diagnostics));
    auto reparsed = mlvm::parse_ir(context, sources, sources.add("roundtrip", lowered->text), diagnostics);
    CHECK(reparsed);
    CHECK(mlvm::print_ir(*reparsed) == lowered->text);
    // Validation gates lowering: a rejected program publishes nothing.
    std::vector<Diagnostic> rejected;
    auto invalid = detail::frontend({"page.ts", "while (true) let x = 1;"}, EngineOptions{}, CompileOptions{}, rejected);
    CHECK(invalid);
    CHECK(!detail::validate(*invalid, rejected));
    CHECK(rejected.front().code == "DS_SCOPE");
    mlvm::DiagnosticEngine clean;
    CHECK(detail::lower(*invalid, clean)); // The lowered form itself is well-formed IR.
    CHECK(!clean.has_error());
}
} // namespace
void lowering_tests() {
    lowered_module_contract();
    malformed_modules_are_rejected();
    structural_invariants();
}