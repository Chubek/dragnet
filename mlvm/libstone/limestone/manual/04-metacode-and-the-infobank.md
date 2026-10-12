# Chapter 4. Metacode and the Infobank

[Previous: First workflows](03-first-programs-and-workflows.md) · [Contents](README.md) · [Next: Graphs and UMD](05-program-graphs-and-umd.md)

## 4.1 The horizontal machine-information layer

Metacode supplies target information to the rest of Limestone. The Infobank under
`metacode/infobank/` is the authoritative repository of ISA descriptions. Its
declarations describe architecture identity, registers, encodings, operations,
semantic expressions, and backend-oriented tooling metadata. Consumers normalize
the portions they understand while retaining provenance and unrelated metadata.

This arrangement lets several algorithms use the same target facts. A register
class imported by Unisel can become an allocation class in RegTL. An encoding
description can become a Bin2Bin form. An explicit source selection tree can
become either a graph pattern or a BURS rule. These are separate adapters over
shared input, rather than independent hand-maintained instruction tables.

The distinction between loading and interpreting is important. Metacode can retain
a field whose value is `unknown`. The scheduler cannot then treat that value as a
known latency. Preservation makes the metadata inspectable; it does not supply
missing machine facts.

## 4.2 Bundle structure and schemas

The Infobank manifest identifies the bundle as `isa-description-bundle`, version
`2`, with S-expression semantics. Its architecture records identify source files,
architecture/family/model/version, instruction counts, register classes, semantic
counts, compiler consumers, and tooling contracts. The current manifest contains
23 architectures. Legacy marker files are recorded separately from usable targets.

The principal references are:

- [Manifest](../metacode/infobank/manifest.json)
- [Bundle schema](../metacode/infobank/schema.json)
- [Tooling augmentation guide](../metacode/infobank/ISA_TOOLING_AUGMENTATION.md)
- [Binary translation metadata](../metacode/infobank/BIN2BIN_TRANSLATION.md)
- [Bin2Bin schema](../metacode/infobank/bin2bin.schema.json)
- [Object-file schema](../metacode/infobank/object-file.schema.json)

The manifest's grammar reference describes the Infobank data contract. The C++
runtime parser is generated from `parsers/isa.g`; the D MachineIR package also has
its own ISA parser. A schema, a grammar, and a semantic consumer perform different
checks. A valid generic object can still lack the contracts needed by a particular
backend operation.

## 4.3 Anatomy of an ISA description

The following small example is the source bytecode fixture's basic structure:

```text
arch byte_source {
  word_size = 64;
  addr_size = 64;
  endian = little;
}
profile {
  family = "fixture";
  model = "semantic-bytecode";
  version = "1";
  execution_model = "one-counter";
}
tooling {
  bin2bin = {
    schema_version = 1;
    instruction_encoding = "fixed8";
    execution_domain = "bytecode";
    word_size = 64;
    address_size = 64;
    endianness = "little";
  };
}
encoding INC { width = 8; base = 0x01; }
op inc {
  encoding = INC;
  operands = ;
  semantics = "(set counter (add counter 1))";
  tooling = {
    binary_translation = {
      schema_version = 1;
      equivalence_basis = "semantics";
    };
  };
}
```

The operation has an explicit encoding reference, a semantic expression, and a
translation contract. The architecture's execution domain distinguishes bytecode
from native instructions. Its execution model declares which state vocabulary the
semantics uses. No CPU calling convention follows from the word size or endianness.

More elaborate descriptions add register classes, aliases, field encodings,
implicit state, scheduling, allocation, and selection metadata. See
[`backend-machine.isa`](../tests/fixtures/backend-machine.isa) for a connected
selection/allocation/spill/encoding fixture.

## 4.4 The C++ metadata model

The public API is [metacode/metacode.hpp](../metacode/metacode.hpp), exported by
`Limestone::metacode`:

```cpp
#include <metacode/metacode.hpp>
#include <iostream>

int main() {
  auto loaded = limestone::metacode::load_isa_file(
      "tests/fixtures/backend-machine.isa");
  if (!loaded) {
    std::cerr << loaded.error().message << '\n';
    return 1;
  }
  const auto& architecture = loaded.value();
  std::cout << architecture.name << '\n';
  std::cout << limestone::metacode::dump(architecture) << '\n';
}
```

`parse_isa(text, filename)` is the in-memory equivalent. A meaningful filename
improves diagnostics and provenance even when the input is not a disk file.

`Architecture` owns identity strings, generic fields, register records, operation
records, encoding objects, aliases, and an architecture source location.
`Operation` owns its name, semantics, generic fields, and source location.
`Value` is a source-located variant of string, signed integer, unsigned integer,
Boolean, object, or array. This generic representation allows nested contracts
and unknown future fields to survive ingestion.

Inspect a value's variant before consuming it. A string containing `unknown` is
not a numeric zero, and a numeric register identity is not a register name.
Semantic adapters should report incompatible types with the relevant architecture,
operation, and field.

## 4.5 Registers and logical classes

Register declarations name a class, width, and encoding/physical number:

```text
regclass G {
  r0(64) = 0,
  r1(64) = 1,
  r2(64) = 2,
  r3(64) = 3,
}
```

Class names are target-defined. `GPR`, `FPR`, or `G` are useful conventions, not
built-in universal storage kinds. Register names provide semantic/diagnostic
identities; numeric fields provide explicit target identities or encoding values.
The normalization adapter carries the required mapping into its consuming model.

An empty logical register class is still a declared class. Virtual ISAs may use
such a declaration to classify abstract values without enumerating physical
storage. UMD normalization preserves it. Physical allocation needs actual legal
members or an explicit adapter; an empty class cannot be silently replaced by a
fictional machine register file.

Register overlap is explicit. RegTL consumes pairwise aliases and does not assume
that overlap is transitive. If `A` overlaps `B` and `B` overlaps `C`, that alone
does not establish overlap between `A` and `C`.

## 4.6 Semantics, selection, and encoding

An instruction's semantics answers what the instruction does. A source selection
pattern answers which compiler computation it can implement. An encoding answers
how a chosen instruction and operands are represented in bits. These descriptions
can reference the same operation while supplying different information.

For example:

```text
tooling = {
  instruction_selection = {
    selection_tree = "add(?lhs:i64, ?rhs:i64):i64";
    cost = 1;
  };
};
```

This explicitly declares a typed source computation. `unisel::from_metacode`
imports only such `selection_tree` contracts as generic patterns. Generic opcode
classes and short inventory `pattern` hints remain metadata; they are not a proof
that architectural flags, widths, traps, or extension availability agree with a
source-language addition.

Likewise, operand legality and encoding are separate. A signed-eight-bit
predicate proves a value lies in range. The encoding still needs a field width,
bit position, signedness, scale, and binding to the selected immediate operand.

## 4.7 Shared declarative operand constraints

Metacode's [operand-constraint API](../metacode/operand_constraints.hpp) is shared
by Unisel and Limeburg. Constraints form an ordered conjunction over bound source
operands:

```text
where {
  constraints = [
    { kind = signed_bits; operand = imm; value = 8; },
    { kind = multiple_of; operand = imm; value = 4; }
  ];
}
```

This admits an immediate only when it is proven to fit the signed width and is a
multiple of four. It does not choose a physical register or encode the scaled
field. Both selectors apply the same legality before cost comparison.

Identity predicates `same_value` and `different_value` compare source IDs.
Immediate comparison predicates compare proven signed-64-bit constants.
`signed_bits` and `unsigned_bits` accept widths 1–64; `multiple_of` requires a
positive divisor; `power_of_two` requires a positive value and has no numeric
parameter. Unknown constants fail to prove an immediate condition. Invalid
binding names, fields, and predicate argument shapes fail validation.

## 4.8 Timing and microarchitecture information

The inventory's tooling augmentation intentionally retains values such as
`unknown`, `target_dependent`, and `abi_dependent`. Exact latency, issue width,
resource capacity, port reservations, and bypass behavior need authoritative
microarchitecture data or a clearly named host model.

A source model may provide an instruction-level latency and more precise
definition-operand overrides. The pipeline resolves those operand indexes to SSA
IDs after selection. Architectural-result latency remains keyed by architectural
identity. An imported throughput value is retained separately from these latencies.

Unknown fields inside an explicit interpreted scheduling/resource/memory contract
are rejected when the consumer cannot establish their semantics. Unrelated
metadata is retained. This distinction prevents a misspelled hard constraint from
becoming a harmless-looking ignored annotation.

## 4.9 Normalization, provenance, and reproducibility

The CLI provides two useful views:

```sh
build/limestone-cli --isa metacode/infobank/isa/riscv64.isa
build/limestone-cli --emit-umd metacode/infobank/isa/riscv64.isa \
  -o /tmp/opencode/riscv64.umd
```

The first produces normalized metadata output. The second preserves the machine
description in UMD form. A normalized UMD can be useful even when the target has
few explicit generic source patterns.

Source locations identify the original description file and declaration. Pattern
origins survive canonical printing and selection emission. Generated
specifications also retain architecture identity, original semantics, operand
contracts, encoding references, and per-instruction disposition.

Deterministic printers sort externally visible map-like information rather than
using pointer addresses or unordered iteration. Regenerate derived data from the
authoritative inputs; direct edits to generated rules will be detected by the
freshness check.

## 4.10 Extending a description

When adding an operation, first establish its semantics and operand/effect model.
Then supply the contracts needed by the consumers you intend to support:
selection tree and predicates, timing/resources, storage constraints, binary
encoding/translation, and object/ABI metadata as applicable. Update manifest
counts and semantic counts when the inventory changes.

Validate parsing, normalization, positive selection, illegal operands, encoding
boundaries, and the executable state model independently. A successful generic
metadata load is a useful first milestone; the remaining tests establish the
specific target capability.

[Previous: First workflows](03-first-programs-and-workflows.md) · [Contents](README.md) · [Next: Graphs and UMD](05-program-graphs-and-umd.md)
