# Chapter 15. TraceML and Metatracing

[Previous: Runtime and caches](14-runtime-translation-and-caching.md) · [Contents](README.md) · [Next: Native interoperability](16-exolayer-native-interoperability.md)

## 15.1 Source semantics before machine lowering

TraceML is Limestone's functional, metatracing-oriented frontend. The current
implementation uses source-located S-expressions and a lambda-calculus-based
TraceLambda representation evaluated by the MetaKrivine model.

The conceptual path is source → TraceLambda → machine-oriented lowering →
selection/allocation/backend. The frontend's lexical lazy semantics are
independent of the chosen target. Target instruction spelling must not change
when an argument is evaluated, how a closure captures names, or whether an
untaken branch can fail.

The public C++ API is [traceml/traceml.hpp](../traceml/traceml.hpp), exported by
`Limestone::traceml`. The connected source compilation entry points are in
`limestone/limestone.hpp` and `limestone/limestone.h`.

## 15.2 The implemented source forms

The source vocabulary includes signed-64-bit integer literals, symbols, application,
single-parameter lambdas, conditionals, and sequences:

```text
42
(add 20 22)
((lambda x (add x 2)) 40)
(if (lt 1 2) (mul 6 7) 0)
(begin 1 (sub 50 8))
```

Lambda syntax is `(lambda name body)`. Application places the function expression
first, followed by arguments. `if` has condition, taken arm, and alternate arm.
`begin` requires at least one result expression and evaluates its expressions in
order, returning the last.

The language is expression-oriented. A lambda is a closure, not immediately a
target function address. A source variable is a lexical binding, not already a
machine register. Closure/environment native representation is a later explicit
runtime-lowering contract.

## 15.3 The current integer primitive vocabulary

| Primitive | Arity | Semantics |
| --- | ---: | --- |
| `add` | 2 | Checked signed-64-bit addition |
| `sub` | 2 | Checked signed-64-bit subtraction |
| `mul` | 2 | Checked signed-64-bit multiplication |
| `neg` | 1 | Checked signed-64-bit negation |
| `eq` | 2 | Equality, returning integer zero or one |
| `lt` | 2 | Signed comparison, returning integer zero or one |

This table reflects the implementation's actual primitive registry. A plausible
operator name is not implicitly a builtin. Adding a primitive requires source
verification, evaluator semantics, tracing, lowering, and target/runtime contracts
as appropriate.

Arithmetic does not wrap silently. Overflow reports a structured error at the
source expression. Negating the signed minimum is invalid. Primitive arguments
are evaluated strictly left to right, and must yield integers.

## 15.4 Lexical call-by-name closures

Lambda application binds the argument as a closure containing its expression and
lexical environment. The argument is evaluated when the body needs it. This is
call-by-name rather than eager argument evaluation or a promise of memoized
call-by-need sharing.

An unused argument can therefore contain a computation that would overflow:

```text
((lambda ignored 42) (add 9223372036854775807 1))
```

The result is `42`. The argument's arithmetic is never executed. Similarly:

```text
(if (lt 1 2) 42 (add 9223372036854775807 1))
```

Only the selected arm is evaluated. Both arms must still be syntactically and
lexically well formed. An unbound name in an untaken arm is a verification problem,
not a permitted deferred arithmetic failure.

Lexical scope follows the closure's captured environment. Rebinding a name in the
caller does not change a previously formed closure's binding. Shadowing and
function application must preserve that distinction when constructing or editing
TraceLambda.

## 15.5 Parsing, normalization, and verification

`parse` produces an expression; `compile` parses a program and verifies normalized
forms. The representation distinguishes `Symbol`, `Integer`, `Apply`, `Lambda`,
`If`, and `Begin`, retaining source offsets, lines, and columns.

`verify` checks names, shape, lambda binding, known primitive arity, malformed
children, cyclic expression graphs, and bounded TraceLambda nesting. Unbound
names and invalid primitive applications fail before evaluation.

```cpp
auto program = limestone::traceml::compile("((lambda x (add x 2)) 40)");
if (!program) return report(program.error());
auto value = limestone::traceml::evaluate(program.value());
```

This API fragment assumes the header and host reporter. `evaluate` returns a
closed integer result. A function-valued final result is not an integer result
for this entry point. Multiple program forms execute under the program's sequence
semantics; they are not an implicit module of named native functions.

## 15.6 Ordinary and traced execution share one evaluator

`execute` returns `ExecutionResult` with final value, step count, optional result
identity, and recorded events. `ExecutionOptions` configures step limit, event
limit, recording, cancellation, and synchronous observation.

```cpp
limestone::traceml::ExecutionOptions options;
options.step_limit = 100000;
options.event_limit = 100000;
options.record_trace = true;
auto execution = limestone::traceml::execute(program.value(), options);
```

Default step and event budgets are 100,000. TraceLambda verification separately
bounds nesting to 256. Step exhaustion, event exhaustion, cancellation, malformed
IR, and arithmetic errors remain explicit failures with source context.

An observer receives a borrowed event during the call. It must not mutate program
IR or retain a reference to transient event storage. Copy desired event fields
into application-owned diagnostics. Recording can be disabled while observation
remains active; observed events still use the event budget.

## 15.7 Event identities and branch decisions

Event kinds include application, binding, lookup, integer, primitive, branch,
sequence, and return. An event records step, offset/line/column, operation,
optional value identity, input identities, optional integer result, and optional
branch decision.

Integer and primitive events can establish stable trace value identities.
Primitive events reference evaluated input identities. Branch events preserve
the chosen path. Binding and lookup events explain the lexical execution path
without pretending closures are already native storage locations.

`print_trace` formats the owning execution result. It is useful for checking
which computations actually ran, especially lazy unused arguments and untaken
overflowing arms. The final integer alone cannot show whether metatracing
information was preserved.

## 15.8 Constant-result lowering

`lower_graph(value)` constructs the portable constant/return graph used by the
source convenience pipeline. Ordinary closed-source compilation first evaluates
the source with its actual lazy semantics, then lowers the resulting integer.

```sh
printf '%s\n' '((lambda x (add x 2)) 40)' | build/limestone-cli
```

This produces inspectable portable IR. It does not claim general closure code
generation. The explicit-target overload uses the same evaluated result and
requires legal constant/return patterns in the target.

The checked text-lowering API is `lower_checked`. The convenience
`lower_to_machineir` also exists; embedding code that needs structured failure
should choose the checked entry point or the full pipeline result.

## 15.9 Trace lowering and guards

`lower_trace(execution)` retains executed operations as checked arithmetic and
branch-guard pseudo-operations. It preserves provenance and the selected branch
contract instead of reducing every trace to one constant result.

```sh
printf '%s\n' '(if (lt 1 2) (add 20 22) 0)' | build/limestone-cli \
  --trace-execution -o /tmp/opencode/traced-answer.mir
```

The machine listing is output separately from execution events on standard error.
An executable guard needs a defined failed-guard continuation or deoptimization
path. Removing that obligation because the recorded branch was true would erase
the metatracing contract.

Likewise, recorded checked arithmetic needs a target implementation with the
correct overflow behavior. A wrapping native instruction without a check is not
automatically legal for the traced source primitive.

## 15.10 Explicit-target source compilation

The connected APIs are:

- C++ `run_pipeline(source,target,options)`;
- C `limestone_compile_target(source,target,configuration,error)`;
- Python `compile_target(source,target_handle,configuration_handle)`.

The normal path uses closed evaluation and constant/return selection. Trace mode
requires legal patterns for every retained checked operation and guard. Missing
or out-of-range patterns report an error rather than degrading the trace.

The native constant fixture declares System V x86-64 no-argument i64 return,
fixed RAX operands, signed-32-bit immediate legality, masked encoding, and ELF
identity. Its frontend accepts arbitrary well-formed closed computations whose
final result fits that bounded slice.

```sh
printf '%s\n' '(if (lt 1 2) 42 (add 9223372036854775807 1))' | \
  build/limestone-cli --target-isa tests/fixtures/native-constant.isa \
  --allocate --encode --object answer -o /tmp/opencode/answer.o
```

The unused arm remains unevaluated, then `42` is emitted under the explicit ABI.
All three selectors and four allocators are exercised by the native pipeline
tests. This establishes the slice's behavior, not a complete native ML runtime.

## 15.11 Extending source and runtime lowering

A new source feature needs a syntax and semantic contract first. Determine its
binding/evaluation behavior, how execution events represent it, and what
TraceLambda invariants it requires. Then establish its machine lowering and
runtime interface.

General closure compilation needs environment layout, captured-value lifetimes,
argument representation, call/return conventions, allocation, and tracing
interaction. General guards need resumable state and exits. These contracts belong
at the lowest stage with sufficient information, while source semantics remain
in the frontend/evaluator.

Source transformations should be validated against MetaKrivine behavior. An
optimization that evaluates a previously unused argument or both conditional
arms changes observable semantics even if many ordinary arithmetic examples
still return the same result.

## 15.12 Semantic regression strategy

Test lexical shadowing, captured environments, unused arguments, repeated uses,
strict primitive order, selected/untaken branches, sequence results, overflow
boundaries, malformed IR, and execution budgets. For metatracing tests, assert
event identities and branch decisions in addition to the integer result.

For explicit targets, independently execute emitted code when the ABI allows it,
including signed immediate boundaries and rejected out-of-range values. Verify
that frontend diagnostics remain source-oriented and target failures identify
the actual unsupported lowering contract.

[Previous: Runtime and caches](14-runtime-translation-and-caching.md) · [Contents](README.md) · [Next: Native interoperability](16-exolayer-native-interoperability.md)
