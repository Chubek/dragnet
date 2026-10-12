#include "harness.hpp"

namespace {
void numbers_and_strings() {
    CHECK(value_of("1 + 2 * 3 - 4 / 2;").number() == 5);
    CHECK(value_of("null;").tag() == Value::Tag::null);
    CHECK(value_of("undefined;").tag() == Value::Tag::undefined);
    CHECK(value_of("!NaN && !0 && !(-0) && !'' && !null && !undefined;").boolean());
    CHECK(std::isnan(value_of("0/0;").number()));
    CHECK(std::isinf(value_of("1/0;").number()));
    CHECK(std::signbit(value_of("-4 % 2;").number()));
    CHECK(value_of("NaN !== NaN && -0 === 0 && null !== undefined;").boolean());
    CHECK(value_of("'a' + 'b';").string() == u"ab");
    CHECK(value_of("'😀'.length;").number() == 2);
    CHECK(value_of("'😀'[0];").string().size() == 1);
    CHECK(value_of("'\\ud800';").string()[0] == 0xd800);
    CHECK(value_of("'\\x41\\n\\u0042';").string() == u"A\nB");
    CHECK(value_of("'a\\\nb';").string() == u"ab");
    CHECK(value_of("'a\\\r\nb';").string() == u"ab");
    CHECK(value_of("'abc'.length;").number() == 3);
    CHECK(value_of("'abc'[9];").tag() == Value::Tag::undefined);
    CHECK(value_of("9007199254740992 + 1;").number() == 9007199254740992);
}
void number_string_conversions() {
    // Number::toString formatting follows the ECMAScript algorithm.
    CHECK(value_of("'' + 0;").string() == u"0");
    CHECK(value_of("'' + -0;").string() == u"0");
    CHECK(value_of("'' + 1;").string() == u"1");
    CHECK(value_of("'' + 1.5;").string() == u"1.5");
    CHECK(value_of("'' + 100;").string() == u"100");
    CHECK(value_of("'' + 0.5;").string() == u"0.5");
    CHECK(value_of("'' + 1e21;").string() == u"1e+21");
    CHECK(value_of("'' + 1e-7;").string() == u"1e-7");
    CHECK(value_of("'' + 0.000001;").string() == u"0.000001");
    CHECK(value_of("'' + 123.456;").string() == u"123.456");
    CHECK(value_of("'' + -42;").string() == u"-42");
    CHECK(value_of("'' + NaN;").string() == u"NaN");
    CHECK(value_of("'' + Infinity;").string() == u"Infinity");
    CHECK(value_of("'' + true;").string() == u"true");
    CHECK(value_of("'' + null;").string() == u"null");
    CHECK(value_of("'' + undefined;").string() == u"undefined");
    // ToNumber covers the StringNumericLiteral forms.
    CHECK(value_of("+'0x10';").number() == 16);
    CHECK(value_of("+'0o17';").number() == 15);
    CHECK(value_of("+'0b101';").number() == 5);
    CHECK(std::isnan(value_of("+'-0x10';").number())); // NonDecimalIntegerLiteral has no sign.
    CHECK(value_of("+'  12  ';").number() == 12);
    CHECK(value_of("+'';").number() == 0);
    CHECK(value_of("+'Infinity';").number() == std::numeric_limits<double>::infinity());
    CHECK(value_of("+'1e3';").number() == 1000);
    CHECK(std::isnan(value_of("+'12abc';").number()));
    CHECK(value_of("+'  ';").number() == 0); // Whitespace-only is zero.
    CHECK(std::isnan(value_of("+undefined;").number()));
    CHECK(value_of("+null;").number() == 0);
    CHECK(value_of("+true;").number() == 1);
    CHECK(std::isnan(value_of("+'abc' * 1;").number()));
    CHECK(value_of("'3' * '4';").number() == 12);
    CHECK(value_of("'a' < 'b';").boolean());
    CHECK(value_of("'a' < 1;").boolean() == false);
    CHECK(value_of("2 < 10;").boolean());
}
void objects_and_arrays() {
    CHECK(value_of("let o = {a: 1, b: 'two'}; o.a;").number() == 1);
    CHECK(value_of("let o = {a: 1}; o.b;").tag() == Value::Tag::undefined);
    CHECK(value_of("let o = {a: 1}; o['a'];").number() == 1);
    CHECK(value_of("let k = 'a'; let o = {a: 1}; o[k];").number() == 1);
    CHECK(value_of("let o = {}; o['x'] = 1; o.x;").number() == 1);
    CHECK(value_of("let o = {a: 1}; o.a = 2; o.a;").number() == 2);
    CHECK(value_of("let o = {a: 1}; let v = o.a; v += 4; v;").number() == 5); // Member compounds are unsupported.
    CHECK(value_of("let o = {1: 'one'}; o[1];").string() == u"one");
    CHECK(value_of("let o = {a: 1}; let p = o; p === o;").boolean());
    CHECK(value_of("({}) === ({});").boolean() == false);
    CHECK(value_of("let o = {}; o === o;").boolean());
    CHECK(value_of("let o = {a: 1}; '' + o;").string() == u"[object Object]");
    CHECK(value_of("typeof ({});").string() == u"object");
    CHECK(value_of("typeof [];").string() == u"object");
    CHECK(value_of("typeof null;").string() == u"object");
    CHECK(value_of("typeof 1;").string() == u"number");
    CHECK(value_of("typeof 'a';").string() == u"string");
    CHECK(value_of("typeof true;").string() == u"boolean");
    CHECK(value_of("typeof undefined;").string() == u"undefined");
    CHECK(value_of("typeof missing;").string() == u"undefined");
    CHECK(value_of("function f() {} typeof f;").string() == u"function");
    CHECK(value_of("typeof double;").string() == u"function");
    CHECK(value_of("let a = [1, 2, 3]; a.length;").number() == 3);
    CHECK(value_of("let a = [1, 2, 3]; a[0];").number() == 1);
    CHECK(value_of("let a = [1, 2, 3]; a[3];").tag() == Value::Tag::undefined);
    CHECK(value_of("let a = []; a[3] = 'x'; a.length;").number() == 4);
    CHECK(value_of("let a = []; a[2] = 'x'; a[0];").tag() == Value::Tag::undefined);
    CHECK(value_of("let a = [1]; a[0] = 9; a[0];").number() == 9);
    CHECK(value_of("let a = [1, 2]; '' + a;").string() == u"1,2");
    CHECK(value_of("let a = [[1, 2], [3]]; '' + a;").string() == u"1,2,3");
    CHECK(value_of("let a = [1, null, 2]; '' + a;").string() == u"1,,2");
    CHECK(value_of("let a = [1]; let b = a; b === a;").boolean());
    CHECK(value_of("let a = [1]; let b = [1]; a === b;").boolean() == false);
    CHECK(value_of("let a = []; void a.length;").tag() == Value::Tag::undefined);
    CHECK(value_of("let a = [5]; a < 10;").boolean());
    CHECK(value_of("1 + [2];").string() == u"12");
    CHECK(value_of("let a = ['x']; let v = a[0]; v += 'y'; v;").string() == u"xy"); // Member compounds are unsupported.
    // Keys and values evaluate left to right, and insertion order is preserved.
    CHECK(value_of("let a = []; let o = {}; let i = 0; o[i++] = i; o[0];").number() == 1);
    CHECK(value_of("let a = [1, 2]; let s = ''; for (const x of a) { s += x; } s;").string() == u"12");
    fails("null.x;", "DS_TYPE");
    fails("undefined['x'];", "DS_TYPE");
    fails("null.x = 1;", "DS_TYPE");
    fails("(1).x = 1;", "DS_TYPE");
    fails("'s'.x = 1;", "DS_TYPE");
    fails("let a = [1]; a.length = 0;", "DS_TYPE");
    fails("let a = [1]; a.x = 1;", "DS_TYPE");
    fails("let f = 1; f();", "DS_CALL");
}
void loops() {
    CHECK(value_of("let i = 0; while (i < 5) { i = i + 1; } i;").number() == 5);
    CHECK(value_of("let i = 0; while (i < 0) { i = 1; } i;").number() == 0);
    CHECK(value_of("let i = 0; do { i = i + 1; } while (i < 3); i;").number() == 3);
    CHECK(value_of("let s = 0; for (let i = 0; i < 4; i = i + 1) { s = s + i; } s;").number() == 6);
    CHECK(value_of("let s = 0; for (let i = 0; i < 10; i++) { if (i > 3) break; s = s + i; } s;").number() == 6);
    CHECK(value_of("let s = 0; for (let i = 0; i < 5; i++) { if (i % 2 === 1) continue; s = s + i; } s;").number() == 6);
    CHECK(value_of("let s = ''; for (const c of 'abc') { s = s + c; } s;").string() == u"abc");
    CHECK(value_of("let s = ''; for (const c of '😀a') { s = s + c; } s.length;").number() == 3);
    CHECK(value_of("let n = 0; for (const x of []) { n = 1; } n;").number() == 0);
    CHECK(value_of("let s = ''; let a = [1, 2, 3]; for (const x of a) { s = s + x; } s;").string() == u"123");
    // Each iteration of a for initializer gets fresh per-iteration bindings.
    CHECK(value_of("let i = 0; for (let j = 0; j < 3; j++) { i = j; } i;").number() == 2);
    // Function declarations are block scoped, so the loop's function is not visible outside.
    fails("for (let i = 0; i < 3; i++) { function f() { return i; } } f();", "DS_REFERENCE");
    CHECK(value_of("let x = 'outer'; for (let x = 0; x < 1; x++) {} x;").string() == u"outer");
    CHECK(value_of("let t = 0; for (const x of [1, 2]) { t = t + x; } t;").number() == 3);
    CHECK(value_of("let total = 0; for (const x of [1, 2]) { if (x === 2) continue; total += x; } total;").number() == 1);
    // Loop completion values follow the script completion rules.
    CHECK(value_of("for (let i = 0; i < 3; i++) { i; }").number() == 2);
    CHECK(value_of("for (let i = 0; i < 0; i++) { i; }").tag() == Value::Tag::undefined);
    CHECK(value_of("while (true) { 1; break; }").number() == 1); // break keeps the block's value.
    CHECK(value_of("while (true) { break; }").tag() == Value::Tag::undefined);
    CHECK(value_of("let n = 0; for (;;) { n = n + 1; if (n > 2) { n; break; } } n;").number() == 3);
    CHECK(value_of("function sum(a) { let t = 0; for (const x of a) { t += x; } return t; } sum([1,2,3]);").number() == 6);
    CHECK(value_of("function find(a) { for (const x of a) { if (x > 2) return x; } return -1; } find([1,2,3,4]);").number() == 3);
    fails("for (const x of 5) {}", "DS_TYPE");
    fails("for (const x of {}) {}", "DS_TYPE");
}
void updates_and_compounds() {
    CHECK(value_of("let i = 1; i++; i;").number() == 2);
    CHECK(value_of("let i = 1; let a = i++; a + i;").number() == 3);
    CHECK(value_of("let i = 1; let a = ++i; a + i;").number() == 4);
    CHECK(value_of("let i = 5; i--; i;").number() == 4);
    CHECK(value_of("let i = 1; i += 2; i;").number() == 3);
    CHECK(value_of("let s = 'a'; s += 'b'; s;").string() == u"ab");
    CHECK(value_of("let s = 'n='; s += 5; s;").string() == u"n=5");
    CHECK(value_of("let i = 10; i -= 3; i;").number() == 7);
    CHECK(value_of("let i = 10; i *= 3; i;").number() == 30);
    CHECK(value_of("let i = 10; i /= 4; i;").number() == 2.5);
    CHECK(value_of("let i = 10; i %= 4; i;").number() == 2);
    CHECK(value_of("let s = 'a'; s += 1; s;").string() == u"a1");
    CHECK(value_of("let a = [1]; a += 1;").string() == u"11");
    CHECK(value_of("let i = 1; i = i++;").number() == 1);
    fails("const c = 1; c++;", "DS_CONST");
    fails("const c = 1; c += 1;", "DS_CONST");
    fails("const c = 1; log(c += 1);", "DS_CONST");
    CHECK(std::isnan(value_of("let s = 'a'; s++;").number())); // ToNumber semantics, not a type error.
    CHECK(std::isnan(value_of("let o = {}; let v = o.a; v++;").number())); // ToNumber of undefined is NaN.
    // The right-hand side still evaluates before a const assignment fails.
    const auto outcome = agree("const c = 1; noop(c += 1);");
    CHECK(outcome.ok == false && outcome.code == "DS_CONST");
}
void scopes_and_functions() {
    CHECK(value_of("let x = 1; { let x = 7; x = 8; } x;").number() == 1);
    CHECK(value_of("let x; x;").tag() == Value::Tag::undefined);
    CHECK(value_of("let x = 1; if (x) { x = 2; } else { x = 3; } x;").number() == 2);
    CHECK(value_of("if (false) { missing(); } 9;").number() == 9);
    CHECK(value_of("f(4); function f(x: number) { if (x < 2) return 1; return x * f(x - 1); }").number() == 24);
    CHECK(value_of("function f(a) { return a; } f();").tag() == Value::Tag::undefined);
    CHECK(value_of("function f(a) { return a; } f(7, 8);").number() == 7);
    CHECK(value_of("function f() { 42; } f();").tag() == Value::Tag::undefined);
    CHECK(value_of("function make() { let n = 0; function next() { n = n + 1; return n; } return next; } let f = make(); f(); f();").number() == 2);
    CHECK(value_of("function f() {} let g = f; g === f;").boolean());
    CHECK(value_of("function apply(f, x) { return f(x); } function double(x) { return x*2; } apply(double, 9);").number() == 18);
    CHECK(value_of("function f() { return {a: 1}; } f().a;").number() == 1);
    CHECK(value_of("function f(a) { return a; } f({}).x;").tag() == Value::Tag::undefined);
    CHECK(value_of("function outer() { let a = 1; function inner() { return a; } a = 2; return inner(); } outer();").number() == 2);
    CHECK(value_of("function count(list) { let n = 0; for (const x of list) { n += 1; } return n; } count([]);").number() == 0);
    fails("let x = x;", "DS_TDZ");
    fails("let x=1; { x; let x=2; }", "DS_TDZ");
    fails("const x=1; x=2;", "DS_CONST");
    fails("missing;", "DS_REFERENCE");
    fails("missing=1;", "DS_REFERENCE");
    fails("1();", "DS_CALL");
    fails("function f() { missing; } f();", "DS_REFERENCE");
    fails("function f() {} f;", "DS_BOUNDARY");
    fails("function f(a) { return a; } f(b);", "DS_REFERENCE");
    fails("function f() { return; } f().a;", "DS_TYPE");
}
} // namespace
void runtime_tests() {
    numbers_and_strings();
    number_string_conversions();
    objects_and_arrays();
    loops();
    updates_and_compounds();
    scopes_and_functions();
    CHECK(value_of("9;").number() == 9);
}