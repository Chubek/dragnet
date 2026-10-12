# VMWeave

VMWeave compiles declarative Lua VM descriptions into a validated owning model,
versioned **STK-00**, and embeddable **C99** runtimes. C instruction bodies are the
authoritative semantics. The frontend uses `third_party/kaguya`; Lua does not
implement a second code generator.

```text
Lua description -> VM model -> STK-00 -> C components
                                     -> MachineIRAdapter -> MachineIR exchange
```

## Build and generate

VMWeave requires Lua 5.4 or 5.3 development headers/libraries. Kaguya in this
checkout does not support Lua 5.5's changed GC API.

```sh
cmake -S . -B build
cmake --build build -j 4
build/vmweave/vmweave-cli --generate -o build/calculator vmweave/examples/calculator.lua
build/vmweave/vmweave-cli --emit-stk vmweave/examples/calculator.lua
build/vmweave/vmweave-cli --emit-c build/calculator/Calculator.stk00
ctest --test-dir build -R vmweave --output-on-failure
```

`--check` validates a specification. `--emit-c` prints a self-contained translation
unit. `--generate` produces a common header, component-assembly `.c`, `.stk00`,
and **only the selected canonical component files**. Output is deterministic.
Generate into a fresh directory when changing component selections: older
artifacts from other invocations are not deleted.

## Lua API

Specifications return `vmweave.vm { ... }`. Raw description tables are also
accepted. The CLI loads the embedded module without custom Lua search paths.

```lua
local v = require("vmweave")
return v.vm {
  name = "Small", execution = "switch",
  stack = { cell = "int64_t", capacity = 64 },
  components = { "token", "opcode", "insncode", "dispatch", "tape", "compile" },
  instructions = {
    { name="PUSH", opcode=1, operands={"cell"}, stack_effect="( -- value )",
      semantics="vm_push(operand0);" },
    { name="HALT", opcode=2, flow="halt", semantics="vm_halt();" },
  },
}
```

| Description attribute | Meaning/default |
| --- | --- |
| `name` | Required non-reserved C identifier |
| `execution` | switch (default), subroutine, indirect, direct, none |
| `stack` | `cell`: intptr_t (default), int64_t, uint64_t; `capacity`: 1024 |
| `fields` | Dense array of `{name, type, count?}` state declarations |
| `instructions` | Dense array of instruction declarations |
| `components` | Default: token/opcode/insncode/dispatch/tape/compile |
| `hooks` | Before/after observation, default false |
| `memory` | `{allocator="custom"}`; system/kalloc/jemalloc/memtkx/mimalloc also accepted |
| `frame_capacity`, `ipc_capacity` | Both default to 64 |
| `subsystems` | Map of selected component name to additional C source |
| `rewrites` | Ordered explicit equivalence assertions |
| `source` | Optional `{file, line}` provenance override |

Capacities and field array counts are positive integers up to 1,048,576. State
types are u8/u16/u32/u64, i8/i16/i32/i64, f32/f64, ptr, size. Fields starting with
`vw_` or `vmweave_` and handlers colliding with generated APIs are reserved.
Fields/instructions are sorted by name. Missing opcodes take the first unused
nonnegative integer in that order. Explicit opcodes must be unique and within
0..2147483647; pin them for persistent bytecode formats.

Instructions accept `name`, `opcode`, `operands`, `semantics` (legacy alias
`body`), `stack_effect`, `flow`, and `source`. At most 16 operands are supported,
each `cell` or `label`. Flow is next/jump/branch/call/return/halt. Default body is
empty, effect `( -- )`, flow next. Supply accurate effects: dispatch checks input
depth, output capacity, and final depth. Table declarations retain file-level
provenance; an explicit `source` can supply precise lines when Lua tail calls
remove the caller frame.

| C semantic operation | Meaning |
| --- | --- |
| `cell`, `operand0` ... `operand15` | Cell type and decoded operands |
| `vm_pop()`, `vm_push(value)` | Checked data stack |
| `vm_operand(index)` | Checked operand lookup |
| `vm_jump(index)`, `vm_halt()` | Instruction-index branch / successful halt |
| `vm_fail(status)` | Sticky VM error; zero maps to -1 |
| `vm_call(index)`, `vm_return()` | Checked frames; requires `.frame` |
| `vm_alloc(bytes)`, `vm_free(pointer)` | Memory callbacks; requires `.memory` |

Handlers receive `NAME_vm *vm` and return void. Their C bodies, including arithmetic
and host calls, are preserved losslessly through STK-00 and emitted with `#line`
provenance. Arithmetic follows C rules; the calculator explicitly checks division
traps. Instruction meaning is never inferred from a mnemonic.

### Existing builder API

```lua
local v = require("vmweave")
local vm = v.vm("Counter") -- plain handlers, no dispatcher
vm:state("acc", {type="u64"})
vm:instruction("increment", {body="vm->acc += 1u;"})
io.write(vm:emit({dispatch="switch", hooks=true}))
```

Methods use colon calls and copy their metadata. Within the C++ loader, `emit`
calls the generator through Kaguya. Standalone Lua invokes `vmweave-cli`; set
`VMWEAVE_EXECUTABLE` and add `vmweave/?.lua` to `LUA_PATH`. Errors propagate through
`pcall`. Initialization is available in plain and dispatched output.

## Generated runtime

```c
#include "Calculator.h"
int main(void) {
    Calculator_vm vm;
    Calculator_instruction code[32];
    Calculator_tape tape = {code, 0, 32};
    Calculator_init(&vm);
    if (Calculator_compile("PUSH 10 PUSH 20 ADD HALT", &tape)) return 1;
    if (Calculator_run(&vm, &tape, 100)) return 2;
    return vm.vw_sp == 1 && vm.vw_stack[0] == 30 ? 0 : 3;
}
```

Compile the consumer and generated `Calculator.c` with `cc -std=c99 -I
build/calculator`. Alternatively include the `.c` in one translation unit.
Fragments can be compiled independently with their common public header.

The tape is caller-owned decoded instructions, each containing an opcode, operand
count, and operands. Compilation accepts whitespace-separated mnemonics, base-0
integers, `#` comments, and `name:` / `&name` labels resolving to instruction
indices. Limits are 256 labels and 127 bytes per token. Validation precedes writes;
failure preserves the tape. Unsigned cells accept the full uint64 range and reject
negative literals. Label operands must target existing instructions.

`run` validates the entire tape, advances the PC before the handler, and enforces
an instruction budget. Halt/falling off the end returns zero. Budget exhaustion
is resumable. Errors preserve the failing PC and already-performed state mutations.
Initialization clears sticky errors. Release owned objects before reinitializing;
do not mutate executing tapes. Mutable state belongs to each VM instance.

Statuses: 0 success; -1 invalid argument/opcode; -2 underflow; -3 capacity exceeded;
-4 malformed operand/tape/label or bounds violation; -6 stack-effect mismatch;
-7 budget exhausted; -8 allocation failure/missing allocator; -9 unsupported
operation/missing native adapter. Semantics/hooks may use additional statuses.
Before hooks run after validation and can veto execution with a nonzero status.
After hooks observe handler completion, including semantic errors.

### Execution models

* **Switch**: opcode switch selects the handler.
* **Subroutine**: `.srtbl` maps opcodes to callable routines.
* **Indirect**: `.addrtbl` maps opcodes to address slots, dereferenced to obtain
  callable handlers (portable C indirect-call threading).
* **Direct**: `.insntbl` prepares handler addresses stored **in the stream**.
  Use `NAME_thread(tape, buffer, capacity)`, then
  `NAME_run_direct(vm, buffer, tape->size, budget)`. Opcodes alongside addresses
  are validation/hook metadata. `step` remains available for isolated execution.

Table selections must match their execution model; threaded dispatch requires its
table. This implementation uses portable function addresses, not computed-goto
labels. The direct loop calls the stored address without token dispatch.

## Optional components

| Component | Generated facility |
| --- | --- |
| token/opcode | Mnemonics / numeric definitions |
| insncode/dispatch | Authoritative handlers / selected execution loop |
| srtbl/addrtbl/insntbl | Distinct threading tables |
| tape/compile | Validation / transactional assembly and label resolution |
| memory | Allocation/resize/release callbacks |
| object | Owned tagged payloads, bounded access, individual/bulk reclamation |
| atomic | Integer/float/Boolean/reference/nil tagged values |
| frame | Checked calls/returns, saved PC/stack base, 16 locals per frame |
| module | Borrowed module loading, export lookup, duplicate-symbol/tape validation |
| ipc | Bounded per-instance message ring; embedder supplies synchronization |
| rewrite | Declared shrinking straight-line transformations |
| optim | Epoch-invalidated inline cache |
| jit | Baseline native compilation/execution/handle release, plus custom backend callbacks |

Compile/jit require tape; object requires memory; call/return require frame.
Components can be generated independently; consumers provide omitted handlers
when linking tables/dispatch alone. Subsystem extensions are preserved in STK-00
foreign-C primitives and appended to selected fragments.

### Allocators and object lifetime

`NAME_memory_set(vm, allocator)` installs borrowed callbacks/context. No allocator
is silently selected. `memory.allocator="system"` generates an explicit
`NAME_memory_system(vm)` opt-in. Do not rebind with outstanding allocations;
objects reject rebinding while their owned list is nonempty.

Isolated `adapters/` integration headers provide:

* `VMWEAVE_KALLOC_ADAPTER(NAME)` / `NAME_memory_kalloc(vm, arena)`;
* `VMWEAVE_MIMALLOC_ADAPTER(NAME)` / `NAME_memory_mimalloc(vm)`;
* `VMWEAVE_JEMALLOC_ADAPTER(NAME)` / `NAME_memory_jemalloc(vm)`;
* C++ `adapters::Memtkx<Space>::bind<NAME_allocator>()` for nonmoving DomMEMTk
  spaces such as `FreeListAllocator`.

Include the generated header before a C adapter and build/link the allocator
with its own include paths. Allocators are not linked into the generator. The
specification selector records intent; callbacks establish the actual binding.
Memtkx tracks sizes and preserves existing storage on failed resize. Contexts and
arenas must outlive allocations. Moving collectors need VM-specific relocation.

Objects allocate solely through those callbacks. Zeroed payloads represent
strings, arrays, or other user layouts. Read/write helpers check ranges.
`object_delete` unlinks one allocation; `object_clear` reclaims the entire list.
This is explicit lifetime management, not an inferred tracing collector. Roots,
barriers, tracing, and safepoints belong in memory/object extensions.

### Rewriting

```lua
rewrites = {{from={"DUP","DROP"}, to={}, equivalent=true}}
```

The author asserts semantic equivalence. Validation checks known operand-free
straight-line instructions, strict shortening, equal stack delta, and replacement
requirements/peaks that cannot increase. Rules run in declaration order and
restart after a match; shortening ensures termination.

`NAME_rewrite(tape)` accepts straight-line tapes intended to start with an **empty
stack**. Original stack/capacity checks precede modification. Halt must be last;
branches/calls are rejected. This preserves error preconditions when removing
`DUP DROP`. Invalidate caches/native handles after code changes. Tests compare
optimized and reference execution; no equivalence is inferred from names.

## C++ and native integration

`vmweave/vmweave.hpp` exposes `load_lua/load_file`, `validate`, `lower`,
`print_stk/parse_stk`, `emit_c`, and `generate`, using owning models and `Result<T>`.
Kaguya types remain private. Installed consumers link `Limestone::vmweave`.

`MachineIRAdapter::lower(Module)` must compile authoritative foreign-C primitives
and return the existing `metacode/machine-ir` region-exchange format.
`emit_machineir` validates/canonicalizes through `machineir_bridge`. Target
metadata, assembly, and encoding remain in downstream MachineIR backends.

### Built-in native compilation

`vmweave/native.hpp` exposes `assemble`, `lower_native`, `emit_native_assembly`,
`compile_native`, and the owning `NativeProgram` handle. The lowering path is:

```text
STK-00 + validated tape
        -> MachineIR CFG with explicit foreign calls, branches, and returns
        -> metacode/machine-ir native C backend
        -> host C compiler -> assembly / shared native image
```

The MachineIR entry has SSA values and conservative memory/call/trap effects.
Each foreign binding invokes the same generated C handler as the interpreter;
arbitrary C99 semantics and subsystem extensions are compiled by the C toolchain.
Names never imply semantics. The backend owns ABI lowering, compilation, loading,
and executable lifetime. It does not need an application-provided semantic frontend.

```sh
build/vmweave/vmweave-cli --native \
  --program vmweave/examples/calculator.tape \
  -o build/calculator-native vmweave/examples/calculator.lua
```

This writes `Calculator.h`, `Calculator.stk00`, `Calculator.machineir.json`,
`Calculator.s`, and `Calculator.so`. The AOT header includes
`Calculator_run_native(vm, budget)`, which checks the compiled state layout before
calling the entry. Link the shared image when building your host program:

```c
#include "Calculator.h"
int main(void) {
  Calculator_vm state;
  Calculator_init(&state);
  return Calculator_run_native(&state, 100) || state.vw_stack[0] != 30;
}
```

```sh
cc -std=c99 -I build/calculator-native main.c \
  build/calculator-native/Calculator.so -o build/calculator-native/main
build/calculator-native/main
```

`--emit-machineir` and `--emit-assembly` write their outputs to stdout. These
modes and `--native` accept Lua or STK-00; native modes also accept saved
`*.machineir.json` envelopes. The envelope owns its tape and runtime bindings;
recompiling it needs neither the original Lua file nor a VMWeave semantic adapter.
Without `--program`, the image contains the selected VM runtime and all handlers,
with an empty specialized entry tape. `--cc`, repeated `--cflag`, and repeated
`--ldflag` configure the toolchain using separate arguments, without shell parsing.

For C++ embedding:

```cpp
#include <vmweave/native.hpp>
#include "Calculator.h"
using namespace limestone::vmweave;
// Check Result<T> errors at each step in application code.
auto module = lower(load_file("vmweave/examples/calculator.lua").value()).value();
auto tape = assemble(module, "PUSH 10 PUSH 20 ADD HALT").value();
auto program = compile_native(module, tape).value();
Calculator_vm state;
Calculator_init(&state);
auto status = program.execute(&state, sizeof(state), Calculator_vw_abi(), 100);
```

`execute` checks state size and the generated ABI fingerprint before entering
native code. The fingerprint covers field types, offsets, sizes and VM alignment;
packing/layout differences fail explicitly. Runtime status codes, hook order, PC,
stack, frames, errors and budgets follow the selected interpreter model.
Budget exhaustion returns -7 and preserves the resumable PC. Tape operands are
exact 64-bit cell patterns, sign-extended for signed cells. Compilation snapshots
the tape; caller mutations require a new compilation. Handles are shareable and
keep their loaded library alive until the final owning copy is released. Borrowed
image/IR views live as long as their handle; synchronize mutation of a shared VM.

### Generated C JIT binding

Define `VMWEAVE_ENABLE_NATIVE` when including the generated `.c` or `.jit` and
link `Limestone::vmweave` (use a C++ linker for the library). For a VM selecting
the `jit` component:

```c
#define VMWEAVE_ENABLE_NATIVE
#include "ObjectVM.c"

ObjectVM_jit_backend backend;
vmweave_native_context *context = NULL;
void *handle = NULL;
/* Check each returned status in application code. */
ObjectVM_jit_native_bind(NULL, &context, &backend);
ObjectVM_jit_compile(&backend, &tape, &handle);
ObjectVM_jit_execute(&backend, handle, &state);
ObjectVM_jit_release(&backend, handle);
vmweave_native_context_destroy(context);
```

The binder installs compile/execute/release callbacks backed by the built-in
MachineIR pipeline. `vmweave/native.h` also provides opaque C context/program APIs
and compiler diagnostics through `vmweave_native_error`. Contexts own copied
toolchain options and outlive calls using their backend; programs own independent
code storage. Custom `MachineIRAdapter` and generated JIT callbacks remain
available. Missing callbacks return -9; partial handles are released on failure.

The built-in executable backend requires a POSIX host with `fork/exec` and
`dlopen`, and a GCC/Clang-compatible C compiler. Defaults use the CMake-selected
compiler, a 60-second compilation timeout, at most 4096 tape instructions, a
4-MiB serialized MachineIR unit, and a 32-MiB artifact limit. C JIT execution
defaults to one million steps; `vmweave_native_options` overrides it. C/native
errors add -11 (ABI mismatch), -12 (compiler failure), and -13 (compiler timeout).
Compilation diagnostics preserve original instruction locations. External C
functions require appropriate compile/link arguments. Assembly can use a
cross-compiler; executable loading requires a host-compatible image and ABI.
Target instruction selection/encoding is supplied by this explicit toolchain
adapter; Infobank target properties are not fabricated.

Dynamic module file formats/loading, tracing/moving GC, richer frames/object
layouts, and specialization policies use explicit subsystem extensions. Built-in
facilities above are functional minimal models.

## Validation

Tests cover Lua validation/legacy compatibility, deterministic component selection,
STK round trips/malformed input, MachineIR adapter validation, generated C across
all execution models, arithmetic/control flow, stack/operand/budget errors,
transactional compilation, object allocation/lifetime, frames, module exports,
atomic values, IPC, cache invalidation, rewrite/reference agreement, missing JIT
backends, and actual DomMEMTk allocation/resize behavior.
Native tests compare execution against the interpreter for arithmetic, branches,
traps, stack/budget errors and resumption; test C JIT calls/frames and tape
snapshot ownership; check arbitrary C bodies, ABI mismatch, compiler failures,
MachineIR round trips, and standalone AOT/saved-envelope output.

[STK-00.md](STK-00.md) is the serialization source of truth. Examples are source
specifications; generated artifacts live in build directories and are not edited.
