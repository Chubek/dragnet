# Chapter 12. Bin2Bin Decoding and Translation

[Previous: MachineIR](11-machineir-and-cpp-d-exchange.md) · [Contents](README.md) · [Next: Objects and linking](13-object-files-and-linking.md)

## 12.1 Binary operations under an explicit state model

Bin2Bin decodes supported instruction forms, lifts semantic expressions, recovers
control flow, translates equivalent forms, encodes compiler regions, and supplies
runtime/cache facilities. Source and target may be identical. Same-ISA rewriting
is a normal case, rather than a special exception to cross-ISA translation.

The public API is [bin2bin/bin2bin.hpp](../bin2bin/bin2bin.hpp), exported by
`Limestone::bin2bin`. Compiler-region encoding uses `Limestone::bin2bin_codegen`.
Objects and runtime observation have separate interfaces discussed in Chapters
13 and 14.

An architecture combines description identity, execution domain, state model,
encoding forms, register tables, semantic expressions, translation statuses,
endianness, and control classifications. These facts come from Metacode or a
clearly defined host adapter. Instruction-name coincidence does not establish
equivalence.

## 12.2 Fixed8 and masked codecs

The built-in codecs support:

- **`fixed8`:** operand-free byte instructions with explicitly declared opcodes.
- **`masked`:** explicit forms of 8–64 bits with fixed bits and nonoverlapping
  operand fields.

Masked forms describe all bits and have unambiguous, prefix-free instruction
boundaries. Endianness is explicit. Fields can be unsigned, signed, register, or
PC-relative. Register fields use the declared class's numeric/name mapping.
PC-relative fields retain scale and start/end-relative base policy.

These codecs are useful for small native slices, virtual register machines, and
fully declared fixed/masked instruction families. General variable-length native
formats, complex operand decoding, and larger virtual format grammars need codec
adapters. An architecture's presence in the Infobank does not automatically
make its complete native binary format a masked codec.

## 12.3 A masked form in detail

The masked source fixture contains:

```text
encoding ADD {
  width = 16;
  base = 1;
  mask = 255;
  fields = {
    dst = { lsb = 8; width = 2; kind = register; class = G; };
    src = { lsb = 10; width = 2; kind = register; class = G; };
    imm = { lsb = 12; width = 4; kind = signed; };
  };
}
```

The low byte is fixed to `1`; the high byte carries two register fields and a
four-bit signed immediate. Every bit belongs either to the fixed mask or a field.
Overlapping fields, uncovered bits, invalid register maps, inconsistent widths,
and ambiguous form prefixes fail validation.

The matching operation supplies an encoding reference, semantics, and a
binary-translation equivalence contract. Its semantics can bind `$dst`, `$src`,
and `$imm` to decoded operand values. Semantic parsing uses SExprTk, rather than
string replacement based on mnemonic spelling.

## 12.4 Ingress and independent encoding

```cpp
auto metadata = limestone::metacode::load_isa_file(
    "tests/fixtures/masked-source.isa");
if (!metadata) return report(metadata.error());
auto architecture = limestone::bin2bin::from_metacode(metadata.value());
if (!architecture) return report(architecture.error());

auto bytes = limestone::bin2bin::encode(
    architecture.value(), "add",
    {{"dst", "r0"}, {"src", "r1"}, {"imm", "7"}}, 4096);
```

This API fragment assumes the component headers and host reporter. Operand strings
are semantic register names or decimal integers. A PC-relative operand denotes
its explicit target address under the form's relative encoding policy.

`encode` selects a legal mnemonic form deterministically. `encode_form` encodes
a particular stable form ID after a layout pass has chosen it. Range, signedness,
scale, register membership, operand names, and required operands remain checked.

## 12.5 Decode, disassemble, lift, and decompile

`decode(architecture,bytes,address)` returns owning instruction records with
address, bytes, mnemonic, status, form identity, decoded operands, control class,
and optional direct target. Unknown or truncated encodings return a diagnostic.

`disassemble` formats a decoded stream. `lift` attaches instantiated semantic
expressions. `decompile` produces semantic assembly, the deterministic high-level
assembly view of the supported instruction meanings.

```sh
printf '\001\001' > /tmp/opencode/inc.bin
build/limestone-cli --disassemble tests/fixtures/byte-source.isa \
  /tmp/opencode/inc.bin
build/limestone-cli --decompile tests/fixtures/byte-source.isa \
  /tmp/opencode/inc.bin
```

The semantic listing preserves architectural state such as the fixture's
`counter`. It is not reconstructed source code with inferred types, functions,
or high-level control constructs. Such decompiler plugins require separate
interfaces and must preserve the distinction between facts and hypotheses.

## 12.6 Translation statuses

Bin2Bin distinguishes supported, unsupported, fallback, architecture-specific,
privileged, environment-dependent, and ambiguous instructions. A description can
permit decoding while explicitly withholding translation support.

Translation and CFG analysis reject unsupported semantic dispositions rather
than treating them as ordinary instructions. A status of fallback does not invoke
an interpreter automatically; the embedding runtime must provide any fallback
execution boundary. Privileged or environment-dependent semantics likewise need
their declared host context before a legal adapter can support them.

This distinction is valuable during analysis: known bytes and mnemonic remain
inspectable even when their complete executable behavior is not admitted by the
current translator.

## 12.7 Control-flow recovery

`analyze` returns decoded instructions and blocks with instruction indexes,
successors, and indirect-exit flags. It uses explicit control classifications and
targets. Direct branches must address valid source instruction boundaries under
the region contract.

Returns, traps, indirect branches, calls, and conditional branches remain
different control kinds. Operand-free fixed8 instructions can return or trap;
a direct target requiring an operand needs an appropriate operand codec.

Recovered CFG blocks are an analysis model. They do not supply missing call ABI,
exception semantics, code/data separation, or an indirect-target policy for an
arbitrary executable. Input loading and region formation remain explicit host
responsibilities beyond the supported raw instruction stream.

## 12.8 Semantic translation and target matching

The translation sequence is:

```text
decode source -> validate supported semantics -> lift operands
              -> optional checked semantic transform
              -> match equivalent target forms
              -> lay out instructions / remap direct targets
              -> relax branches -> encode target bytes
```

Source and target execution/state models must agree under the supplied adapter.
Operand and architectural-state meaning cannot be bridged by matching equal opcode
numbers. Broader cross-ISA state or ABI transformations require explicit IL
adapters.

The bytecode fixtures make this visible: source `inc` is byte `0x01`, target
`increment` is byte `0x09`, and both declare the same counter-state update.

```sh
build/limestone-cli --translate tests/fixtures/byte-source.isa \
  tests/fixtures/byte-target.isa /tmp/opencode/inc.bin \
  -o /tmp/opencode/increment.bin
```

Equivalent legal target forms are chosen by width and stable identity, with
layout accounting for direct branches. The result owns its byte vector.

## 12.9 Addressed translation and branch relaxation

`TranslationOptions` carries source and target addresses, rule/configuration/plugin
identities, runtime configuration, and optional semantic transform. Addresses are
part of correctness because relative fields depend on instruction placement.

Source direct targets are mapped to relocated target boundaries. Short forms
can become insufficient when translation expands intervening instructions.
Relaxation monotonically promotes legal short forms to longer forms until layout
stabilizes or no form can encode the displacement.

Scale requires an exactly representable displacement; start/end-relative policy
uses the declared base. Arithmetic overflow, a mid-instruction target, unsupported
external control, or an out-of-range final displacement is an explicit failure.
Do not copy source displacement bits into a differently sized target stream.

The raw translation CLI uses default addresses. Embedding APIs expose explicit
addresses for addressed regions. Object relocation is a separate contract,
explained in Chapter 13.

## 12.10 Checked semantic transforms

`SemanticTransform` contains an identity, owning callable, and cacheable flag.
The callable consumes a `LiftedInstruction` with source address, semantics,
status, control class, and optional target. It returns proved-equivalent semantic
text before target matching.

Tunah's adapter supplies a snapshot, ingress/extraction legality checks, bounded
saturation, and a deterministic identity. The host still establishes widths,
types, state effects, operand legality, and rule soundness.

Per-instruction transforms preserve instruction/control boundaries and direct
targets. They cannot silently remove a branch or combine arbitrary CFG regions.
A CFG-changing optimizer needs a region representation and reconstruction adapter.
Nondeterministic or cancellation-sensitive transforms must bypass byte and
resident translation reuse.

## 12.11 Encoding a compiler region

`encode_region` consumes final Scheduler IR, emission order, target register
names, allocation, and field bindings. A binding identifies a selected definition,
use, immediate, or block target by operand index. Fixed definitions/uses constrain
physical storage without corresponding encoded fields.

The encoder verifies sequential hazards and layout, complete operand binding,
allocation completeness, fixed requirements, immediate ranges, and legal forms.
Register fields require spill-free final physical assignments. Ties, early
definitions, effects, and control remain explicit contracts.

The Metacode-backed pipeline backend chooses the materialized region and
allocation after spills. A custom backend receives the module and must consume
the same final representation. Backend output owns bytes and named relocations,
which the object layer can package later.

## 12.12 Practical binary validation

Test codecs with exact positive bytes, malformed/truncated streams, illegal
register numbers, signed boundaries, scales, and endian variants. Test translation
with same-ISA and equivalent different-encoding fixtures, relocated branches,
short-to-long relaxation, unsupported statuses, and changed cache identities.

When possible, independently execute both source and target state updates.
Round-trip decoding alone checks representation consistency; independent execution
checks semantic preservation. The following chapter adds object containers,
symbols, and addressed relocation to the encoded-byte boundary.

[Previous: MachineIR](11-machineir-and-cpp-d-exchange.md) · [Contents](README.md) · [Next: Objects and linking](13-object-files-and-linking.md)
