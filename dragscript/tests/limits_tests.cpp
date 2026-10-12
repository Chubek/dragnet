#include "harness.hpp"

namespace {
// Every limit is exercised at its exact boundary and one past it, on both backends.
void compile_limits() {
    EngineOptions limits;
    limits.max_source_bytes = 2;
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        limits.backend = backend;
        Harness harness(limits);
        CHECK(harness.engine.compile({"test.ts", "1;"}).ok());
        CHECK(!harness.engine.compile({"test.ts", "1; "}).ok());
        CHECK(harness.run("1;").ok);
    }
    auto limit_code = [&](std::size_t nodes, const std::string& text) {
        EngineOptions options; options.max_nodes = nodes;
        Harness harness(options);
        auto compiled = harness.engine.compile({"test.ts", text});
        return compiled.ok() ? std::string("compiled") : compiled.diagnostics.front().code;
    };
    // "1;" produces program, expression_statement, number and ";" nodes.
    CHECK(limit_code(4, "1;") == "compiled"); // Exactly at the limit.
    CHECK(limit_code(3, "1;") == "DS_LIMIT"); // One over the limit.
    EngineOptions depth; depth.max_compile_depth = 2;
    Harness deep(depth); CHECK(deep.engine.compile({"test.ts", "1;"}).ok());
    depth.max_compile_depth = 1;
    Harness shallower(depth);
    CHECK(shallower.engine.compile({"test.ts", "1;"}).diagnostics.front().code == "DS_LIMIT");
    EngineOptions parser; parser.max_parse_checks = 0;
    Harness no_work(parser);
    CHECK(no_work.engine.compile({"test.ts", "1;"}).diagnostics.front().code == "DS_LIMIT");
    parser.max_parse_checks = 1; // Progress callbacks are granularity dependent; zero is deterministic.
    Harness little_work(parser);
    CHECK(little_work.engine.compile({"test.ts", "1;"}).ok());
    EngineOptions one_diagnostic; one_diagnostic.max_diagnostics = 1;
    Harness single(one_diagnostic);
    CHECK(single.engine.compile({"test.ts", "let x=; let y=; let z=;"}).diagnostics.size() == 1);
    one_diagnostic.max_diagnostics = 0;
    Harness none(one_diagnostic);
    CHECK(none.engine.compile({"test.ts", "let x=;"}).diagnostics.size() == 1);
    EngineOptions strings; strings.max_string_units = 2;
    Harness small_strings(strings);
    CHECK(small_strings.run("'😀';").ok);
    compile_error(small_strings, "'abc';", "DS_LIMIT");
    EngineOptions nesting; nesting.max_compile_depth = 64;
    Harness normal(nesting);
    CHECK(normal.engine.compile({"test.ts", std::string(200, '(') + "1" + std::string(200, ')') + ";"})
              .diagnostics.front().code == "DS_LIMIT");
}
void runtime_budgets() {
    const char* loop = "let n = 0; while (n < 3) { n++; } n;";
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        EngineOptions options; options.backend = backend;
        Harness harness(options);
        RunOptions limited; limited.max_steps = 5;
        fails("function f() { return f(); } f();", "DS_LIMIT", limited);
        const auto baseline = harness.run(loop);
        CHECK(baseline.ok && baseline.steps > 0 && baseline.heap > 0);
        RunOptions exact; exact.max_steps = baseline.steps;
        CHECK(harness.run(loop, exact).ok);
        exact.max_steps -= 1;
        CHECK(!harness.run(loop, exact).ok);
        RunOptions heap; heap.max_heap_bytes = baseline.heap;
        CHECK(harness.run(loop, heap).ok);
        heap.max_heap_bytes -= 1;
        CHECK(!harness.run(loop, heap).ok);
        RunOptions depth; depth.max_evaluation_depth = 512;
        CHECK(harness.run(loop, depth).ok);
        depth.max_evaluation_depth = 1;
        CHECK(!harness.run(loop, depth).ok);
        RunOptions calls; calls.max_call_depth = 1;
        CHECK(harness.run("function f() { return 1; } f();", calls).ok);
        calls.max_call_depth = 0;
        CHECK(!harness.run("function f() { return 1; } f();", calls).ok);
        const auto deep_calls = harness.run("function f(n) { if (n > 0) { return f(n - 1) + 1; } return 0; } f(5);", calls);
        CHECK(!deep_calls.ok && deep_calls.limit == "call_depth");
        const auto allocated = harness.run("let a = [1, 2, 3];");
        CHECK(allocated.ok && allocated.heap > 0);
        RunOptions heap_small; heap_small.max_heap_bytes = allocated.heap;
        CHECK(harness.run("let a = [1, 2, 3];", heap_small).ok);
        heap_small.max_heap_bytes -= 1; // One byte less than the run actually charges.
        CHECK(!harness.run("let a = [1, 2, 3];", heap_small).ok);
    }
}
void data_structure_limits() {
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        EngineOptions options; options.backend = backend;
        options.max_array_length = 4;
        Harness arrays(options);
        CHECK(arrays.run("let a = [1, 2, 3, 4]; a.length;").value.number() == 4);
        fails_on(arrays, "let a = [1, 2, 3, 4, 5];", "DS_LIMIT");
        fails_on(arrays, "let a = []; a[4] = 1;", "DS_LIMIT");
        fails_on(arrays, "let a = []; a[99] = 1;", "DS_LIMIT");
        options = {}; options.backend = backend; options.max_properties_per_object = 2;
        Harness objects(options);
        CHECK(objects.run("let o = {a: 1, b: 2}; o.a;").value.number() == 1);
        fails_on(objects, "let o = {a: 1, b: 2, c: 3};", "DS_LIMIT");
        CHECK(objects.run("let o = {a: 1}; o.a = 2; o.b = 3; o.a;").value.number() == 2);
        options = {}; options.backend = backend; options.max_property_name_units = 4;
        Harness names(options);
        CHECK(names.run("let o = {'abcd': 1}; o.abcd;").value.number() == 1);
        fails_on(names, "let o = {}; o['abcde'] = 1;", "DS_LIMIT");
        CHECK(names.run("let o = {}; o['abcde'];").value.tag() == Value::Tag::undefined); // Reads do not store.
        options = {}; options.backend = backend; options.max_string_units = 3;
        Harness strings(options);
        CHECK(strings.run("'a' + 'b';").value.string() == u"ab");
        CHECK(strings.run("'a' + 'bc';").value.string() == u"abc"); // Exactly at the limit.
        fails_on(strings, "'a' + 'bcd';", "DS_LIMIT"); // One over the limit.
        fails_on(strings, "'abcd';", "DS_LIMIT");
        fails_on(strings, "big();", "DS_LIMIT");
        CHECK(strings.run("'abc';").value.string() == u"abc");
        // A runtime limit applies to an artifact compiled while it was still permissive.
        EngineOptions permissive;
        permissive.backend = backend;
        Harness source(permissive);
        const auto artifact = source.engine.compile({"test.ts", "'abc';"});
        CHECK(artifact.ok());
        options = {}; options.backend = backend; options.max_string_units = 2;
        Harness tightened(options);
        CHECK(tightened.engine.run(artifact.script).diagnostics.front().code == "DS_LIMIT");
        options.max_string_units = 3;
        Harness exact(options);
        CHECK(exact.engine.run(artifact.script).value.string() == u"abc");
    }
}
void cancellation() {
    std::atomic_bool cancelled{true};
    Harness harness;
    auto compiled = harness.engine.compile({"test.ts", "1;"});
    CHECK(compiled.ok());
    CHECK(harness.engine.compile({"cancel.ts", "1;"}, {&cancelled}).diagnostics.front().code == "DS_CANCELLED");
    RunOptions options; options.cancellation = &cancelled;
    CHECK(harness.engine.run(compiled.script, options).diagnostics.front().code == "DS_CANCELLED");
    cancelled = false;
    CHECK(harness.engine.run(compiled.script, options).ok());
    CHECK(harness.engine.register_host_function("stop", [&](auto, auto&) { cancelled = true; return HostResult{}; }));
    fails_on(harness, "stop(); 99;", "DS_CANCELLED", options);
    cancelled = false;
    CHECK(harness.run("1;").ok);
    CHECK(harness.engine.register_host_function("spend", [](auto, HostContext& context) {
        CHECK(!context.consume_steps(std::numeric_limits<std::uint64_t>::max()));
        return HostResult{};
    }));
    fails_on(harness, "spend();", "DS_LIMIT");
    RunOptions small; small.max_steps = 200;
    const auto outcome = harness.run("while (true) { }", small);
    CHECK(!outcome.ok && outcome.code == "DS_LIMIT" && outcome.limit == "steps");
}
} // namespace
void limits_tests() {
    compile_limits();
    runtime_budgets();
    data_structure_limits();
    cancellation();
}