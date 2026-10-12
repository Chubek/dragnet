# Feature matrix

“Parsed” means the pinned grammar recognizes syntax, not that it executes.
“Tested” applies only to the bounded semantics described here and in
[runtime-semantics.md](runtime-semantics.md). Unsupported executable syntax
produces `DS_UNSUPPORTED`; malformed syntax produces `DS_SYNTAX`.

Status legend: **parsed** = recognized only, **validated** = accepted or rejected
with a diagnostic, **executed** = runs, **tested** = executed with positive,
negative and edge-case tests on both execution backends.

| Feature | Parser | Semantic validation | Runtime | Host integration | Tests | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `.ts`, `.tsx`, explicit source kinds | TS/TSX entry points | Explicit mode selection | Common subset | Source name/origin | Frontend | tested |
| `.js` | TS grammar | Reject supported TS-only erasures | Common subset | Explicit eval mode | Frontend | tested |
| `.jsx` | TSX grammar when enabled | Opt-in mode; JSX nodes rejected | Common non-JSX subset | `enable_jsx` option | Frontend | tested |
| Syntax errors/missing nodes | Error recovery | Fail compilation, bounded diagnostics | — | Owned spans, byte columns | Frontend | tested |
| Decimal numbers, booleans, strings, null | Yes | Restrict literal forms | Binary64; UTF-16 strings | Public primitive `Value` | Runtime/differential | tested |
| `undefined`, `NaN`, `Infinity` | Identifiers | Lexical lookup | Read-only builtin bindings | Per-run environment | Runtime/differential | tested |
| `+ - * / %`, unary `+ - !` | Yes | Supported operators only | Numeric operations; string/string `+` | Primitive args/results | Runtime/differential | tested |
| `=== !== < <= > >=` | Yes* | Strict comparison contracts | Primitive strict equality; internal callable identity | Primitive comparisons | Runtime/differential | tested |
| `&& \|\| ??` | Yes | Explicit DSIR operators | Left-to-right, lazy RHS, operand-valued | Observable callback order | Embedding/differential | tested |
| `typeof`, `void` | Yes | Single operand | All primitive tags plus object/function; `typeof` of unresolvable name | — | Runtime/differential | tested |
| `let`, `const`, identifier assignment | Yes | ASCII bindings, duplicates, const initializer | Lexical cells, TDZ, const checks, shadowing | Host/builtin shadowing | Runtime/embedding | tested |
| Blocks, `if`/`else`, expression/empty statements | Yes | Branch declarations require blocks | Distinct scopes; completion propagation | Fake page script | Runtime/differential | tested |
| Object literals, member read/write, computed keys | Yes | String-keyed own properties | Insertion-ordered properties, `undefined` for absent | Primitive values only at the boundary | Runtime/differential | tested |
| Array literals, index read/write, `.length` | Yes | No holes, no non-index properties | Dense storage, undefined padding on growth | Primitive values only | Runtime/differential | tested |
| String indexing and `.length` | Yes | UTF-16 code units | Single-unit strings; length in units | — | Runtime/differential | tested |
| `while`, `do`/`while`, `for` | Yes | Declarations require a block body | Test-before-body, per-iteration bindings, completion values | Observable loop progress | Runtime/differential | tested |
| `break`, `continue` | Yes | Requires an enclosing loop | Absorbed by the loop; preserve block completion | — | Runtime/differential | tested |
| `for (x of array/string)` | Yes | Array or string iterable only | Code-point iteration; per-iteration bindings | Observable callback order | Runtime/differential | tested |
| `++`, `--` | Yes | Identifier target only | Prefix and postfix forms; numeric conversion | — | Runtime/differential | tested |
| `+= -= *= /= %=` | Yes | Identifier target only | Reuses the binary operator contract | — | Runtime/differential | tested |
| Number↔string conversions | — | Explicit conversions | `Number::toString`, `StringNumericLiteral`, whitespace trim | Debug `display()` is separate | Runtime/differential | tested |
| Function declarations, calls, returns | Yes | Simple parameters; no top-level return | Hoisting, recursion, missing/extra args | Arity-checked callbacks | Runtime/embedding/limits | tested |
| Nested functions/captured mutable bindings | Yes | Scope and parameter validation | Per-run arena handles; no function escape to host | Primitive-only boundary | Runtime/differential | tested |
| Annotations, aliases, interfaces | Yes | Syntax validation, TS modes only | Erased; no type checker | — | Frontend | tested |
| `as`, `satisfies`, non-null, angle-bracket assertions | TS mode rules | Erase around supported expression | Expression unchanged | — | Frontend | tested |
| Function type parameters/call type arguments, optional typed params | Yes | Erased; defaults/rest rejected | Normal argument binding | — | Frontend/runtime | tested |
| `ds.*` tree dialect and dumps | — | Arity, tags/types, child yields, attributes, SSA uses | Both backends consume it | Opaque artifact | Lowering/negative IR tests | tested |
| Second execution backend | — | Shared value/coercion/host contracts | MLVM `ds.*` tree interpreter | Same host contract | Backend equivalence/fuzz | tested |
| Host capabilities | Calls | Explicit registration, simple names | Sync callback; exception/error translation | Fake log/record/double | Embedding | tested |
| Cancellation/resource quotas | — | Source/parser/tree/diagnostic limits | Step/depth/heap/string/array/property limits | Atomic cancellation; callback charging | Exact/one-over limits | tested |
| `var`, `for-in`, `while`-free updates, loose equality, bitwise | Yes | Rejected | — | — | Negative frontend | parsed |
| Compound/member assignment targets beyond identifiers | Yes | Rejected | — | — | Negative frontend | parsed |
| Classes, `new`, arrows, getters/setters, spread, destructuring | Yes | Rejected | — | — | Negative frontend | parsed |
| Enums, namespaces, decorators, JSX transforms | Yes | Rejected | — | — | Negative frontend | parsed |
| Type-only and runtime imports/exports | Yes | Rejected, including type-only forms | No resolution or filesystem access | — | Negative frontend | parsed |
| `throw`, `try`, generators, async/await, templates, labels | Yes | Rejected | Structured engine errors only | — | Negative frontend | parsed |
| Conditional (`?:`) and sequence expressions | Yes | Rejected | — | — | Negative frontend | parsed |
| General TypeScript type checking | Grammar only | — | — | — | — | planned |
| Dragworks DOM/network/timer integration, realms | — | — | — | Actual interfaces absent | Fake host only | planned |
| MLVM optimization/Blackstone kernels/JIT/native backend | — | — | — | — | — | planned |

*Pinned grammar limitation: `(-3) < (-3)` is misrecognized as an incomplete
instantiation expression by both generated 0.23.2 grammars. Dragscript retains
the syntax error and has a regression test. `let a=-3; let b=-3; a < b;` is
accepted and differential-tested. Syntax recognition is not repaired by ignoring
missing nodes.

Additional deliberate boundaries: decimal radix only, no separators or legacy
octal; literal decimal overflow/underflow rejected; selected conventional string
escapes only; no Unicode/escaped identifiers; no function values crossing a run or
host boundary; unique names throughout each lexical scope and parameter/body scope;
dense arrays without holes; `length` is read-only; property keys are strings. See
the semantic document for exact details.