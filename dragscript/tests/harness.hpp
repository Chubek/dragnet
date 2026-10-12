#pragma once
#include <dragscript/engine.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace dragscript;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

// NaN-aware, signed-zero-aware equality used for every cross-backend claim.
inline bool same(const Value& a, const Value& b) {
    if (a.tag() != b.tag()) return false;
    if (a.tag() == Value::Tag::number) {
        if (std::isnan(a.number())) return std::isnan(b.number());
        if (a.number() == 0) return b.number() == 0 && std::signbit(a.number()) == std::signbit(b.number());
        return a.number() == b.number();
    }
    return a.strictly_equals(b);
}
inline std::string show(const Value& value) {
    if (value.tag() == Value::Tag::string) {
        std::string text;
        for (char16_t c : value.string()) { text += static_cast<char>(c < 128 ? c : 63); }
        return "\"" + text + "\"";
    }
    return value.display();
}
struct Outcome {
    bool compiled = false;
    bool ok = false;
    Value value;
    std::string code;
    std::size_t trace = 0;
    std::string limit;
    std::uint64_t steps = 0;
    std::size_t heap = 0;
    std::vector<std::u16string> logs;
    std::vector<double> numbers;
};
// One engine plus the fake host capabilities the semantic tests rely on.
struct Harness {
    Engine engine;
    Outcome outcome;
    explicit Harness(Backend backend = Backend::dsir) : Harness(defaults(backend)) {}
    explicit Harness(EngineOptions options) : engine(std::move(options)) { register_hosts(); }
    static EngineOptions defaults(Backend backend) {
        EngineOptions options; options.backend = backend; return options;
    }
    void register_hosts() {
        // log() accepts any primitive and records its display form.
        CHECK(engine.register_host_function("log", [&](std::span<const Value> args, HostContext&) {
            const auto text = args[0].display();
            outcome.logs.emplace_back(text.begin(), text.end());
            return HostResult{};
        }, {1, 1}));
        CHECK(engine.register_host_function("double", [](std::span<const Value> args, HostContext&) {
            return HostResult{Value(args[0].number() * 2), {}};
        }, {1, 1}));
        CHECK(engine.register_host_function("hostfail", [](auto, auto&) -> HostResult { return HostResult{{}, "denied by host"}; }));
        CHECK(engine.register_host_function("big", [](auto, auto&) { return HostResult{Value(std::u16string(8, u'z')), {}}; }));
        CHECK(engine.register_host_function("noop", [](auto, auto&) { return HostResult{}; }));
        CHECK(engine.register_host_function("record", [&](std::span<const Value> args, HostContext&) {
            if (args[0].tag() != Value::Tag::number) return HostResult{{}, "record expects a number"};
            outcome.numbers.push_back(args[0].number()); return HostResult{args[0], {}};
        }, {1, 1}));
    }
    Outcome run(const std::string& text, RunOptions options = {}, std::string name = "test.ts") {
        outcome = Outcome{};
        auto compiled = engine.compile({name, text, SourceKind::infer, "test://page"});
        outcome.compiled = compiled.ok();
        if (!outcome.compiled) { outcome.code = compiled.diagnostics.front().code; return outcome; }
        auto result = engine.run(compiled.script, options);
        outcome.ok = result.ok();
        outcome.value = result.value;
        outcome.steps = result.steps;
        outcome.heap = result.heap_bytes;
        if (!outcome.ok) {
            outcome.code = result.diagnostics.front().code;
            outcome.trace = result.diagnostics.front().trace.size();
            outcome.limit = result.diagnostics.front().limit;
        }
        return outcome;
    }
};
inline std::string describe(const Outcome& outcome) {
    return std::string(outcome.compiled ? "" : "compile ") + outcome.code + " value=" + show(outcome.value);
}
// Every semantic case runs on both backends and must agree, including failure codes.
inline Outcome agree(const std::string& text, RunOptions options = {}) {
    Harness dsir(Backend::dsir), tree(Backend::mlvm_ir);
    const auto first = dsir.run(text, options), second = tree.run(text, options);
    if (first.compiled != second.compiled) throw std::runtime_error("Backend compile disagreement on: " + text);
    if (first.ok != second.ok || first.code != second.code || first.trace != second.trace || first.limit != second.limit)
        throw std::runtime_error("Backend error disagreement on: " + text + "\n  dsir: " + describe(first) +
                                 "\n  tree: " + describe(second));
    if (first.ok && !same(first.value, second.value))
        throw std::runtime_error("Backend value disagreement on: " + text + "\n  dsir: " + show(first.value) +
                                 "\n  tree: " + show(second.value));
    if (first.logs != second.logs) throw std::runtime_error("Backend host call disagreement on: " + text);
    return first;
}
inline Value value_of(const std::string& text) {
    const auto outcome = agree(text);
    if (!outcome.ok) throw std::runtime_error(describe(outcome) + " on: " + text);
    return outcome.value;
}
// Failure assertion against a specific engine, for per-engine limit configurations.
inline void fails_on(Harness& harness, const std::string& text, std::string_view code, RunOptions options = {}) {
    const auto outcome = harness.run(text, options);
    if (outcome.ok) throw std::runtime_error("Expected " + std::string(code) + " but succeeded on: " + text);
    if (outcome.code != code) throw std::runtime_error("Expected " + std::string(code) + " but got " + describe(outcome) + " on: " + text);
}
inline void fails(const std::string& text, std::string_view code, RunOptions options = {}) {
    const auto outcome = agree(text, options);
    if (outcome.ok) throw std::runtime_error("Expected " + std::string(code) + " but succeeded on: " + text);
    if (outcome.code != code) throw std::runtime_error("Expected " + std::string(code) + " but got " + describe(outcome) + " on: " + text);
}
inline void compile_error(Harness& harness, const std::string& text, std::string_view code, std::string name = "test.ts") {
    auto compiled = harness.engine.compile({name, text, SourceKind::infer, "test://page"});
    if (compiled.ok() || compiled.script.valid() || compiled.diagnostics.empty())
        throw std::runtime_error("Expected compile error " + std::string(code) + " on: " + text);
    if (compiled.diagnostics.front().code != code)
        throw std::runtime_error("Expected compile " + std::string(code) + " but got " + compiled.diagnostics.front().code +
                                 " on: " + text);
}
inline void compile_error(const std::string& text, std::string_view code, std::string name = "test.ts") {
    Harness harness; compile_error(harness, text, code, name);
}