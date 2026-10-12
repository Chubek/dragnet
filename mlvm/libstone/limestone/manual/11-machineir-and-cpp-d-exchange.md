# Chapter 11. MachineIR and C++/D Exchange

[Previous: Equality saturation](10-tunah-equality-saturation.md) · [Contents](README.md) · [Next: Binary translation](12-bin2bin-decoding-and-translation.md)

## 11.1 MachineIR's place in the framework

MachineIR is Limestone's architecture-neutral machine intermediate representation,
implemented as a D package under `metacode/machine-ir`. It describes machine
programs while keeping target descriptions, instruction semantics, scheduling,
allocation, and backend contracts separate.

The C++ pipeline uses Scheduler IR and RegTL as its native stage models, then
produces a diagnostic machine listing and a versioned region exchange. The
exchange bridge is an adapter into the D representation, not a second universal
MachineIR implementation hidden in C++.

This separation makes machine-facing programs available to D consumers without
leaking Satie, Equinox-NG, parser internals, allocator implementation state, or
target-specific C++ types into the IR.

The package overview is [MachineIR's README](../metacode/machine-ir/README.md);
the C++ bridge API is [bridge.hpp](../metacode/machine-ir/bridge.hpp).

## 11.2 Five related models

MachineIR distinguishes:

1. **Machine description:** register classes, aliases, encodings, and target
   properties normalized from ISA declarations.
2. **Machine operations:** operand/effect descriptions and explicit machine-facing
   constraints.
3. **Semantic expressions:** parsed S-expressions describing meaning without
   evaluating it.
4. **Program IR:** modules, functions, blocks, values, instructions, operands,
   and CFG edges.
5. **Backend contracts:** selection, scheduling, allocation, disassembly, and
   translation information layered on the representation.

Machine kind and execution domain remain explicit. A virtual ISA or stack bytecode
does not become a native register architecture merely because its program is
represented by MachineIR. Target-specific properties belong in `MachineSpec` or
extensible contracts, not in an architecture-specific field on every instruction.

## 11.3 Package source map

| File | Responsibility |
| --- | --- |
| `source/machineir/package.d` | Public package imports |
| `ir.d` | Program and machine IR |
| `isa.d` | Normalized machine description |
| `parser.d` | Infobank declaration parser |
| `builder.d` | Parsed declarations to `MachineSpec` |
| `semantics.d` | Semantic expression parsing and printing |
| `analysis.d` | Verification, use/def, dominance, and liveness |
| `serialize.d` | Deterministic textual serialization |
| `exchange.d` | Owning versioned region exchange |

Unknown ISA fields remain in `MachineSpec.raw`. That allows consumers to retain
future metadata before they acquire a semantic adapter for it. Semantic expressions
are parsed separately and are not evaluated by the ISA loader.

## 11.4 Constructing and analyzing D programs

The public package import is:

```d
import machineir;
```

A machine-independent construction fragment is:

```d
auto function_ = new MachineFunction("add");
auto block_ = function_.entry();
auto x = function_.newVReg("GPR", 64);
auto y = function_.newVReg("GPR", 64);
auto z = function_.newVReg("GPR", 64);
block_.append(MachineInstruction(
    Opcode.generic("add"),
    [Operand.reg(x), Operand.reg(y)],
    [Operand.reg(z)]
));
```

This fragment demonstrates values and operands. A complete function must declare
the applicable input/boundary/effect and control contracts before verification or
execution. The source type/class and width remain independent from any eventual
physical assignment.

For an ISA description loaded from the D package directory:

```d
auto machine = loadISA("../infobank/isa/riscv64.isa");
assert(machine.arch == "riscv64");
```

Use the declared machine classes and effect vocabulary when constructing
machine-specific operations. Do not infer an operand layout solely from a generic
opcode name.

## 11.5 Program verification and CFG analyses

The verifier checks declared identities, register and operand classifications,
effects, memory alignment, block references, terminators, CFG reciprocity, and
parallel transfers. Virtual and physical registers remain separate domains.

`recomputePredecessors` derives predecessor sets from successors.
`analyzeDominance` and `dominates` establish cross-block availability.
`analyzeLiveness` computes live values over the CFG. Use/def helpers support
inspection without interpreting semantic S-expressions.

These analyses answer program questions. Full resource capacity, alias legality,
and allocation verification still need the relevant target model and subsystem
verifier. A structurally sound instruction vector can have an illegal timing
assignment or physical register map.

## 11.6 Diagnostic text and owning exchange

`Module::machine_ir` is the pipeline's readable listing. It is useful for logs,
inspection, and finding the first wrong opcode or operand. It is not a substitute
for the full envelope required to preserve schedules, frames, and allocations.

`Module::machine_ir_exchange` contains the owning JSON region handoff. C exposes
it through `limestone_module_exchange`; the CLI emits it with `--machineir-json`.
Python's `Module.exchange` returns an independent string copy.

```sh
printf '%s\n' '(add 20 22)' | build/limestone-cli \
  --machineir-json -o /tmp/opencode/region.json
```

The schema identity is `limestone.machineir.region`. The envelope carries program
values, selected instructions, dependencies, final order, optional schedule and
allocation, CFG/control targets, spill frames, and group contracts as available.

## 11.7 Exchange versions

| Version | Additional contract |
| --- | --- |
| 1 | Simple region and selection/timing/allocation envelope |
| 2 | CFG/control targets, spill frames, and architectural-result latency |
| 3 | Scheduling groups, fusion hints, and bundle issue restrictions |

Readers validate the version and its field contract. Versioning is meaningful
because a consumer must not silently drop a group or spill frame it does not
understand. Prefer the provided serializers over manually inventing a JSON
shape from a diagnostic listing.

Throughput and resource quantities use decimal strings in the exchange. This
preserves their explicit numeric representation across language boundaries.
Identity keys and numeric values must satisfy the canonical contract.

## 11.8 Reading and writing in D

```d
import machineir;
import std.file : readText, write;

void main() {
    auto exchange = readExchange(readText("/tmp/opencode/region.json"));
    auto function_ = exchange.program.functions[0];
    auto live = analyzeLiveness(function_);
    auto contract = exchange.instructionContract(0);
    auto assigned = exchange.physicalRegister(1);
    write("/tmp/opencode/returned-region.json", writeExchange(exchange));
}
```

`instructionContract` uses an instruction inspection position; physical register
lookup uses a value identity and returns a nullable result. A value need not have
a physical assignment in an unallocated exchange. Preserve that distinction
instead of using a numeric sentinel as a guessed register.

The exchange object owns both the program and the machine envelope. Contract
inspection includes `frameSize`, `spillFrameContract`, `groupCount`, and
`groupContract` when those facilities are present.

## 11.9 Reading and writing in C++

`machineir_bridge::RegionExchange` contains module/target/function identity,
region, order, outputs, value descriptors, schedule, optional allocation,
spill slots, and frame size. Group contracts remain in its region.

```cpp
auto restored = limestone::machineir_bridge::deserialize(json, "region.json");
if (!restored) return report(restored.error());
auto normalized = limestone::machineir_bridge::serialize(restored.value());
```

This fragment assumes a host JSON string and reporter. Link
`Limestone::machineir_bridge`. A consumer that changes timing/resources must
clear and recompute schedules with a real machine model before re-emission.

The bridge's verification boundary checks identities and availability, not merely
whether JSON can be parsed. Errors retain a source name suitable for diagnostics.

## 11.10 Accepted and rejected exchange edits

The owning envelope permits immediate, naming, and source-provenance edits while
preserving its established representation. This is useful for controlled
specialization or source mapping across a D analysis pass.

Structural, opcode, dataflow, effect, and machine-configuration changes require
adapters that re-establish downstream contracts. A changed opcode can alter
latency, resource use, physical requirements, and encoding. A changed operand can
alter dominance, liveness, interference, and branch targets. Retaining the old
schedule and allocation would falsely describe the new program.

Even an admitted immediate edit can make a later encoding field out of range.
Exchange validity and target encoding legality are complementary checks.

Group order, adjacency, equal cycles, slots, CFG layout, dependencies, and spill
frame consistency are validated. Reversing dependent co-issued instructions or
interleaving blocks is rejected even when their cycle numbers remain unchanged.

## 11.11 Bounds and strictness

Both exchange boundaries bound JSON to 4 MiB and nesting to 256 levels. They reject
duplicate or unknown fields, noncanonical identity keys, invalid SSA/dominance,
malformed resources and slots, inconsistent allocations/frames, and order or
latency violations.

This strictness differs intentionally from lossless generic ISA metadata loading.
Unknown ISA fields can remain uninterpreted metadata. Unknown fields in a
versioned executable handoff could imply obligations that the reader would lose.
Use a new version or a defined adapter when extending that handoff.

## 11.12 Building and validating the language boundary

Run package tests from its directory:

```sh
cd metacode/machine-ir
dub test
```

When D tooling is installed, CMake registers package and exchange integration
tests. The actual C++ → D → C++ test re-encodes and independently executes edited
CFG and spill programs. This validates ownership and machine-facing behavior
across languages, beyond a string round trip.

Use identity round trips first, then admitted edits, then explicit rejected edits.
Retain target metadata for complete scheduling/allocation/encoding verification.
The next chapter follows the final program into Bin2Bin's explicit binary codecs.

[Previous: Equality saturation](10-tunah-equality-saturation.md) · [Contents](README.md) · [Next: Binary translation](12-bin2bin-decoding-and-translation.md)
