# MachineIR

MachineIR is an architecture-neutral machine intermediate representation for the
machines described by `../infobank/isa/*.isa`.

It deliberately separates:

* **machine description** — registers, register classes, aliases, encodings and
  target properties;
* **machine operations** — operands, explicit/implicit effects, memory/control
  effects, scheduling and allocation constraints;
* **semantic IR** — a small S-expression representation of instruction meaning;
* **program IR** — values, virtual/physical registers, memory operands,
  instructions, basic blocks, CFGs and functions;
* **backend contracts** — instruction selection, scheduling, register
  allocation, disassembly, reverse engineering and binary translation.

The `.isa` loader is intentionally lossless at the generic-value level. Unknown
metadata is retained in `MachineSpec.raw`, so adding a field to the Infobank
does not require changing MachineIR immediately.
Quoted metadata uses JSON string escapes, including Unicode surrogate pairs.
Malformed escapes, invalid UTF-8, and unescaped control characters are rejected;
printing preserves decoded strings, including embedded control characters.

## Layout

* `source/machineir/package.d` — public API
* `source/machineir/ir.d` — program and machine IR
* `source/machineir/isa.d` — normalized ISA model
* `source/machineir/parser.d` — parser for the Infobank `.isa` syntax
* `source/machineir/semantics.d` — semantic S-expression parser/printer
* `source/machineir/builder.d` — conversion from parsed `.isa` to `MachineSpec`
* `source/machineir/analysis.d` — verification, use/def, dominance and CFG liveness
* `source/machineir/serialize.d` — deterministic textual serialization
* `source/machineir/exchange.d` — owning, validated C++/D region exchange
* `bridge.hpp` / `bridge.cpp` — C++ exchange adapter (`Limestone::machineir_bridge`)
* `native_c.hpp` / `native_c.cpp` — foreign-runtime binding envelope and C backend
  (`Limestone::machineir_native`)
* `native_toolchain.cpp` — POSIX C-toolchain assembly/native compilation and owning
  shared-library loader
* `tests/` — loader and IR tests

## Example

```d
import machineir;

auto isa = loadISA("../infobank/isa/riscv64.isa"); // from the D package directory
assert(isa.arch == "riscv64");
assert(isa.registerClasses["GPR"].registers.length == 32);

auto f = new MachineFunction("add");
auto bb = f.entry();
auto x = f.newVReg("GPR", 64);
auto y = f.newVReg("GPR", 64);
auto z = f.newVReg("GPR", 64);
bb.append(MachineInstruction(
    Opcode.generic("add"),
    [Operand.reg(x), Operand.reg(y)],
    [Operand.reg(z)]
));
```

MachineIR does not pretend that all machines have the same execution model.
`MachineKind`, `ExecutionDomain`, register classes, address spaces, resource
classes and effect sets are explicit and extensible.

## Analyses and C++/D exchange

`verify` checks register/operand/effect classifications, declared identities,
terminators, block operands, CFG reciprocity, memory alignment, and parallel
transfers. `recomputePredecessors`, `analyzeDominance`, `dominates`,
`analyzeLiveness`, and use/def helpers operate on the architecture-neutral program.
Physical and virtual identities remain distinct; semantics are parsed without
evaluation.

The `limestone.machineir.region` schema exports a compiler region and its owning
selection/timing/allocation envelope. Version 1 handles simple regions; version 2
adds CFG/control targets, spill frames, and architectural-result latency.
Version 3 adds scheduling groups, fusion hints, and bundle issue constraints.
Groups may overlap; each contract is checked independently, including shared
members' adjacency, cycle and slot requirements.

```d
import machineir;
import std.file : readText, write;

auto exchange = readExchange(readText("region.json"));
auto function_ = exchange.program.functions[0];
auto live = analyzeLiveness(function_);
auto contract = exchange.instructionContract(0);
auto assigned = exchange.physicalRegister(1); // Nullable!uint
write("returned.json", writeExchange(exchange));
```

C++ exposes `Module::machine_ir_exchange` and `machineir_bridge::deserialize`;
C exposes `limestone_module_exchange`. Both exchange boundaries reject
duplicate/unknown fields, noncanonical keys, invalid SSA/dominance/availability,
malformed resources/slots, order/latency violations, and inconsistent CFG/frame
data. JSON is bounded to 4 MiB and 256 nesting levels. Throughput and resource
quantities are decimal strings. Complete resource/alias/allocation verification
still consumes target metadata through the subsystem verifiers.
Potentially aliasing atomic reads retain their source order even with relaxed
ordering, including when the envelope supplies no explicit memory dependency.

`writeExchange` preserves the envelope while admitting immediate, naming, and
source-provenance edits. Structural, opcode, dataflow, effect, and machine-config
changes need adapters. Timing/resource changes require clearing and recomputing
schedules. Frames are inspectable through `frameSize` and `spillFrameContract`.
Groups remain in the owning envelope, exposed through `groupCount` and
`groupContract`; both exchange boundaries check their order, adjacency, cycles,
and slot restrictions.
Issue vectors preserve intra-block dependency order and declared CFG layout,
including dependencies that issue in different cycles. Reversing dependent
co-issued instructions or interleaving/reversing block issue vectors is rejected.
The actual C++ -> D -> C++ CTest re-encodes and independently executes edited CFG
and spill programs; `dub test` runs the package's unit and Infobank-loader suites.

## Native C backend

The C++ `machineir_native::Unit` combines a normal `RegionExchange` entry with
explicit parameter IDs, C support source, foreign-function definitions, and
instruction-ID-to-callee bindings. It is an extensible backend contract, not a
second MachineIR core model. The entry is unallocated/unscheduled SSA with a CFG;
the C toolchain owns physical allocation, ABI lowering, optimization and encoding.

`verify` validates the existing exchange (including dominance, dependencies and
emission order), parameter/value types, conservative foreign-call effects,
callee signatures, supported opcodes, and explicit block terminators. Supported
ABI types are `ptr`, `i32`, and `u64`; entry returns `i32`. Operations are
`foreign.call`, `const.i32`, `eq.i32`, `lt_zero.i32`, `br.nonzero`, `br`, and
`ret.i32`. Foreign C bodies bind `a0`, `a1`, etc. to their declared parameters.
Calls are nonspeculative barriers with read/write memory and possible traps.
Arbitrary foreign C is checked and compiled by the configured C99 toolchain.

`serialize` / `deserialize` use the strict, versioned
`limestone.machineir.native-c` v1 JSON envelope, limited to 4 MiB:

| Field | Contract |
| --- | --- |
| `schema`, `version` | Fixed schema name and integer 1 |
| `region` | Canonical existing region-exchange JSON stored as a string |
| `parameters` | Ordered array of entry input value IDs |
| `support` | Authoritative translation-unit C source |
| `functions` | Array of `{name,result,arguments,body,file,line}` foreign bindings |
| `callees` | Array of `{instruction,function}` call bindings |

Function names, argument/result ABI types, source provenance and complete body
bytes survive round trips. Duplicate/unknown fields and bindings are errors. Map
output is deterministically ordered. The `region` alone can be read and returned
through the existing D exchange; preserve the foreign envelope alongside it when
editing the MachineIR. The D core needs no VM-specific fields.

`emit_c` emits the validated entry CFG and bindings. `emit_assembly` invokes the
selected GCC/Clang-compatible C compiler with `-S`; `compile` builds and loads a
shared library. `Library` owns image bytes and a reference-counted loader handle;
`symbol` returns addresses valid while a library copy is alive. POSIX loader
ownership keeps the mapped code alive after temporary build files are removed.
Compiler execution uses argument vectors, a configurable timeout and bounded
artifacts; errors retain C diagnostics and foreign source locations. Non-POSIX
executable loading returns `Unsupported`. This adapter obtains target information
from its toolchain; it does not add target ISA data to MachineIR or Infobank.
