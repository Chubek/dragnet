#include <dragscript/engine.hpp>
#include <quickjs.h>
#include <charconv>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace dragscript;
namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string render(const Value& value) {
    switch (value.tag()) {
    case Value::Tag::undefined: return "undefined";
    case Value::Tag::null: return "null";
    case Value::Tag::boolean: return value.boolean() ? "true" : "false";
    case Value::Tag::number: {
        if (std::isnan(value.number())) return "NaN";
        if (std::isinf(value.number())) return value.number() < 0 ? "-Infinity" : "Infinity";
        char buffer[64];
        auto result = std::to_chars(buffer, buffer + sizeof buffer, value.number());
        return std::string(buffer, result.ptr);
    }
    case Value::Tag::string: {
        std::string text;
        for (char16_t unit : value.string()) text += static_cast<char>(unit < 128 ? unit : '?');
        return text;
    }
    }
    return "?";
}
Value reference_value(JSContext* context, JSValue value) {
    if (JS_IsUndefined(value)) return {};
    if (JS_IsNull(value)) return Value::null();
    if (JS_IsBool(value)) return Value(bool(JS_ToBool(context, value)));
    if (JS_IsNumber(value)) {
        double number = 0;
        check(JS_ToFloat64(context, &number, value) == 0, "QuickJS numeric conversion failed");
        return Value(number);
    }
    if (JS_IsString(value)) {
        std::size_t size = 0;
        const auto* units = JS_ToCStringLenUTF16(context, &size, value);
        check(units != nullptr, "QuickJS string conversion failed");
        std::u16string text;
        text.reserve(size);
        for (std::size_t i = 0; i < size; ++i) text.push_back(static_cast<char16_t>(units[i]));
        JS_FreeCStringUTF16(context, units);
        return Value(std::move(text));
    }
    throw std::runtime_error("Oracle returned a value outside the common subset");
}
bool same(const Value& a, const Value& b) {
    if (a.tag() != b.tag()) return false;
    if (a.tag() != Value::Tag::number) return a.strictly_equals(b);
    if (std::isnan(a.number())) return std::isnan(b.number());
    return a.number() == b.number() && (a.number() != 0 || std::signbit(a.number()) == std::signbit(b.number()));
}
// The oracle decides the expected result; both Dragscript backends must agree with it.
void compare(const std::string& source) {
    std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> runtime(JS_NewRuntime(), JS_FreeRuntime);
    check(bool(runtime), "QuickJS runtime allocation failed");
    std::unique_ptr<JSContext, decltype(&JS_FreeContext)> context(JS_NewContext(runtime.get()), JS_FreeContext);
    check(bool(context), "QuickJS context allocation failed");
    auto value = JS_Eval(context.get(), source.c_str(), source.size(), "oracle.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    struct Release { JSContext* context; JSValue value; ~Release() { JS_FreeValue(context, value); } } release{context.get(), value};

    std::vector<std::string> observed;
    for (auto backend : {Backend::dsir, Backend::mlvm_ir}) {
        EngineOptions options; options.backend = backend;
        Engine engine(options);
        auto compiled = engine.compile({"oracle.js", source});
        if (!compiled.ok())
            throw std::runtime_error("Unexpected compile failure: " + source + " (" + compiled.diagnostics.front().code +
                                     ": " + compiled.diagnostics.front().message + ")");
        auto result = engine.run(compiled.script);
        if (!result.ok()) { observed.push_back("runtime:" + result.diagnostics.front().code); continue; }
        observed.push_back("value:" + render(result.value));
    }
    if (observed[0] != observed[1]) throw std::runtime_error("Backend disagreement on: " + source +
        "\n  dsir: " + observed[0] + "\n  tree: " + observed[1]);
    if (JS_IsException(value)) {
        auto exception = JS_GetException(context.get()); // Both engines must fail where the oracle throws.
        JS_FreeValue(context.get(), exception);
        if (observed[0].rfind("compile:", 0) != 0 && observed[0].rfind("runtime:", 0) != 0)
            throw std::runtime_error("Expected a Dragscript failure: " + source);
        return;
    }
    const auto expected = reference_value(context.get(), value);
    if (observed[0] != "value:" + render(expected))
        throw std::runtime_error("Mismatch on: " + source + "\n  expected " + render(expected) + "\n  got " + observed[0]);
}
std::vector<std::string> cases() {
    std::vector<std::string> list = {
        "", ";", "1;", "1; let x;", "1; {}", "1; if(false) {}", "1; if(true) { let x; }",
        // Values, identity and completion.
        "undefined;", "null;", "true;", "false;", "let x; x;", "let x = 1; let y = x; y;",
        "1; 2; 3;", "let a = 1; let b = 2; a + b;", "if (true) { 1; } else { 2; }",
        "if (false) { 1; } else { 2; }", "if (false) { 1; } 3;",
        // Numbers.
        "-0;", "0/0;", "1/0;", "-1/0;", "-4%2;", "1%0;", "Infinity%2;", "2%Infinity;", "-7%3;",
        "9007199254740992+1;", "NaN===NaN;", "null!==undefined;", "-0===0;", "1<=NaN;", "NaN>=1;",
        "1e21*10;", "0.1+0.2;", "1/3*3;", "-1.5;", "(0-0);",
        // Number and string conversions.
        "'' + 0;", "'' + -0;", "'' + 1;", "'' + 1.5;", "'' + 100;", "'' + 0.5;", "'' + 123.456;",
        "'' + 1e21;", "'' + 1e-7;", "'' + 0.000001;", "'' + NaN;", "'' + Infinity;", "'' + -Infinity;",
        "'' + true;", "'' + false;", "'' + null;", "'' + undefined;", "'' + (1/3);", "'' + 1e20;",
        "+'0x10';", "+'0o17';", "+'0b101';", "+'-0x10';", "+'  12  ';", "+'';", "+' ';",
        "+'Infinity';", "+'-Infinity';", "+'1e3';", "+'12abc';", "+'abc';", "+null;", "+true;", "+false;",
        "+undefined;", "1 + 'a';", "'a' + 1;", "1 + null;", "1 + undefined;", "1 + true;",
        "'3' * '4';", "'a' < 'b';", "'a' < 1;", "2 < 10;", "'10' < '9';", "'' < 'a';",
        // Operators and short circuiting.
        "!NaN;", "!Infinity;", "!'';", "![];", "!{};", "false&&missing();", "true||missing();",
        "0??missing();", "null??3;", "undefined??4;", "''??5;", "1&&2;", "0||2;",
        "1 < 2 === true;", "3 % 2 === 1;", "-3 % 2 === -1;",
        // typeof and void.
        "typeof 1;", "typeof 'a';", "typeof true;", "typeof undefined;", "typeof missing;",
        "typeof null;", "typeof ({});", "typeof [];", "void 0;", "void 'x';",
        // Objects, arrays and properties.
        "({}).a;", "({a: 1}).a;", "({a: 1, b: 2}).b;", "({a: 1})['a'];", "({a: 1}).b;",
        "({a: 1}).a = 2;", "let o = {}; o.x = 1; o.x;", "let k = 'x'; let o = {x: 1}; o[k];",
        "({a: 1, b: 2}) === ({a: 1, b: 2});", "let o = {a: 1}; o === o;",         "({}).a.b;", "let o = {a: 1}; let p = o; p.a;",
        "[].length;", "[1, 2, 3].length;", "[1, 2, 3][0];", "[1, 2, 3][2];", "[1, 2, 3][3];",
        "[[1], [2]][1][0];", "let a = []; a[2] = 1; a.length;", "let a = []; a[2] = 1; a[0];",
        "'abc'.length;", "'abc'[0];", "'abc'[9];",
        "let a = ['x']; let v = a[0]; v += 'y'; v;", "let o = {}; o['k'] = 1; o.k;",
        "'' + [];", "'' + [1];", "'' + [1, 2];", "'' + [[1], [2]];", "'' + [null, 1];", "'' + [undefined];",
        "[] + 1;", "[2] + 1;", "[5] < 10;",         // Assignment, update and compound forms.
        "let i = 1; i = 2; i;", "let i = 1; i++; i;", "let i = 1; let a = i++; a + i;",
        "let i = 1; let a = ++i; a + i;", "let i = 5; i--; i;", "let i = 1; i += 2; i;",
        "let s = 'a'; s += 'b'; s;", "let s = 'n='; s += 5; s;", "let s = 'a'; s += 1; s;",
        "let i = 10; i -= 3; i;", "let i = 10; i *= 3; i;", "let i = 10; i /= 4; i;", "let i = 10; i %= 4; i;",
        "let s = 'x'; s++;", "let o = {}; let v = o.a; v++;",
        // Loops and control flow.
        "let i = 0; while (i < 5) { i = i + 1; } i;", "let i = 0; while (i < 0) { i = 1; } i;",
        "let i = 0; do { i = i + 1; } while (i < 3); i;", "let i = 0; do { i = i + 1; } while (false); i;",
        "let s = 0; for (let i = 0; i < 4; i = i + 1) { s = s + i; } s;",
        "let s = 0; for (let i = 0; i < 10; i++) { if (i > 3) break; s = s + i; } s;",
        "let s = 0; for (let i = 0; i < 5; i++) { if (i % 2 === 1) continue; s = s + i; } s;",
        "let n = 0; for (;;) { n = n + 1; if (n > 4) break; } n;",
        "let s = ''; for (const c of 'abc') { s = s + c; } s;",
        "let s = ''; for (const c of '😀a') { s = s + c; } s.length;",
        "let s = 0; for (const x of [1, 2, 3]) { s = s + x; } s;",
        "let n = 0; for (const x of []) { n = 1; } n;",
        "let s = ''; let a = [1, 2, 3]; for (const x of a) { s = s + x; } s;",
        "let t = 0; for (const x of [1, 2]) { if (x === 2) continue; t += x; } t;",
        "for (let i = 0; i < 3; i++) { i; }", "for (let i = 0; i < 0; i++) { i; }", "while (true) { 1; break; }",
        "let x = 'outer'; for (let x = 0; x < 1; x++) {} x;",
        "let n = 0; for (const x of [1]) { n = 1; } n;",
        // Functions, closures and calls.
        "function f(x) { return x * 2; } f(21);", "function f() { return 1; } f();",
        "function f(a) { return a; } f();", "function f(a) { return a; } f(7, 8);",
        "function f() { 42; } f();", "function f() { return; } f();",
        "function f(a, b) { return a + b; } f(1, 2);", "function f(n) { if (n < 2) return 1; return n * f(n - 1); } f(5);",
        "function f() {} let g = f; g === f;", "function f() {} f === f;",
        "function apply(f, x) { return f(x); } function double(x) { return x * 2; } apply(double, 9);",
        "function make() { let n = 0; function next() { n = n + 1; return n; } return next; } let f = make(); f(); f();",
        "function o() { return {a: 1}; } o().a;", "function s() { return 'x'; } s().length;",
        "function f(a) { return a; } f(1) + f(2);",
        // Mixed programs.
        "let a = [1, 2, 3]; let total = 0; for (const x of a) { total = total + x; } total;",
        "let o = {items: [1, 2]}; let n = 0; for (const x of o.items) { n += x; } n;",
        "function count(list) { let n = 0; for (const x of list) { n = n + 1; } return n; } count([]);",
        "function find(a) { for (const x of a) { if (x > 2) return x; } return -1; } find([1, 2, 3, 4]);",
        "let s = 0; for (let i = 0; i < 3; i++) { s = s + i; } '' + s;",
    };
    for (int a = -3; a <= 3; ++a)
        for (int b = -3; b <= 3; ++b)
            for (const char* op : {"+", "-", "*", "/", "%", "<", "<=", ">", ">=", "===", "!=="})
                list.push_back("let a = " + std::to_string(a) + "; let b = " + std::to_string(b) + "; a " + op + " b;");
    return list;
}
} // namespace
int main() {
    try {
        const auto list = cases();
        for (const auto& source : list) compare(source);
        std::cout << list.size() << " supported-subset differential cases passed on both backends\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}