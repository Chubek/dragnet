# VM contracts

`libvm` is a dependency-free, header-only C++17 component in `firestone::vm`.
Link `Firestone::vm` or `Firestone::firestone`.

## Architecture

| Layer | API |
| --- | --- |
| Values and locations | `Value`, `ValueKind`, `Address`, `ObjectRef`, `CodeAddress`, `Signature` |
| Bytecode | `Opcode`, `opcode_table`, `Instruction`, `CodeTape`, `ConstantPool`, `Function`, `Module` |
| Validation and lowering | `Module::verify`, `require_valid`, `compile`, `Compiler` |
| Execution | `execute`, `step`, `resume`, `StackVM`, `RegisterVM`, `ExecutionState` |
| Host runtime | `Environment`, `NativeFunction`, `MetaObject`, `Object` |
| Persistence | `encode`, `decode`, `BytecodeCache`, `Loader`, `disassemble` |
| Dispatch representations | `TableThreadedCode`, `DirectThreadedCode`, `IndirectThreadedCode`, `SubroutineThreadedCode` |
| Compilation integration | `AOT`, `MethodJIT`, `TraceJIT`, `CompiledEntry`, `TraceBody` |
| State transfer | `Guard`, `SlotLocation`, `OSRPlan`, `transfer_osr`, `deoptimize` |

Interpreter semantics are shared by both VM models. Threaded representations
bind a tape to caller-supplied host handlers. Compilation backends are callbacks;
the library provides tiering, validation, recording, fallback, and state transfer.
The host supplies architecture-specific native code generation and code ownership.

## SSA embedding

```cpp
#include <libvm/Compiler.hpp>
#include <libvm/RegisterVM.hpp>
#include <libvm/BytecodeBinary.hpp>

firestone::ir::SimpleSSAGraph ssa;
auto block = ssa.cfg().create_block("entry");
auto x = ssa.parameter(firestone::ir::Type::integer());
auto sum = ssa.define(block, firestone::ir::Opcode::Add,
    firestone::ir::Type::integer(),
    {firestone::ir::Operand::ref(x), firestone::ir::Operand::constant(1)});
ssa.cfg().return_value(block, firestone::ir::Operand::ref(sum));

firestone::vm::Module module;
auto function = firestone::vm::compile(module, ssa, "increment");
auto loaded = firestone::vm::decode(firestone::vm::encode(module));
firestone::vm::Environment environment;
auto result = firestone::vm::RegisterVM{}.run(loaded, function,
    {std::int64_t{41}}, environment);
auto answer = firestone::vm::require<std::int64_t>(*result.state.result); // 42
```

Pass `VMModel::Stack` to `compile` and use `StackVM::run` for stack lowering.
Compilation validates the SSA and resulting module before committing. Failure
preserves the original module, including its constants. Existing bytecode/native
callees resolve by the SSA instruction's annotation. Declare native signatures in
`module.natives` before compilation. The function being compiled is declared
internally before lowering so self-recursion resolves. Other callees must exist.

Parameters occupy initial slots in SSA value-ID order. Other SSA values receive
stable slots. Register lowering materializes immediate operands in temporary
registers. Stack lowering uses locals for SSA values. Phi assignments execute in
split edge blocks; every source is captured before any destination changes.
This preserves loop-carried swaps, shared targets, and cyclic parallel copies.

Supported IR values: boolean, signed integers at widths 2–64, binary64 floats,
and pointer values represented as heap-cell `Address` handles. Runtime storage
does not retain integer widths or pointer pointee names; scalar bytecode carries
the operation width explicitly. Function/native signatures check runtime kinds,
not IR width/name equality. Memory tokens, function values, arbitrary-width
integers, and non-binary64 floats are rejected. Named operands lower to globals.

## Values and object storage

`Value` is a variant of null, boolean, signed 64-bit integer, double, string,
`Address`, and `ObjectRef`. Use explicit `std::int64_t` for integer values.
`kind` and `require<T>` inspect values; a mismatched access throws `VMError`.

`Signature::result == ValueKind::Null` denotes void. Void returns have zero
operands and produce no caller operand/register value. Execution still completes
with an engaged `state.result` containing null. Null may also be an argument,
constant, or heap payload.

`Environment` owns:

- globals keyed by nonempty names;
- native bindings with exact runtime-kind signatures;
- append-only heap cells, with explicit release and stale-address rejection;
- append-only objects with copied `MetaObject` schemas and named fields.

Heap/object handles belong to one environment. Heap slots are never reused;
released cells cannot be accessed. `store` may change a cell's runtime kind.
Object fields must match their declared kinds. Methods map to native symbols;
`invoke` prepends the receiver to the explicit argument list. Native bindings may
be replaced; binding metadata is copied before a callback executes.

Borrowed references from environment containers may be invalidated by mutation.
Execution snapshots own values and frame storage, but retain environment-local
heap handles. Copying a snapshot does not copy or roll back environment effects.
Heap cells and objects persist until release/environment destruction; this layer
does not automatically trace or collect object cycles.

## Instruction fields

`Index` is `std::size_t`; `no_index` is its maximum value. Code positions are
instruction indices, distinct from heap addresses and function indices.

| Field | Meaning |
| --- | --- |
| `opcode` | Entry in the declarative opcode metadata table |
| `destination` | Register-model result slot; absent for stack code and effects |
| `operands` | Register input slots, in semantic argument order; empty for stack code |
| `index` | Constant index, local slot, or bytecode callee index |
| `target`, `alternate` | Jump/true target and branch false target |
| `argument_count` | Dynamic input count for calls and returns |
| `bits` | Checked signed arithmetic width, 1–64; default 64 |
| `symbol` | Global, field, or native symbol |

`Constant` indexes the module pool. `Move` copies one value. Stack-only operations
are `LocalGet`, `LocalSet`, `Pop`, `Dup`, and `Swap`. Scalar operations mirror the
shared IR's arithmetic/comparison operations. `Select` takes `(boolean, true,
false)`. Memory operations take `(address)` or `(address, value)`; object field
operations take `(object)` or `(object, value)`. `Call` uses a function index;
`NativeCall` uses a declared symbol. `Return` consumes zero or one value according
to the function result signature.

Stack operations consume the top N values in their original left-to-right order.
Arguments initialize locals and are not initially on the operand stack. Each
activation has its own operand stack. Calls can cross VM models in either
direction; value results are pushed to a stack caller or written to the recorded
register destination.

## Verification and scalar semantics

`Module::verify` returns positioned `ir::Diagnostics`. It checks every instruction
for opcode/model validity, arity, indices, branch targets, fall-through outside
the tape, destination presence, and native/callee declarations. Reachable
dataflow additionally checks:

- equal operand heights at joins;
- underflow and exact operand count at stack returns;
- definite assignment of registers and locals across all incoming paths.

Parameters are initially assigned. Other slots are unavailable until written.
Unreachable instructions receive structural checks, but no initial availability
or stack-depth assumptions. Dynamic kinds remain runtime-checked. The verifier
has a configurable resource/evaluation limit (default 1,000,000).

Integer arithmetic traps on overflow, zero division, and invalid shifts using
`sema::checked_integer`. Right shifts explicitly sign-extend. Left shifts require
nonnegative values. Bitwise results are masked/sign-extended to the declared
width without overflowing signed intermediates. Boolean bitwise operations use
logical semantics; boolean arithmetic is rejected. Floating operations use host
binary64 arithmetic/comparisons, including IEEE infinities, signed zero, and NaN.
No implicit scalar conversions occur.

Runtime failures throw `VMError` with `code` and `CodeAddress position`; native
callback exceptions propagate. Common codes include `vm.type`, `vm.arity`,
`vm.overflow`, `vm.divzero`, `vm.shift`, `vm.address`, `vm.uninitialized`,
`vm.call.limit`, and `vm.stack.limit`. A runtime trap is not a resumable budget
exit; effects already performed in the environment are retained.

## Execution and snapshots

`ExecutionLimits` bounds instruction count, activation depth, and operand depth.
`execute` and VM `run` validate the module before starting. `ExecutionResult`
distinguishes `Returned` and `Budget`; `state.result` is engaged only on return.
Budget exhaustion occurs between instructions, including inside recursive callees.
An instruction budget of zero creates the initial activation without executing.

`ExecutionState` owns frames, locals, registers, operand stacks, the final value,
and a cumulative instruction counter. `resume` accepts the state by value and
applies a fresh per-invocation instruction budget. A caller can therefore retain
the earlier snapshot. Resume requires the same unchanged module and the same
environment/handle domain. Snapshots do not embed module identity or environment
storage. State validation checks frame/PC/slot shapes and return destinations;
restored value kinds and slot availability are checked when used.

`step` is the low-level single-instruction API for a verified module and valid
state. Its caller enforces the instruction budget. An optional `Observer` sees
the code position and instruction immediately before dispatch; observers must
not mutate the module or execution state.

## Binary, loader, and cache

FSVM v1 encoding is deterministic and uses:

1. four magic bytes `FSVM`, then a little-endian u64 version (`1`);
2. u64 constant count and tagged constants;
3. u64 native declaration count and `(name, signature)` pairs;
4. u64 function count and function records.

Kinds, opcodes, models, booleans, and widths use one byte. Indices/counts use u64;
all-ones encodes `no_index`. Signed constants use two's-complement bit patterns.
Floating constants preserve IEEE binary64 bits. Strings use a u64 byte length
followed by raw bytes. A signature stores result kind, parameter count, and kinds.
A function stores name, model, signature, register/local counts, and instructions.
Each instruction stores opcode, destination, operand count/indices, index, target,
alternate, argument count, width, and symbol, in that order.

`decode` rejects bad magic/version, truncation, invalid tags, host-index overflow,
duplicate declarations/canonical constants, trailing input, and invalid modules.
Default `BinaryLimits`: 64 MiB total bytes, 1,000,000 cumulative items, 1 MiB per
string. Limits are configurable. Heap/object references cannot be constants.
Non-floating constants are interned by value; floats retain distinct entries to
preserve signed zero and NaN encodings.

`BytecodeCache` is entry-count-bounded LRU storage keyed by exact bytes and decode
policy. Cache hits return shared immutable modules. Eviction does not invalidate
existing shared owners. `Loader` installs named modules transactionally and can
check all native declarations against an `Environment` using `link`. Native
calls also check their binding signatures at runtime.

## Portable threaded representations

`ThreadedHandler` takes `(instruction, pc)` and returns the next instruction index
or `no_index` to finish. `run` returns next position, dispatch count, and completion
status under a caller-selected budget.

- Table threading resolves a handler by opcode at each dispatch.
- Direct threading copies the resolved handler into each instruction entry.
- Indirect threading follows shared handler cells, allowing rebinding.
- Subroutine threading binds each instruction into a callable host subroutine.

These are standard C++ callable representations; host machine addresses and
computed-goto labels are not serialized. Handlers implement their own state
effects and control transfers. `SubroutineThgreadedCode.hpp` retains the original
spelling as a compatibility include/type alias.

## Compilation backends, guards, and transfer

`AOT` owns an immutable module and invokes an `EntryCompiler` for each function.
Empty entries use interpreter fallback. Installation is transactional across all
functions; a throwing compiler preserves the previous entry set. `bytecode`
exports the portable baseline module, not native code pointers.

`MethodJIT` counts entry invocations, attempts compilation at a configurable
threshold, and caches accepted entries. Declined entries continue interpreting
and can be retried. `profile` exposes invocations, compilation attempts, and
compiled invocations. `invalidate` removes one entry. Tiering applies to host
entry calls; internal bytecode calls use the shared interpreter.

`CompiledEntry` receives arguments, environment, and limits, returning an
`ExecutionResult`. Backend results are checked for argument/result kinds,
stop/state agreement, frame structure, root activation, and instruction budget.
Backends must preserve bytecode semantics, count equivalent executed instructions,
and return baseline-compatible snapshots on budget exits. Captured code/module
storage must outlive the callback. Native entry ownership, ABI lowering, executable
memory, relocation, and cache flushing belong to the backend.

`TraceJIT::record` collects a bounded dynamic instruction path and position heat
counts. Trace selection/compilation/entry are explicit host operations. `compile`
validates recorded positions and asks a `TraceCompiler` for a `TraceBody`. `enter`
checks the current code position and guards before invoking that body. A failed
entry/guard returns the unchanged state with a reason. A body returns a `DeoptResult`
containing a valid baseline state, optimization status, and an exit reason.
Progressed side exits must include all environment effects and reconstructed
baseline values; entry guards must be tested before speculative effects.

`Guard` checks an active-frame slot's kind and optional exact value. `OSRPlan`
specifies a target activation/PC, parallel slot transfers, and reconstructed stack
operands. `transfer_osr`/`deoptimize` read every source from the old frame, reject
unavailable/duplicate destinations, preserve caller frames and return destinations,
and validate the new frame shape. The host supplies complete liveness/state maps
for the chosen PC and preserves the activation's result kind.

Mutable modules, environments, profiles, and caches require external
synchronization. Installed modules are shared immutable snapshots.
