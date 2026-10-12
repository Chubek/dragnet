#include "harness.hpp"

namespace {
// Deterministic mutation sweep: both backends must stay in agreement, stay inside
// their budgets, never report an internal error, and never publish a partial artifact.
void mutation_sweep() {
    EngineOptions limits;
    limits.max_source_bytes = 1024; limits.max_nodes = 512; limits.max_compile_depth = 16;
    limits.max_diagnostics = 2; limits.max_parse_checks = 64; limits.max_string_units = 64;
    limits.max_array_length = 32; limits.max_properties_per_object = 8;
    RunOptions run;
    run.max_steps = 400; run.max_call_depth = 5; run.max_evaluation_depth = 32; run.max_heap_bytes = 16384;
    const char* seeds[] = {"let x=1; x+2;", "function f(x) { return x*2; } f(3);", "let x: number=1; (x as number)!;",
        "'\\ud800';", "if(true) { 1; } else { 2; }", "function f() { return f(); } f();", "let a=[1,2,3]; a[1];",
        "let o={a:1}; o.a=2; o.a;", "while(true) { break; }", "for (let i=0;i<3;i++) { }", "let s=''; for (const c of 'ab') s+=c; s;",
        "let x=1; x+=2; x;", "for (const v of [1]) { }", "let o={}; o['k']=1; o.k;"};
    constexpr std::string_view tokens = "{}();:+-*/%<>=!&|?'\"\\, [].\n012abc";
    std::uint32_t random = 0x5eed1234u;
    auto next = [&] { random = random * 1664525U + 1013904223U; return random; };
    for (unsigned trial = 0; trial < 900; ++trial) {
        auto text = std::string(seeds[next() % std::size(seeds)]);
        for (unsigned edit = 0, count = next() % 8; edit < count; ++edit) {
            const auto position = next() % (text.size() + 1);
            const char token = tokens[next() % tokens.size()];
            if (next() & 1U) text.insert(position, 1, token);
            else if (position < text.size()) text[position] = token;
        }
        EngineOptions for_dsir = limits; for_dsir.backend = Backend::dsir;
        EngineOptions for_tree = limits; for_tree.backend = Backend::mlvm_ir;
        Harness dsir(for_dsir), tree(for_tree);
        const auto first = dsir.run(text, run), second = tree.run(text, run);
        if (first.compiled != second.compiled) throw std::runtime_error("Compile disagreement on: " + text);
        for (const Outcome* outcome : {&first, &second}) {
            CHECK(outcome->steps <= run.max_steps);
            CHECK(outcome->heap <= run.max_heap_bytes);
            if (!outcome->ok) CHECK(outcome->code != "DS_INTERNAL");
        }
        if (first.ok != second.ok || first.code != second.code)
            throw std::runtime_error("Outcome disagreement on: " + text + "\n  dsir: " + describe(first) +
                                     "\n  tree: " + describe(second));
        if (first.ok && !same(first.value, second.value))
            throw std::runtime_error("Value disagreement on: " + text + "\n  dsir: " + show(first.value) +
                                     "\n  tree: " + show(second.value));
        if (first.logs != second.logs) throw std::runtime_error("Host disagreement on: " + text);
        // A failed compile or run must not poison the engine for later requests.
        CHECK(dsir.run("1;").ok && dsir.run("1;").ok);
        CHECK(tree.run("1;").ok && tree.run("1;").ok);
    }
}
} // namespace
void fuzz_smoke_tests() {
    mutation_sweep();
}