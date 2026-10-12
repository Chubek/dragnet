# AGENTS.md — VMWeave

## 1. Purpose

VMWeave is a virtual-machine generation system inspired by VM generators such as **VMGen** from Gforth.

VMWeave separates **VM specification** from **VM implementation**:

* **Lua** is used as the high-level specification language.
* **C/C++** implements the VMWeave generator and its runtime/compiler infrastructure.
* VM instruction semantics are primarily specified in C.
* VMWeave lowers those specifications into **STK-00**, a stack-oriented intermediate language.
* STK-00 can subsequently be compiled into C or into an assembly/machine-code target supported by `metacode/machine-ir`.

The intended pipeline is:

```text
                    VM specification
                           │
                           ▼
                    ┌─────────────┐
                    │   Lua DSL   │
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │   VMWeave   │
                    │   frontend  │
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │   STK-00     │
                    │ stack IR/DSL │
                    └──────┬──────┘
                           │
             ┌─────────────┴──────────────┐
             ▼                            ▼
       C code generation          Machine/assembly generation
                                          │
                                          ▼
                                 metacode/machine-ir
```

Agents modifying VMWeave MUST preserve this separation.

---

# 2. Core Concepts

VMWeave has four major conceptual layers.

## 2.1 VM Specification

The VM specification describes:

* instructions;
* instruction names/mnemonics;
* opcode assignments;
* operands;
* instruction semantics;
* registers/stacks;
* memory;
* objects;
* modules;
* execution model;
* optimization facilities;
* optional JIT facilities.

The specification is expressed primarily through Lua.

Lua is a **description language**, not the generated VM implementation language.

---

## 2.2 Instruction Semantics

Instruction semantics are defined in C.

For example, a conceptual instruction might be:

```c
VM_INSN(add)
{
    cell b = vm_pop();
    cell a = vm_pop();

    vm_push(a + b);
}
```

The exact API depends on the implementation, but the important architectural rule is:

> The semantic definition of an instruction is authoritative; generated VM code must be derived from it rather than duplicating its meaning elsewhere.

Instruction semantics may use VMWeave-provided operations for:

* stack manipulation;
* registers;
* memory access;
* control flow;
* object access;
* exception handling;
* allocation;
* calls;
* returns;
* VM state.

Users may also provide C source files for VM subsystems other than instruction semantics.

---

## 2.3 STK-00

STK-00 is VMWeave's generated stack-based intermediate language.

Generated VM components are represented in STK-00 before being lowered to a concrete implementation language.

STK-00 is deliberately Forth-like.

Conceptually:

```text
stack before:

    a b

operation:

    ADD

stack after:

    a+b
```

A representative STK-00 fragment might look like:

```stk-00
: ADD ( a b -- c )
    + ;

: SUB ( a b -- c )
    - ;

: PUSH ( x -- )
    stack-push ;

: POP ( -- x )
    stack-pop ;
```

The exact STK-00 syntax is defined by the implementation and MUST NOT be invented by an agent when modifying the project.

If STK-00 syntax changes, update all affected producers, consumers, examples, and documentation.

---

## 2.4 Machine IR

Assembly generation is performed through:

```text
metacode/machine-ir
```

Machine IR represents machine-level operations independently of the VM specification.

The intended lowering path is:

```text
STK-00
   │
   ▼
Machine IR
   │
   ├── C
   ├── x86-64
   ├── AArch64
   └── other supported targets
```

Agents MUST keep VM-level semantics separate from target-specific machine instructions.

Do not embed x86-64 or AArch64 assumptions into generic VMWeave code unless the code explicitly belongs to the Machine IR backend.

---

# 3. Language and Runtime Dependencies

VMWeave is implemented using both Lua and C++.

Lua is used for:

* VM specifications;
* declarative configuration;
* VM descriptions;
* instruction declarations;
* generation configuration.

C++ is used for:

* VMWeave's implementation;
* parsing/loading specifications;
* generation;
* intermediate representation handling;
* integration with machine IR;
* runtime support where appropriate.

Lua/C++ interoperability uses:

```text
third_party/kaguya
```

Kaguya is the binding layer between C++ and Lua.

Agents modifying the Lua/C++ boundary MUST follow the existing Kaguya conventions in the repository.

Do not introduce another Lua binding framework unless explicitly requested.

---

# 4. Memory Management

VMWeave permits the generated VM to use either user-supplied memory-management code or one of the supported allocators.

Supported allocator integrations include:

```text
third_party/klib/kalloc
third_party/jemalloc
third_party/memtkx
third_party/mimalloc
```

The VM specification may also provide custom C memory-management code.

The memory subsystem may implement:

* allocation;
* deallocation;
* arenas;
* object allocation;
* garbage collection;
* object lifetime;
* VM heaps;
* VM-specific memory abstractions.

Agents MUST NOT assume that the VM uses a particular allocator.

Generated code must isolate allocator-specific functionality behind the appropriate VMWeave memory interface.

---

# 5. Generated VM Components

VMWeave can generate a number of independent VM components.

Not every VM requires every component.

The canonical component names are:

| Extension   | Component              | Purpose                                          |
| ----------- | ---------------------- | ------------------------------------------------ |
| `.token`    | Token table            | Instruction mnemonics/tokens                     |
| `.opcode`   | Opcode table           | Numeric instruction opcodes                      |
| `.srtbl`    | Subroutine table       | Subroutine-threaded execution                    |
| `.addrtbl`  | Address table          | Indirect-threaded execution                      |
| `.insntbl`  | Instruction table      | Direct-threaded execution                        |
| `.insncode` | Instruction code       | Structured instruction definitions               |
| `.dispatch` | Dispatch code          | VM dispatch mechanism                            |
| `.ipc`      | IPC subsystem          | Inter-process/instruction communication support  |
| `.rewrite`  | Rewrite subsystem      | Instruction rewriting and peephole optimization  |
| `.compile`  | Compiler               | Compilation of instruction streams               |
| `.memory`   | Memory subsystem       | VM memory and allocation                         |
| `.module`   | Module subsystem       | Modules and loaders                              |
| `.jit`      | JIT subsystem          | Baseline/simple JIT support                      |
| `.tape`     | Code tape              | Representation of executable instruction streams |
| `.frame`    | Stack-frame subsystem  | Activation/stack frames                          |
| `.optim`    | Optimization subsystem | VM optimizations such as inline caches           |
| `.atomic`   | Atomic values          | Integers, floats, booleans, etc.                 |
| `.object`   | Object subsystem       | GC-managed complex objects                       |

The spelling above is normative.

In particular, avoid the historical misspellings:

```text
.dispatc
.frmae
```

Use:

```text
.dispatch
.frame
```

unless compatibility with an existing external format explicitly requires the old names.

---

# 6. Optional Generation

VMWeave MUST support VMs that require only a subset of the available components.

For example, a minimal stack VM might generate only:

```text
foo.token
foo.opcode
foo.insncode
foo.dispatch
foo.compile
```

A VM with objects and garbage collection might additionally generate:

```text
foo.memory
foo.atomic
foo.object
foo.module
```

A threaded-code VM may generate:

```text
foo.token
foo.opcode
foo.srtbl
```

or:

```text
foo.token
foo.opcode
foo.addrtbl
```

or:

```text
foo.token
foo.opcode
foo.insntbl
```

depending on the selected execution model.

Agents MUST NOT generate unused components merely because the component exists.

---

# 7. Execution Models

VMWeave supports multiple VM dispatch/execution strategies.

## 7.1 Switch Dispatch

A conventional interpreter may dispatch using a switch:

```c
for (;;) {
    switch (*ip++) {
    case OP_ADD:
        /* ... */
        break;

    case OP_SUB:
        /* ... */
        break;
    }
}
```

---

## 7.2 Subroutine Threading

Subroutine-threaded execution uses callable instruction implementations.

Conceptually:

```text
code:
    ADD
    PUSH
    MUL
    HALT
```

becomes a table or stream of instruction routines.

The `.srtbl` component represents this form.

---

## 7.3 Indirect Threading

Indirect threading uses an address table:

```text
opcode
  │
  ▼
address table
  │
  ▼
instruction implementation
```

The `.addrtbl` component represents this form.

---

## 7.4 Direct Threading

Direct threading stores instruction addresses directly in the instruction stream.

The `.insntbl` component represents this form.

Agents must keep these execution models conceptually separate.

Do not silently transform one execution model into another without updating the relevant generator and documentation.

---

# 8. Instruction Representation

Each VM instruction should conceptually have:

```text
Instruction
├── mnemonic
├── opcode
├── operands
├── semantic implementation
├── stack effect
├── control-flow behavior
└── optional optimization metadata
```

For example:

```text
ADD

mnemonic:
    "add"

opcode:
    0x01

stack effect:
    ( a b -- a+b )

semantics:
    pop b
    pop a
    push a+b
```

Instruction metadata may be used to generate:

* token tables;
* opcode tables;
* dispatch code;
* instruction code;
* compiler code;
* rewrite rules;
* optimization metadata.

---

# 9. Stack Effects

Where possible, VM instructions SHOULD document their stack effects.

Use Forth-style notation:

```text
( before -- after )
```

Examples:

```text
( a b -- a+b )
( x -- )
( -- x )
( a b -- a b a )
```

For example:

```text
ADD: ( a b -- a+b )
DUP: ( a -- a a )
DROP: ( a -- )
SWAP: ( a b -- b a )
```

Stack effects are documentation and may also be consumed by static validation or optimization passes.

If the implementation has a machine-readable stack-effect representation, documentation and machine-readable metadata MUST agree.

---

# 10. VM Objects

Object-capable VMs may provide:

```text
.atomic
.object
.memory
.module
```

Atomic values include primitive VM values such as:

```text
integer
float
boolean
character
pointer/reference
nil/null
```

Complex objects may include:

```text
string
array
list
table
closure
module
user object
```

Objects that participate in garbage collection MUST be allocated through the VM's memory/object interface rather than directly through an arbitrary system allocator.

---

# 11. Code Tape

A VM may represent executable instructions using a **code tape**.

The code tape is an ordered sequence of VM instructions and their operands.

For example:

```text
PUSH 10
PUSH 20
ADD
PRINT
HALT
```

may conceptually become:

```text
+-------+-------+-----+-------+------+
| PUSH  |  10   | PUSH|  20   | ADD  |
+-------+-------+-----+-------+------+
```

The exact binary or textual representation is implementation-defined.

The `.tape` component is responsible for code-tape representation and associated operations.

---

# 12. Stack Frames

The `.frame` component handles activation records.

A frame may contain:

```text
Frame
├── return address
├── previous frame
├── locals
├── arguments
├── temporary values
└── VM-specific metadata
```

A VM without function calls or stack frames SHOULD NOT generate this component.

---

# 13. Modules and Loading

The `.module` component handles VM modules and loaders.

Depending on the VM, this can include:

* module metadata;
* symbol tables;
* imports;
* exports;
* initialization;
* dynamic loading;
* bytecode loading;
* native code loading.

Module loading must not be confused with Lua module loading. The former belongs to the generated VM unless explicitly stated otherwise.

---

# 14. Compilation

The `.compile` component converts a stream of VM instructions into an executable representation.

For example:

```text
source instructions
        │
        ▼
     compiler
        │
        ▼
   code tape / native code
```

A compiler may perform:

* opcode encoding;
* operand encoding;
* constant resolution;
* label resolution;
* basic optimization;
* instruction selection;
* code layout.

More sophisticated optimizations belong in `.rewrite` or `.optim` where appropriate.

---

# 15. Rewriting and Optimization

The `.rewrite` component is responsible for local instruction transformations.

Examples:

```text
PUSH 1
PUSH 2
ADD
```

may become:

```text
PUSH 3
```

Another example:

```text
DUP
DROP
```

may become:

```text
/* nothing */
```

The `.optim` component is intended for broader VM optimizations such as:

* polymorphic inline caches;
* specialization;
* constant propagation;
* superinstructions;
* instruction fusion;
* dispatch optimization;
* inline caching;
* specialization of object operations.

Do not put target-machine optimization in these components. Machine-specific optimization belongs in `metacode/machine-ir`.

---

# 16. Baseline JIT

The `.jit` component provides simple/baseline JIT functionality.

A baseline JIT should prioritize:

1. correctness;
2. predictable compilation;
3. low compilation latency;
4. straightforward mapping from VM instructions to machine instructions.

It should not duplicate the optimizer in `metacode/machine-ir`.

The conceptual pipeline is:

```text
VM instruction stream
        │
        ▼
   baseline JIT
        │
        ▼
    Machine IR
        │
        ▼
 native machine code
```

---

# 17. Lua Specification

Lua is the VM specification language.

A VM specification should describe the VM rather than manually implement every generated artifact.

Conceptually:

```lua
local vm = vmweave.vm {
    name = "example",

    stack = {
        cell = "intptr_t",
    },

    instructions = {
        {
            name = "PUSH",
            opcode = 0x01,
            operands = { "cell" },
            semantics = "push(operand0)",
        },

        {
            name = "ADD",
            opcode = 0x02,
            semantics = [[
                b = pop();
                a = pop();
                push(a + b);
            ]],
        },
    },
}
```

The exact Lua API MUST follow the implementation in the repository.

This example is illustrative and must not be treated as a normative API unless the corresponding API exists.

---

# 18. Example: Minimal Stack VM

A minimal VM may provide:

```text
PUSH
POP
ADD
SUB
MUL
DIV
HALT
```

Its conceptual instruction set is:

```text
PUSH n     ( -- n )
POP        ( n -- )
ADD        ( a b -- a+b )
SUB        ( a b -- a-b )
MUL        ( a b -- a*b )
DIV        ( a b -- a/b )
HALT       ( -- )
```

A program:

```text
PUSH 10
PUSH 20
ADD
HALT
```

produces:

```text
30
```

A corresponding generated artifact set could be:

```text
calculator.token
calculator.opcode
calculator.insncode
calculator.dispatch
calculator.compile
```

No object, module, JIT, or GC subsystem is required.

---

# 19. Example: Object VM

A more sophisticated VM might contain:

```text
PUSH_CONST
LOAD
STORE
ADD
CALL
RETURN
ALLOC
GETFIELD
SETFIELD
JUMP
BRANCH
HALT
```

Its generated components might include:

```text
objectvm.token
objectvm.opcode
objectvm.insncode
objectvm.dispatch
objectvm.compile
objectvm.memory
objectvm.atomic
objectvm.object
objectvm.frame
objectvm.module
objectvm.tape
```

The object subsystem could represent:

```text
String
Array
Table
Closure
```

with GC-managed allocation.

---

# 20. Example: Optimizing VM

An optimizing VM may additionally generate:

```text
optimized.rewrite
optimized.optim
optimized.jit
```

For example:

```text
LOAD x
LOAD y
ADD
STORE z
```

could potentially be specialized into a fused operation:

```text
ADD_STORE x y z
```

Likewise:

```text
LOAD_GLOBAL foo
CALL 1
```

could use an inline cache:

```text
LOAD_GLOBAL_IC foo
CALL_IC 1
```

The exact transformations must be defined by the optimizer rather than assumed by the generator.

---

# 21. Generated Files

Generated files are artifacts.

Agents MUST distinguish between:

```text
source/specification
```

and:

```text
generated output
```

Generated output should not normally be manually edited.

If generated files are checked into the repository, the generation procedure must remain reproducible.

When changing a generator:

1. modify the generator/specification;
2. regenerate affected artifacts;
3. compare generated output;
4. run the relevant tests;
5. update examples if their generated output changes intentionally.

---

# 22. C/C++ Coding Rules

When modifying VMWeave's C/C++ implementation:

* preserve existing ownership conventions;
* preserve existing error-handling conventions;
* avoid introducing unnecessary global state;
* keep VM specification logic separate from code generation;
* keep target-specific code out of generic VM logic;
* use existing abstractions before introducing new ones;
* do not duplicate generated logic in multiple backends;
* do not silently change generated file formats;
* maintain ABI compatibility where applicable.

C++ code should use RAII where appropriate.

C code used for generated VM semantics should remain compatible with the C dialect expected by the selected backend.

---

# 23. Lua Coding Rules

Lua specification code should remain declarative.

Prefer:

```lua
vm {
    name = "foo",

    instructions = {
        add = instruction {
            opcode = 0x01,
            semantics = "...",
        },
    },
}
```

over embedding generator implementation logic in specifications.

Lua code should not reimplement functionality already provided by the C++ generator.

---

# 24. Third-Party Libraries

Important third-party dependencies include:

```text
third_party/kaguya
third_party/klib/kalloc
third_party/jemalloc
third_party/memtkx
third_party/mimalloc
```

Agents MUST NOT modify third-party code merely to accommodate VMWeave.

If a third-party dependency must be patched:

1. determine whether the behavior can be fixed in VMWeave instead;
2. document why the patch is necessary;
3. keep the patch isolated;
4. avoid unrelated changes.

---

# 25. Machine IR Integration

Assembly generation must use:

```text
metacode/machine-ir
```

rather than directly emitting target assembly from arbitrary VMWeave components.

The preferred architecture is:

```text
VM instruction
      │
      ▼
 STK-00 operation
      │
      ▼
 Machine IR
      │
      ▼
 target backend
      │
      ▼
 assembly
```

This permits one VM specification to target multiple architectures.

When adding a new target:

* do not modify VM semantics;
* do not add architecture conditionals throughout the VM generator;
* implement the necessary Machine IR target functionality instead.

---

# 26. Testing

Every significant change should be tested at the lowest useful level.

Tests should cover, where applicable:

### Specification parsing

```text
Lua specification
        │
        ▼
parsed VM model
```

### Generation

```text
VM model
   │
   ▼
STK-00
```

### STK-00 lowering

```text
STK-00
   │
   ▼
C / Machine IR
```

### Runtime behavior

Generated VMs should be tested with representative programs.

At minimum, a basic stack VM should test:

```text
PUSH
POP
ADD
SUB
MUL
DIV
control flow
```

Object VMs should additionally test:

```text
allocation
object access
GC/lifetime
strings
arrays/tables
calls
frames
```

Optimizing VMs should compare optimized execution against an unoptimized/reference implementation.

---

# 27. Correctness Invariants

The following invariants are important.

## Instruction semantics

For every instruction:

```text
specified semantics == generated semantics
```

unless the transformation is explicitly semantics-preserving.

## Opcode uniqueness

Within one VM:

```text
opcode(i) != opcode(j)
```

for distinct instructions unless opcode aliasing is explicitly supported.

## Mnemonic uniqueness

Instruction mnemonics must be unambiguous unless aliases are explicitly supported.

## Stack correctness

For every instruction sequence accepted by the VM:

```text
stack effects remain valid
```

and underflow must be detected or impossible according to the VM's defined semantics.

## Generated output

Generation must be deterministic for identical inputs unless nondeterminism is explicitly part of the design.

---

# 28. Error Handling

Errors should identify the highest-level useful source location.

For example:

```text
VMWeave: error: instruction 'ADD' has duplicate opcode 0x02
  specification: examples/calculator.lua:37
```

Prefer reporting:

```text
source file
line
VM
instruction
component
```

when available.

Do not emit obscure internal errors when a specification-level diagnostic can be produced.

---

# 29. Adding a New VM Feature

When adding a new VM subsystem:

1. Define its semantic responsibility.
2. Determine whether it belongs in an existing component.
3. Define its specification-level representation.
4. Define its STK-00 representation.
5. Define generated artifacts.
6. Define runtime behavior.
7. Add tests.
8. Add an example.
9. Update this document if the architecture changes.

Do not create a new generated file extension merely because a subsystem has a new implementation detail.

---

# 30. Adding a New Instruction

When adding an instruction:

1. Define its mnemonic.
2. Assign an opcode.
3. Define operands.
4. Define its stack effect.
5. Define its C semantics.
6. Add it to the Lua VM specification.
7. Regenerate affected artifacts.
8. Test its interpreter behavior.
9. Test compilation if applicable.
10. Test JIT behavior if applicable.
11. Update documentation/examples.

Example:

```text
Instruction: DUP

Opcode:
    0x07

Stack effect:
    ( x -- x x )

Semantics:
    x = pop();
    push(x);
    push(x);
```

---

# 31. Adding a New Allocator

A new allocator integration should provide a VMWeave-compatible memory interface.

Conceptually:

```text
VMWeave memory API
        │
        ├── allocate
        ├── reallocate
        ├── free
        └── optional GC hooks
                 │
                 ▼
             allocator
```

The VM specification should not need to know whether allocation is backed by:

```text
kalloc
jemalloc
memtkx
mimalloc
custom C
```

---

# 32. Adding a New Machine Target

Machine targets belong to `metacode/machine-ir`.

The correct approach is:

```text
1. VMWeave
      ↓
2. STK-00
      ↓
3. Machine IR
      ↓
4. New target backend
```

Do not implement:

```text
VMWeave → x86 assembly
VMWeave → AArch64 assembly
VMWeave → RISC-V assembly
```

as independent VMWeave generators unless there is a specific architectural reason.

The Machine IR layer exists to prevent such duplication.

---

# 33. Agent Workflow

When working on VMWeave, an agent should follow this workflow.

## Step 1 — Inspect

Before modifying code, inspect:

```text
AGENTS.md
README
build files
src/
include/
examples/
metacode/machine-ir/
third_party/
tests/
```

and determine where the requested functionality actually belongs.

## Step 2 — Identify the layer

Determine whether the change belongs to:

```text
Lua specification
C/C++ frontend
VM model
STK-00
code generation
runtime
Machine IR
target backend
```

Do not implement a change in a lower layer if it belongs in a higher abstraction.

## Step 3 — Implement the smallest coherent change

Avoid unrelated refactoring.

## Step 4 — Regenerate

If generated artifacts are affected, regenerate them.

## Step 5 — Test

Run:

```text
unit tests
generation tests
generated-VM tests
target tests
```

as applicable.

## Step 6 — Inspect generated output

Generated code is part of the product.

Do not assume that successful generator compilation means successful generation.

## Step 7 — Update documentation

Update examples and this document when the public architecture or workflow changes.

---

# 34. Things Agents Must Not Do

Agents MUST NOT:

* invent undocumented STK-00 syntax;
* silently rename generated file formats;
* modify third-party dependencies unnecessarily;
* hard-code a specific allocator into generic VM code;
* hard-code an architecture into VM semantics;
* duplicate instruction semantics between Lua and C;
* manually edit generated artifacts when the generator should be changed;
* add optional VM components to every generated VM;
* bypass Machine IR for ordinary assembly generation;
* introduce a second Lua binding library;
* change public generated formats without updating consumers;
* remove existing execution models without explicit instruction.

---

# 35. Terminology

Use these terms consistently.

| Term             | Meaning                                   |
| ---------------- | ----------------------------------------- |
| VM               | Virtual machine generated by VMWeave      |
| VM specification | Lua-level description of a VM             |
| Instruction      | Individual VM operation                   |
| Opcode           | Numeric representation of an instruction  |
| Mnemonic         | Human-readable instruction name           |
| STK-00           | Stack-oriented intermediate language      |
| Code tape        | VM instruction stream                     |
| Threading        | VM dispatch strategy                      |
| Frame            | VM activation/stack frame                 |
| Atomic           | Primitive VM value                        |
| Object           | Complex GC-managed VM value               |
| Rewrite          | Local instruction transformation          |
| Optimization     | Higher-level VM optimization              |
| Baseline JIT     | Simple VM-to-native-code compiler         |
| Machine IR       | Machine-level intermediate representation |
| Target           | Concrete architecture/backend             |
| Generator        | Program producing VM artifacts            |

---

# 36. Design Philosophy

VMWeave follows these principles:

### Specification over implementation

The user describes **what the VM is**, while VMWeave determines how to implement it.

### Layering

VM semantics, VM representation, and machine representation are separate concerns.

### Optionality

A VM should generate only the subsystems it actually requires.

### Reuse

Common machine-level functionality belongs in Machine IR rather than being duplicated in VMWeave.

### Reproducibility

The same specification and configuration should produce deterministic output.

### Explicit semantics

Instruction behavior should have one authoritative semantic definition.

### Generated-code transparency

Generated artifacts should remain understandable enough for developers to inspect and debug.

---

# 37. Canonical Architecture

The overall VMWeave architecture should be understood as:

```text
                         ┌───────────────┐
                         │ Lua VM Spec   │
                         └───────┬───────┘
                                 │
                                 ▼
                         ┌───────────────┐
                         │ VM Model      │
                         │ Instructions  │
                         │ Objects       │
                         │ Memory        │
                         │ Modules       │
                         └───────┬───────┘
                                 │
                                 ▼
                         ┌───────────────┐
                         │ STK-00        │
                         │ Stack IR      │
                         └───────┬───────┘
                                 │
                   ┌─────────────┴──────────────┐
                   │                            │
                   ▼                            ▼
            ┌──────────────┐            ┌──────────────┐
            │ C Backend    │            │ Machine IR   │
            └──────┬───────┘            └──────┬───────┘
                   │                           │
                   ▼                           ▼
             Generated C                Assembly / Native
```

VMWeave is therefore not merely a code generator. It is a **VM specification compiler** whose intermediate representation is STK-00 and whose machine-level backend is `metacode/machine-ir`.

---

# 38. Final Rule

When in doubt, preserve the abstraction boundaries:

```text
Lua
  = VM specification

C semantics
  = authoritative instruction behavior

STK-00
  = VM-level generated intermediate representation

Machine IR
  = machine-level representation

Backend
  = concrete target implementation
```

A change that crosses these boundaries should be explicit, documented, and tested.

## Implementation references

The concrete Lua/C++ APIs and supported runtime contracts are documented in
`README.md`. `STK-00.md` defines version 1's grammar, stack-effect words, and
length-framed foreign-C primitive. `stk.cpp` owns the reader/writer; do not add
another serialization or infer instruction behavior from mnemonics.

Built-in native lowering (`native.cpp`) creates an ordinary MachineIR CFG with
explicit foreign-runtime calls. `metacode/machine-ir/native_c.*` owns the validated
binding envelope and C lowering; `native_toolchain.cpp` owns compilation/loading.
Authoritative C handlers are compiled by the selected C toolchain, not interpreted
from mnemonics. Custom `MachineIRAdapter` support remains available. Generated JIT
bindings use the same MachineIR pipeline. Preserve interpreter hook/error/budget
semantics and validate state ABI before entering executable code.
Allocator bindings are isolated in `adapters/`; objects use installed memory
callbacks. Examples are specifications; generated artifacts belong in build trees.
