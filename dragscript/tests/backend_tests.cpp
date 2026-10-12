#include "harness.hpp"

namespace {
// Both backends must produce identical observable behavior for a broad battery.
void cross_backend_equivalence() {
    const char* programs[] = {
        "1;", "let x = 1; x + 2;", "'a' + 'b';", "true && false;", "null ?? 'x';",
        "let o = {a: 1, b: [1, 2]}; o.b[1];", "let a = []; for (let i = 0; i < 3; i++) a[i] = i * i; a[2];",
        "let a = []; for (const x of [1, 2, 3]) a[x] = x; a[3];",
        "let total = 0; for (const x of [1, 2, 3]) { total += x; } total;",
        "let n = 0; let out = 0; while (n < 10) { n++; if (n % 3 === 0) continue; if (n > 7) break; out += n; } out;",
        "function fact(n) { let r = 1; for (let i = 2; i <= n; i++) r *= i; return r; } fact(6);",
        "function make() { let n = 0; function bump() { n++; return n; } return bump; } let f = make(); f(); f(); f();",
        "let o = {list: [1, 2, 3]}; o['list'].length;", "typeof o;", "void 1;", "let o = {}; o.missing;",
        "let s = 'x'; let o = {}; o[s] = 1; o.x;", "let a = [[1], [2]]; '' + a;",
        "let i = 0; do { i++; } while (i < 3); i;", "let x = 5; x += 1; x -= 2; x *= 3; x /= 7; x;",
        "function f(a, b) { if (a === undefined) { return b; } return a; } f(undefined, 9);",
        "let a = [1]; let b = a; b[0] = 2; a[0];", "log('side'); 42;",
        "let o = {a: 1}; function f() { return o.a; } f();", "let a = [1, 2]; let s = ''; for (const x of a) s += x; s;",
    };
    for (const char* text : programs) value_of(text);
}
void backend_selection() {
    Harness dsir(Backend::dsir), tree(Backend::mlvm_ir);
    const std::string source = "let o = {a: [1, 2]}; for (const x of o.a) { log(x); } o.a.length;";
    auto first = dsir.engine.compile({"page.ts", source});
    auto second = tree.engine.compile({"page.ts", source});
    CHECK(first.ok() && second.ok());
    CHECK(first.script.dump_dsir() == second.script.dump_dsir());
    CHECK(first.script.dump_mlvm() == second.script.dump_mlvm()); // Lowering is backend independent.
    CHECK(dsir.run(source).value.number() == 2);
    CHECK(tree.run(source).value.number() == 2);
    CHECK((dsir.outcome.logs == std::vector<std::u16string>{u"1", u"2"}));
    CHECK(tree.outcome.logs == dsir.outcome.logs);
    // An artifact compiled under one backend runs under either, in any engine.
    CHECK(tree.engine.run(first.script).value.number() == 2);
    CHECK(dsir.engine.run(second.script).value.number() == 2);
}
void failure_agreement() {
    fails("let o = {}; o.a.b;", "DS_TYPE");
    fails("for (const x of 5) {}", "DS_TYPE");
    fails("while (true) { let x = 1; }", "DS_LIMIT");
    fails("function f() { return f(); } f();", "DS_LIMIT");
    fails("const c = 1; c++;", "DS_CONST");
    fails("let x = 1; let x = 2;", "DS_DUPLICATE");
    fails("missing;", "DS_REFERENCE");
    fails("hostfail();", "DS_HOST");
    fails("'s'.x = 1;", "DS_TYPE");
}
} // namespace
void backend_tests() {
    cross_backend_equivalence();
    backend_selection();
    failure_agreement();
}