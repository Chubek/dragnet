#include "harness.hpp"

namespace {
void host_calls() {
    // Host effects are observed on this engine, so call order is part of the contract.
    Harness harness;
    std::vector<double>& seen = harness.outcome.numbers;
    CHECK(harness.run("record(1) + record(2);").value.number() == 3);
    CHECK(seen == std::vector<double>({1, 2})); seen.clear();
    CHECK(harness.run("function f(a,b) { return a+b; } f(record(1), record(2));").value.number() == 3);
    CHECK(seen == std::vector<double>({1, 2})); seen.clear();
    CHECK(harness.run("false && record(1); true || record(2); 3 ?? record(3); null ?? record(4);").value.number() == 4);
    CHECK(seen == std::vector<double>({4})); seen.clear();
    // The loop yields the last body completion, so the final value is the last recorded call.
    CHECK(harness.run("record(1); for (const x of [1, 2]) { record(x); }").value.number() == 2);
    CHECK(seen == std::vector<double>({1, 1, 2})); seen.clear();
    const auto outcome = harness.run("log('ready'); 1 + 1;");
    CHECK(outcome.ok && outcome.value.number() == 2);
    CHECK(outcome.logs == std::vector<std::u16string>{u"ready"});
    fails("log([]);", "DS_BOUNDARY"); // Only primitives cross the boundary.
    fails("record('x');", "DS_HOST"); // Host-side tag checks are translated.
    fails("hostfail();", "DS_HOST");
    fails("record();", "DS_ARITY");
    fails("function f() {} record(f);", "DS_BOUNDARY");
    fails("record({});", "DS_BOUNDARY");
    fails("double('x');", "DS_HOST"); // Host-side type errors are translated, never leaked.
}
void registration_rules() {
    Harness harness;
    CHECK(!harness.engine.register_host_function("log", [](auto, auto&) { return HostResult{}; }));
    CHECK(!harness.engine.register_host_function("x.y", [](auto, auto&) { return HostResult{}; }));
    CHECK(!harness.engine.register_host_function("empty", {}));
    CHECK(!harness.engine.register_host_function("true", [](auto, auto&) { return HostResult{}; }));
    CHECK(harness.engine.register_host_function("late", [](auto, auto&) { return HostResult{}; }));
    CHECK(harness.run("typeof log;").value.string() == u"function");
    CHECK(harness.run("typeof late;").value.string() == u"function");
    fails("empty();", "DS_REFERENCE");
    // Host bindings shadowable by script declarations.
    CHECK(value_of("let log = 5; log;").number() == 5);
    CHECK(value_of("let double = 5; double;").number() == 5);
}
void isolation_and_artifacts() {
    Engine engine, other;
    CHECK(engine.register_host_function("page", [](auto, auto&) { return HostResult{Value(7.0), {}}; }));
    auto compiled = engine.compile({"page.ts", "page() + 1;"});
    CHECK(compiled.ok());
    CHECK(engine.run(compiled.script).value.number() == 8);
    CHECK(engine.run(compiled.script).value.number() == 8);
    { Harness probe(Backend::dsir); (void)probe; }
    // No host capability leaks between engines: the other engine rejects the name.
    CHECK(other.run(compiled.script).diagnostics.front().code == "DS_REFERENCE");
    CompiledScript surviving;
    { Engine temporary; surviving = temporary.compile({"page.ts", "'owned';"}).script; }
    CHECK(other.run(surviving).value.string() == u"owned");
    // Each run starts from a fresh script environment.
    auto counter = engine.compile({"page.ts", "let x = 0; x = x + 1; x;"});
    CHECK(engine.run(counter.script).value.number() == 1);
    CHECK(engine.run(counter.script).value.number() == 1);
    // Objects do not survive as artifacts, and mutable state is per run.
    CHECK(engine.run(engine.compile({"page.ts", "let a = {n: 1}; a.n;"}).script).value.number() == 1);
}
void reentrancy_and_lifecycle() {
    Harness harness;
    CHECK(harness.engine.register_host_function("reenter", [&](auto, auto&) {
        auto failed = harness.engine.compile({"x.ts", "2;"});
        CHECK(failed.diagnostics.front().code == "DS_REENTRANT");
        CHECK(harness.engine.run({}).diagnostics.front().code == "DS_REENTRANT"); // Reentrancy is checked before the artifact.
        CHECK(!harness.engine.register_host_function("late", [](auto, auto&) { return HostResult{}; }));
        return HostResult{Value(3.0), {}};
    }));
    CHECK(harness.run("reenter();").value.number() == 3);
    CHECK(!harness.engine.run({}).ok());
    const auto cancelled = harness.run("1;");
    CHECK(cancelled.ok);
}
void compile_only() {
    Harness harness;
    auto compiled = harness.engine.compile({"page.ts", "log('never');"});
    CHECK(compiled.ok() && compiled.script.valid());
    CHECK(harness.outcome.logs.empty()); // Compilation performs no observable work.
    CHECK(compiled.script.dump_dsir().find("call") != std::string::npos);
    CHECK(compiled.script.dump_mlvm().find("ds.call") != std::string::npos);
    auto failed = harness.engine.compile({"page.ts", "while (true) let x = 1;"});
    CHECK(!failed.ok() && !failed.script.valid());
    CHECK(harness.outcome.logs.empty());
}
} // namespace
void embedding_tests() {
    host_calls();
    registration_rules();
    isolation_and_artifacts();
    reentrancy_and_lifecycle();
    compile_only();
}