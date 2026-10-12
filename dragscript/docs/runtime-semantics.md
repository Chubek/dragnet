# Runtime semantics of the initial subset

This document specifies the implemented subset, including deliberate rejections.
The semantic baseline is ECMAScript primitive values, the conversion algorithms,
strict equality, Boolean conversion, ordered expression evaluation, lexical
bindings, calls and completion records (ECMA-262 sections *Language Types*,
*Number Type* (including `Number::toString` and `StringNumericLiteral`),
*ToPrimitive*, *ToNumber*, *ToString*, *ToBoolean*, *IsStrictlyEqual*,
*Evaluation*, *Declarative Environment Records*, *Completion Records*, and
*ForIn/OfBodyEvaluation*). Tests compare the common subset with the local QuickJS
implementation; the oracle is evidence, not a substitute for these contracts.

## Values, literals and strings

Public `Value` tags are distinct `undefined`, `null`, boolean, IEEE-754 binary64
number, and immutable UTF-16 string. Accessors require matching tags. Internal
values additionally include script-function arena IDs, host-function registry
references, and object/array arena indices. Objects and arrays are truthy and
identity-comparable, but cannot escape through public results or host arguments.
BigInt, Symbol, prototype chains and exception values are not implemented.

Source is validated UTF-8. String length in quotas counts UTF-16 **code units**:
astral characters occupy two, `\uD800` preserves a lone surrogate, and string
indexing returns a single code unit. Supported escapes: quotes, backslash,
`\n \r \t \b \f \v`, isolated `\0`, `\xHH`, `\uHHHH`, and LF/CR/CRLF line
continuations. Other escapes, including `\u{...}`, identity escapes and legacy
octal, are rejected. `display()` is debug formatting that escapes non-ASCII
units and preserves negative zero; it is not JavaScript `ToString`.

Numbers use decimal literals with an optional decimal point/exponent. Radix
prefixes, separators, BigInt, leading-zero legacy forms, and decimal literals
outside `from_chars` binary64 range are rejected.

## Conversions

`ToString(number)` implements `Number::toString`: shortest round-trip digits with
the specification's fixed/exponential selection at `1e21` and `1e-6`, so `0`,
`-0`, `1`, `1.5`, `100`, `0.5`, `1e+21`, `1e-7`, `0.000001` and `123.456` all
format as the language requires. NaN and infinities use their spellings.

`ToNumber(string)` trims ECMAScript whitespace and then accepts only a complete
`StringNumericLiteral`: decimal with optional sign and exponent, `Infinity`
spellings, or a `0x`/`0o`/`0b` integer literal. Non-decimal literals carry no
sign, so `"-0x10"` is NaN. Anything else, including trailing garbage such as
`"12abc"`, is NaN. `ToNumber` of `true`/`false` is 1/0, of `null` is 0, of
`undefined` and objects without user conversions is NaN.

`ToPrimitive` has no user-defined conversions, so both the default and number
hints end at `toString`: objects become `"[object Object]"` and arrays become
their `join` result, recursively, with nullish elements rendered as empty. The
result then flows through `ToNumber` or `ToString` as the operator requires, which
reproduces cases such as `[5] < 10` being true and `{} < 1` being false.

Arithmetic supports numeric `+ - * / %` and unary `+ -`; string/string `+`
concatenates. Ordering supports homogeneous number or string operands (UTF-16
lexicographic order); mixed operands convert numerically. `+` concatenates when
either primitive operand is a string, otherwise it adds numerically.
`===`/`!==` implement primitive strict equality, object/array identity and
internal callable identity. Loose equality, SameValue APIs and bitwise operators
are unsupported.

Division by zero yields infinities/NaN, and remainder uses truncating
floating-point remainder including negative zero and NaN. NaN never compares
strictly equal, and both zero signs compare equal. Builds require IEEE binary64;
embedders must not enable fast-math or change the execution thread's
floating-point mode during callbacks.

Falsy values are undefined, null, false, either zero sign, NaN and the empty
string. `typeof` returns `undefined` for unresolvable identifiers without raising
a ReferenceError, `object` for null and objects, `function` for callables, and
the primitive tag name otherwise. `void` evaluates its operand and yields
undefined.

## Objects, arrays and properties

Objects have insertion-ordered own string-keyed properties; assigning an existing
key keeps its position. Absent properties read as undefined. Keys are produced by
`ToString`, so numbers, booleans, null and undefined become canonical strings and
computed keys evaluate in source order. Reading a property of `null` or
`undefined` is a type error.

Arrays are dense with no holes. `length` is read-only; assigning it is a type
error. Index assignment grows the array with undefined padding up to the array
length limit, and non-index properties are rejected rather than silently dropped.
Array `toString` is `join(",")` with nullish elements rendered as empty and nested
arrays joined recursively.

The object and array representations are deliberately simple. There are no
prototypes, property descriptors, getters/setters, enumerability rules, `delete`,
`in`, iteration protocols, proxies or reflective operations, and object identity
is arena-based per run. Function values carry no properties yet.

## Bindings, calls and completion

Supported names are unescaped ASCII identifiers that are not reserved words.
`let`/`const` are block-scoped cells prepared before a scope executes. Reads and
writes before initialization produce `DS_TDZ`; uninitialized `let` becomes
undefined at its declaration. `const` requires an initializer and rejects writes.
Duplicate names within a scope, including parameters against function-body
declarations, are compile errors; this strict uniqueness rule is narrower than
JavaScript's permitted parameter and block-function redeclarations. Nested
shadowing works. `var`, destructuring and Annex B sloppy block-function rules are
unsupported, and function declarations are block scoped.

Function declarations are hoisted in their immediately enclosing lexical scope.
Nested declarations capture that environment's mutable cells, not copied values.
Functions may be assigned, passed to other script functions, returned internally,
and called recursively. Parameters are simple names with optional erased TypeScript
annotations or optionality. Missing arguments become undefined; extra arguments
are evaluated and ignored. Defaults, rest, `arguments`, `this`, constructors,
function expressions, arrows, generators and async functions are unsupported.

Call target is evaluated before arguments; arguments and binary operands evaluate
left-to-right. Assignment resolves its reference, evaluates the right-hand side,
then checks missing, TDZ or immutability errors, preserving right-hand effects.
`++`/`--` and compound assignment accept identifier targets only; member targets
are rejected at compile time. Compound assignment reuses the binary operator
contract, so `s += 5` concatenates for strings and `v++` coerces with `ToNumber`.

`while` evaluates its test before every iteration including the first; `do`/`while`
runs the body first; `for` evaluates optional initializer, test, body and update
in that order, and `for (let ...)` copies its loop bindings into a fresh
per-iteration environment so later iterations observe updated values. `for (x of
a)` iterates arrays densely and strings by code point, and may declare its binding
with `let`/`const`.

Statement completion follows the specification: declarations and empty blocks
preserve the previous completion value; `if` applies the undefined fallback;
returns propagate their own value; `break` and `continue` carry an empty value,
so a block keeps its accumulated completion value through them, and a loop
absorbs them and yields the completion of its last iteration. Top-level evaluation
returns the script's final completion value or undefined. Top-level `return`,
`break` and `continue` are compile errors. Abrupt **return** is distinct from
normal completion and from engine failure; `throw`/`try` are rejected.

Each run starts fresh. Undefined/NaN/Infinity and host functions are read-only
bindings in a parent environment, and scripts may shadow them. A failed request
does not retain script cells; already performed host effects are not rolled back.

## TypeScript handling

Type annotations including parenthesized annotations, aliases, interfaces,
function type parameters, call type arguments, optional typed parameters, definite
assignment annotations, `as`, `satisfies`, non-null and TS angle-bracket assertions
are erased after full syntax validation. Surrounding expressions must be
supported. No assignability, type-name resolution or generic checking is performed.
JS and JSX modes reject these erasures. Type-only imports and exports are also
rejected until module handling is defined. Enums, namespaces, decorators and JSX
are rejected. The pinned grammar exposes no prefix flag for `++`/`--` and no
parenthesized-annotation erasure difference, so those are handled explicitly and
tested. TSX uses the TSX grammar for all `.tsx` sources, and the pinned grammar's
negative parenthesized comparison ambiguity is recorded in the feature matrix.

## Host boundary, diagnostics and limits

Only explicitly registered simple-name functions are exposed. The core grants no
filesystem, process, networking or browser capability. Registration rejects
duplicates, invalid names, empty callbacks and impossible arity contracts.
Callback argument-count bounds are checked by the engine; callbacks validate
primitive tags themselves. `HostResult.error` and C++ exceptions become `DS_HOST`
errors with call-site provenance. Host returns undergo string and heap checks.

Cancellation is polled at parser progress, syntax traversal, compile publication
and interpreter safe points, including after host returns. Step charging covers
statements, expressions, binding preparation, calls and explicit callback work
through `HostContext::consume_steps`; ignoring a false charge result cannot let
execution continue. A synchronous callback must cooperate with cancellation; the
interpreter cannot preempt blocking native host code. Hosts own wall-time
enforcement and scheduling; there is no core event loop.

Limits use `DS_LIMIT` with a `limit` category: `source_bytes`, `parse_checks`,
`nodes`, `compile_depth`, `string_units`, `steps`, `call_depth`,
`evaluation_depth`, `heap_bytes`, `array_length`, `properties_per_object` and
`property_name_units`. Hard ceilings are 50,000 syntax and DSIR nodes, 128 compile
nesting, 128 script calls and 512 interpreter nesting; smaller configured limits
apply. At least one diagnostic is retained. Tree walking counts punctuation and
erased types as well as executable syntax. Parser work counts progress checks, a
runtime-dependent granularity, so zero is the deterministic boundary. Tree
count and depth checks happen after parsing and are not exact accounting of
native parser memory.

`max_heap_bytes` is a cumulative **logical allocation quota**, not a process RSS
limit. It charges environments, binding storage and names, function handles,
argument vectors, trace frames and UTF-16 units on literal evaluation,
concatenation and host return. Charges are conservative and are never reclaimed
during a run, consistent with the arena lifetime. `RunResult` exposes charged
bytes and used steps. String limits apply to literal evaluation at run time, so
an artifact compiled under laxer limits still obeys the limits of the engine that
runs it.

Diagnostics own code, message, source name, origin, half-open byte span, one-based
UTF-8 byte line and column, compile or runtime phase, optional limit category and
script call-site trace. EOF locations are valid. Codes include `DS_MODE`,
`DS_ENCODING`, `DS_SYNTAX`, `DS_UNSUPPORTED`, `DS_DUPLICATE`, `DS_DECLARATION`,
`DS_SCOPE`, `DS_SEMANTIC`, `DS_RETURN`, `DS_IR`, `DS_REFERENCE`, `DS_TDZ`,
`DS_CONST`, `DS_TYPE`, `DS_CALL`, `DS_ARITY`, `DS_HOST`, `DS_BOUNDARY`,
`DS_CANCELLED`, `DS_REENTRANT`, `DS_ARTIFACT`, `DS_LIMIT` and allocation or
internal failure categories.