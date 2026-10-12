#include "harness.hpp"

namespace {
void source_modes() {
    Harness harness;
    CHECK(harness.engine.compile({"page.ts", "let x: number = 2; x;"}).ok());
    CHECK(harness.engine.compile({"page.tsx", "let x: number = 2; x;"}).ok());
    CHECK(harness.engine.compile({"page.js", "let x = 2; x;"}).ok());
    CHECK(harness.engine.compile({"eval", "2;", SourceKind::javascript}).ok());
    compile_error(harness, "2;", "DS_MODE", "eval");
    compile_error(harness, "2;", "DS_MODE", "page.jsx");
    EngineOptions options; options.enable_jsx = true;
    Harness jsx(options);
    CHECK(jsx.engine.compile({"page.jsx", "2;"}).ok());
    CHECK(jsx.engine.compile({"page.tsx", "<div/>;"}).diagnostics.front().code == "DS_UNSUPPORTED");
    CHECK(harness.engine.compile({"eval", "2;", static_cast<SourceKind>(999)}).diagnostics.front().code == "DS_MODE");
    compile_error(harness, "let x: number = 1;", "DS_UNSUPPORTED", "page.js");
    compile_error(harness, "(1: number);", "DS_UNSUPPORTED", "page.js");
    CHECK(harness.engine.compile({"page.ts", "(1: number);"}).ok());
    CHECK(harness.engine.compile({"page.ts", "<number>1;"}).ok());
    compile_error(harness, "<number>1;", "DS_SYNTAX", "page.tsx");
    compile_error(harness, "(-3) < (-3);", "DS_SYNTAX");
}
void syntax_errors() {
    Harness harness;
    for (const char* text : {"let x = ;", "function f( {", "let x = [1, 2;", "if (true) {", "for (;;"})
        compile_error(harness, text, "DS_SYNTAX");
    const auto unterminated = harness.engine.compile({"utf.ts", "// café\n  function f() {"});
    CHECK(!unterminated.ok());
    CHECK(unterminated.diagnostics.front().code == "DS_SYNTAX");
    CHECK(unterminated.diagnostics.front().message.find("Missing") == 0); // Reports the missing token at EOF.
    CHECK(unterminated.diagnostics.front().span.begin.line == 2);
    CHECK(unterminated.diagnostics.front().span.begin.byte == 25);
    CHECK(unterminated.diagnostics.front().source_name == "utf.ts");
    CHECK(unterminated.diagnostics.front().origin.empty()); // No origin was supplied here.
    // A construct error keeps its own source position and byte column.
    const auto unsupported = harness.engine.compile({"utf.ts", "// café\n  class C {}"});
    CHECK(unsupported.diagnostics.front().code == "DS_UNSUPPORTED");
    CHECK(unsupported.diagnostics.front().span.begin.line == 2);
    CHECK(unsupported.diagnostics.front().span.begin.column == 3);
    CHECK(unsupported.diagnostics.front().span.begin.byte == 11);
    const auto unicode = harness.engine.compile({"utf.ts", "'é'; class C {}"});
    CHECK(unicode.diagnostics.front().span.begin.column == 7);
    CHECK(harness.engine.compile({"bad.ts", std::string(1, char(0xff))}).diagnostics.front().code == "DS_ENCODING");
    const Source coordinates{"coord.ts", "é\n"};
    CHECK(source_position(coordinates, 2).column == 3);
    CHECK(source_position(coordinates, 3).line == 2 && source_position(coordinates, 3).column == 1);
    CHECK(source_span(coordinates, 0, 3).end.byte == 3);
}
void unsupported_syntax() {
    for (const char* text : {
            "var x = 1;", "x **= 2;", "x &&= 1;", "new F();", "() => 1;",
            "async function f() {}", "function* f() {}", "`hello`;", "import type { X } from 'x';",
            "export type X = number;", "export const x = 1;", "class C {}", "enum E { A }", "namespace N {}",
            "[...[1]];", "[...[1], 2];", "({...o});", "({m(){}});", "({x});", "({get x(){return 1}});",
            "a?.[0];", "a?.b;", "delete a.x;", "'x' in a;", "a instanceof b;", "label: for(;;) {}",
            "outer: { break outer; }", "const [a, b] = c;", "const {a} = o;", "function f(...x) {}",
            "function f(x = 1) {}", "f(...a);", "1n;", "0x10;", "01;", "'\\1';",
            "this;", "function f() { return this; }", "switch (1) {}", "throw 1;", "try {} catch(e) {}",
            "with (o) {}", "debugger;"})
        compile_error(text, "DS_UNSUPPORTED");
    compile_error("for (x in y) {}", "DS_UNSUPPORTED");
    compile_error("a[0]++;", "DS_UNSUPPORTED");
    compile_error("a.x += 1;", "DS_UNSUPPORTED");
}
void semantic_rejections() {
    compile_error("return 1;", "DS_RETURN");
    compile_error("const x;", "DS_DECLARATION");
    compile_error("let x; let x;", "DS_DUPLICATE");
    compile_error("function f(a,a) {}", "DS_DUPLICATE");
    compile_error("function f(a) { let a; }", "DS_DUPLICATE");
    compile_error("if (true) function f() {}", "DS_SCOPE");
    compile_error("while (true) function f() {}", "DS_SCOPE");
    compile_error("while (true) let x = 1;", "DS_SCOPE");
    compile_error("for (;;) function f() {}", "DS_SCOPE");
    compile_error("break;", "DS_SCOPE");
    compile_error("continue;", "DS_SCOPE");
    compile_error("function f() { break; }", "DS_SCOPE");
    compile_error("for (let i, i;;) {}", "DS_DUPLICATE");
    compile_error("while (true) { let x = 1; let x = 2; }", "DS_DUPLICATE");
    CHECK(value_of("for (let i = 0; i < 3; i++) { if (i) { continue; } }").tag() == Value::Tag::undefined);
}
void erasure() {
    CHECK(value_of("type X = number; interface A { x: X } let x: X = 3; (x as X)! satisfies X;").number() == 3);
    CHECK(value_of("function id<T>(x: T): T { return x; } id<number>(7);").number() == 7);
    CHECK(value_of("let a: Array<number> = []; a.push;").tag() == Value::Tag::undefined);
    CHECK(value_of("function f(x?: number) { return x; } f();").tag() == Value::Tag::undefined);
    CHECK(value_of("for (const x of [1] as number[]) { }").tag() == Value::Tag::undefined);
}
void supported_surface() {
    Harness harness;
    const char* supported[] = {"while (true) { break; }", "do { break; } while (true);", "for (;;) { break; }",
        "for (let i = 0; i < 1; i++) {}", "for (const x of [1, 2]) {}", "for (x of [1]) {}",
        "for (;;) continue;", "let o = {a: 1, 'b': 2, 3: 4, ['k']: 5};", "let a = [1, , 3];",
        "a[0] = 1;", "o.a = 1;", "o['a'] = 1;", "let a = []; a[3] = 1;", "let x = 1; ++x;", "let x = 1; x++;",
        "let x = 1; --x;", "let x = 1; x--;", "let x = 1; x += 1;", "let x = 5; x -= 1;", "let x = 2; x *= 3;",
        "let x = 8; x /= 2;", "let x = 8; x %= 3;", "typeof x;", "void x;", "let o = {a: 1}; let k = 'a'; o[k];",
        "for (const c of 'ab') {}", "let a = [1]; a.length;", "let a = [1]; a[0];", "let s = 'ab'; s[1];",
        "let o = {}; o.a.b;", "let a = [[1], [2]];", "function f() { return {a: 1}; } f().a;"};
    for (const char* text : supported) {
        auto compiled = harness.engine.compile({"test.ts", text});
        if (!compiled.ok()) throw std::runtime_error("Expected support for: " + std::string(text) + " (" +
            compiled.diagnostics.front().code + ": " + compiled.diagnostics.front().message + ")");
    }
}
} // namespace
void frontend_tests() {
    source_modes();
    syntax_errors();
    unsupported_syntax();
    semantic_rejections();
    erasure();
    supported_surface();
}