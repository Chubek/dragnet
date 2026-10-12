# TraceML

TraceML implements a source-located S-expression frontend and MetaKrivine
execution model. Link `Limestone::traceml` for `traceml.hpp`.

```text
((lambda x (add x 2)) 40)
(if (lt 1 2) (mul 6 7) 0)
(begin 1 (sub 50 8))
```

Lambdas use lexical call-by-name closures. Integer primitives evaluate arguments
strictly left to right; arithmetic overflow, invalid division, malformed IR, and
unbound names return structured errors. `compile` parses and verifies the source;
`evaluate` returns a closed integer result.

`execute` shares the same evaluator and accepts `ExecutionOptions`: step/event
budgets, cancellation, trace recording, and a synchronous observer. Events preserve
source locations, argument/value identities, results, and branch decisions.
Observers receive borrowed events and must not mutate program IR.

`lower_graph` produces portable constant/return IR for a closed result.
`lower_trace` retains executed operations and explicit branch guards with source
provenance; `lower_machine_ir_checked` exposes lowering errors. The top-level
`--trace-execution` mode also retains the execution events in the module and
prints them to stderr.

```sh
printf '%s\n' '(if (lt 1 2) (add 20 22) 0)' | build/limestone-cli --trace-execution
```

The guard contract preserves the selected branch, rather than erasing metatracing
semantics during lowering. Executable guards need a backend deoptimization/
continuation contract; general closure/environment native code generation needs
runtime lowering. Portable IR output is directly inspectable and exchangeable
with D MachineIR.

Closed source programs can also use an explicit compiler target via
`limestone::run_pipeline(source, target, options)` or the C/Python
`limestone_compile_target` / `compile_target` entry points. Normal lowering
evaluates the closed program using the same lazy MetaKrivine semantics, then
selects/allocates/encodes its constant/return graph. Trace lowering requires legal
target implementations of each recorded checked operation and guard.
`tests/fixtures/native-constant.isa` declares a bounded native constant/return
slice, including fixed RAX operands, the System V x86-64 i64 return ABI and ELF
metadata. The end-to-end tests execute its emitted functions and system-linked
objects. Missing/out-of-range target patterns fail explicitly.
