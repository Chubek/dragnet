# Chapter 17. VMWeave VM Generation

[Previous: Native interoperability](16-exolayer-native-interoperability.md) · [Contents](README.md) · [Next: C/C++ embedding](18-c-and-cpp-embedding.md)

## 17.1 A declarative VM specification compiler

VMWeave compiles Lua descriptions through a C++/Kaguya frontend into a validated
model, versioned STK-00, and deterministic optional C99 runtime components.
Mutable execution state belongs to an explicit VM struct,
so independent instances can exist without hidden global state.

The current API, component guide, examples, and native requirements are in
[vmweave/README.md](../vmweave/README.md). The intermediate format is defined in
[STK-00.md](../vmweave/STK-00.md). The builder API below remains supported;
standalone Lua `emit` calls invoke `vmweave-cli` (set `VMWEAVE_EXECUTABLE` or place
it on PATH). Lua remains declarative; the C++ library owns generation.

This separation is useful when building a small interpreter around Limestone.
The DSL defines the execution skeleton; Metacode can define an independently
versioned bytecode contract; Bin2Bin can translate declared regions; an installer
can make them callable under the VM's guest-state ABI.

## 17.2 Loading and creating a description

Add the module directory to Lua's search path, or extend `package.path` inside
your generator script:

```lua
local vmweave = require("vmweave")
local vm = vmweave.vm("Counter")
```

Use colon calls for state, instruction, and generation methods. The implementation
checks the receiver, so `vm.state(...)` is not a substitute for
`vm:state(...)`.

VM and declaration names must be non-reserved C identifiers beginning with an
alphabetic character, followed by letters, digits, or underscores. C keywords
are rejected. Names become generated C identifiers, not arbitrary quoted labels.

Construction-time validation makes invalid declarations fail before C emission.
Lua assertions carry the error; callers can use Lua's normal protected-call
mechanism if they need to present those errors in a larger generator application.

## 17.3 State declarations and types

```lua
vm:state("value", {type = "u64"})
vm:state("pc", {type = "u64"})
vm:state("registers", {type = "i64", count = 8})
vm:state("host", {type = "ptr"})
```

Supported state types map to ordinary C representations:

| DSL type | C type |
| --- | --- |
| `u8/u16/u32/u64` | Fixed-width unsigned integers |
| `i8/i16/i32/i64` | Fixed-width signed integers |
| `f32/f64` | `float` / `double` |
| `ptr` | `void *` |
| `size` | `size_t` |

`count` declares a fixed array and must be a positive integer within the
implementation's accepted count range. Duplicate state names, unknown types,
unknown metadata keys, and malformed counts fail construction.

State declaration order is not a storage-layout guarantee: fields are emitted in
sorted name order for deterministic output. An embedding ABI depending on exact
VM struct layout must use the generated declaration or a separately versioned
layout contract.

## 17.4 Instruction declarations

```lua
vm:instruction("increment", {
  opcode = 1,
  body = "  vm->value += 1u;"
})
vm:instruction("reset", {
  opcode = 7,
  body = "  vm->value = 0;"
})
```

An instruction has an optional nonnegative 31-bit opcode and optional C source
body. Names and explicit opcodes must be unique. Bodies must be strings without
embedded NUL. The DSL copies validated declaration attributes and rejects
unrecognized keys.

Bodies are caller-supplied C, preserved in STK-00 foreign-C primitives. VMWeave
checks its stack primitives/effects but does not define arithmetic-overflow
behavior or infer guest memory semantics. In this
example, unsigned counter arithmetic has C's defined modular behavior. A checked
guest arithmetic model would require an explicit handler implementation.

An omitted body produces an ordinary no-op handler skeleton. Handlers always
receive the VM pointer and return void; richer error/state conventions can be
represented in explicit VM fields and embedding code.

## 17.5 Plain handler generation

```lua
local source = vm:emit()
```

The default output includes standard integer/size headers, the VM state typedef,
and one ordinary handler function per instruction. For `Counter`, handlers are
named `Counter_increment`, `Counter_reset`, and so on, with a `Counter_vm *`
parameter.

Plain output provides initialization and handlers. An embedder can call handlers,
build its own dispatch table, or wrap them in a separately defined execution loop.
The ordinary function boundary makes these policies compositional.

An empty state receives a placeholder member so the generated C struct is valid.
Declarations and functions use stable name ordering. Output contains no hidden
mutable globals or runtime-selected ordering.

## 17.6 Switch dispatch and opcode assignment

```lua
local source = vm:emit({dispatch = "switch"})
```

Switch mode adds `Counter_init`, `Counter_step`, and named opcode macros such as
`Counter_opcode_increment`. Explicit opcodes are honored. Missing opcodes are
assigned from available numbers in sorted handler-name order.

Initialization zeroes state and hooks. `step` rejects a NULL VM or unknown opcode
with `-1`. A known opcode dispatches its handler and returns zero after ordinary
completion. The `run` API fetches decoded tapes and advances the PC; this legacy
example invokes individual handlers without selecting tape/compiler components.

`init` and `step` are reserved handler names when dispatch is enabled. Unknown
dispatch strategies and generation keys fail validation. Automatic opcode
assignment is reproducible for a fixed description, but adding a handler can
change implicitly assigned values. Use explicit opcodes for a versioned external
bytecode format.

## 17.7 Runtime hooks

```lua
local source = vm:emit({dispatch = "switch", hooks = true})
```

Hooks require dispatch and add:

```c
void *vmweave_userdata;
int (*vmweave_before)(struct Counter_vm *, uint32_t, void *);
void (*vmweave_after)(struct Counter_vm *, uint32_t, void *);
```

Unknown opcodes are rejected before either hook or handler executes. For known
opcodes, a nonzero before-hook status prevents execution and is returned by
`step`. Otherwise the handler runs, followed by the optional after hook.

These hooks can observe hot entries, instrument state transitions, consult an
embedding memory manager, or call a Bin2Bin runtime. A nonzero before status is
an embedding-defined control result; VMWeave does not automatically interpret it
as compiled execution or deoptimization.

Hook field names are reserved in hooked output. Userdata remains embedding-owned,
and initialization clears it along with the function pointers. Configure hooks
after initialization, and keep their code/state alive while a VM uses them.

## 17.8 A complete generator script

Save this as `/tmp/opencode/counter.lua`:

```lua
local vmweave = require("vmweave")
local vm = vmweave.vm("Counter")
vm:state("value", {type = "u64"})
vm:instruction("increment", {opcode = 1, body = "  vm->value += 1u;"})
vm:instruction("reset", {opcode = 7, body = "  vm->value = 0;"})
io.write(vm:emit({dispatch = "switch", hooks = true}))
```

Generate from the repository root:

```sh
VMWEAVE_EXECUTABLE=build/vmweave/vmweave-cli LUA_PATH='./vmweave/?.lua;;' lua /tmp/opencode/counter.lua \
  > /tmp/opencode/counter-vm.c
```

The double semicolon preserves Lua's default search path. Use your installed Lua
interpreter name if it differs. An installed VMWeave module resides under the
Limestone data directory; adjust the search path to that location for consumers.

## 17.9 A generated-C consumer

Save next to the generated file as `/tmp/opencode/counter-main.c`:

```c
#include "counter-vm.c"
#include <stdio.h>

int main(void) {
  Counter_vm vm;
  Counter_init(&vm);
  if (Counter_step(&vm, Counter_opcode_increment) != 0) return 1;
  if (Counter_step(&vm, Counter_opcode_increment) != 0) return 1;
  printf("%llu\n", (unsigned long long)vm.value);
  return vm.value == 2 ? 0 : 1;
}
```

```sh
cc -std=c99 /tmp/opencode/counter-main.c -o /tmp/opencode/counter
/tmp/opencode/counter
```

Including the generated source in this small example places definitions in one
translation unit. A larger project can organize generated declarations and
definitions according to its normal build conventions, without compiling the
same definitions twice.

## 17.10 Connecting translation and execution

A VM hook can identify a guest region, prepare it through Bin2Bin, and use a valid
compiled handle under an explicit state-transfer ABI. The hook's before status
can then tell the host execution loop that the ordinary handler path was bypassed.
That status convention is owned by the embedding VM.

The VM must define region boundaries, guest PC advancement, incoming/outgoing
state, exception/trap handling, bytecode mutation invalidation, and native
installer ownership. Metacode should describe the bytecode semantics when those
semantics are shared with translation. Handler C bodies and ISA metadata need
independent behavior tests so they remain consistent.

Optional memory/object/frame/module/rewrite/cache components are available; see
the component guide for ownership and validation contracts. Allocators and
executable mappings are explicit choices. VMWeave supplies a MachineIR native C
backend for assembly, AOT shared images and baseline JIT execution on POSIX hosts.
Authoritative C handlers become explicit foreign-runtime bindings in the
MachineIR CFG and are compiled by the configured host C compiler.

```sh
build/vmweave/vmweave-cli --native \
  --program vmweave/examples/calculator.tape \
  -o build/calculator-native vmweave/examples/calculator.lua
```

The generated AOT header provides `Calculator_run_native(vm, budget)`. C++ callers
can use `vmweave/native.hpp` and owning `NativeProgram` handles. Generated VMs
selecting `jit` can define `VMWEAVE_ENABLE_NATIVE` and call `NAME_jit_native_bind`
to install the built-in compiler/executor/releaser. Link `Limestone::vmweave`.
State size/ABI fingerprints are checked before execution; native handles own tape
snapshots and loaded code. The component guide documents options, statuses,
saved MachineIR envelopes and lifetime requirements.

## 17.11 Determinism and tests

The generator sorts state and instruction names rather than relying on Lua table
iteration order. Equivalent declarations with the same metadata generate the same
C. Explicit opcode uniqueness and reserved-name checks are part of construction.

The repository test loads the DSL, checks deterministic output and invalid
declarations, compiles generated C, and executes dispatch/hook behavior when Lua
and a C compiler are available. When adding a generation concept, validate both
the Lua declaration and the resulting C semantics.

Independent VM instances share generated immutable code but own mutable state.
Synchronizing a shared VM belongs to the embedder, just as synchronizing a shared
Bin2Bin runtime or Exolayer context does.

[Previous: Native interoperability](16-exolayer-native-interoperability.md) · [Contents](README.md) · [Next: C/C++ embedding](18-c-and-cpp-embedding.md)
