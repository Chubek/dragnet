# Chapter 13. Object Files and Linking

[Previous: Binary translation](12-bin2bin-decoding-and-translation.md) · [Contents](README.md) · [Next: Runtime and caches](14-runtime-translation-and-caching.md)

## 13.1 Containers after encoding

The object layer packages code and data into owning ELF64 relocatable objects,
loads supported external objects, preserves sections/symbols/relocations, and
links them into an addressed byte image. It is independent of instruction
selection, scheduling, allocation, binary decoding, and executable installation.

An object supplies named boundaries and unresolved references around encoded
content. Linking resolves those references under an explicit target relocation
contract. Installation supplies memory mapping, permissions, callable ABI, and
lifetime. Keeping these stages separate lets the same compiler result be passed
to the system linker or to an embedding runtime.

The C++ API is [bin2bin/object.hpp](../bin2bin/object.hpp), exported by
`Limestone::bin2bin_object`. The C API is
[limestone/object.h](../limestone/object.h), exported by `Limestone::object`.
The detailed component guide is [bin2bin/OBJECTS.md](../bin2bin/OBJECTS.md).

## 13.2 Explicit object target metadata

`tooling.object_file` supplies independent version-1 object metadata:

```text
tooling {
  object_file = {
    format = elf64;
    version = 1;
    machine = 62;
    endianness = little;
    flags = 0;
    osabi = 0;
    abi_version = 0;
    text_alignment = 16;
    relocations = [
      { type = 1; name = abs64; kind = absolute;
        storage_bytes = 8; bit_offset = 0; bits = 64;
        scale = 1; signed = false; },
      { type = 2; name = pc32; kind = pc_relative;
        storage_bytes = 4; bit_offset = 0; bits = 32;
        scale = 1; signed = true; }
    ];
  };
}
```

This explicitly declares x86-64 ELF identity and two relocation encodings. Numeric
machine and relocation identities are target data, not inferred from an
architecture name. The synthetic bytecode fixture declares a distinct identity
and can be packaged without becoming native host code.

Top-level identity/alignment/relocation fields are required. Unknown fields,
duplicate types/names, invalid widths, non-power-of-two alignment, and unsupported
relocation expressions fail target validation.

## 13.3 The owning model and stable references

`ObjectFile` owns format identity, sections, symbols, relocations, and source name.
Section and symbol references are zero-based model indexes. They are not the
eventual ELF section/table indexes, which the emitter remaps deterministically.

`object_undefined` and `object_absolute` are distinct symbol-section identities.
A symbol in an ordinary section uses an offset into that section. An absolute
symbol supplies an address independent of section placement. An undefined symbol
requests resolution during linking.

`ObjectSymbol` also retains size, local/global/weak binding, scalar ELF type, and
visibility. `ObjectRelocation` identifies content section, symbol, numeric type,
storage offset, and signed explicit addend. The model never overlays host ELF
structs on untrusted input bytes.

## 13.4 Sections and allocated opaque content

The supported content model includes:

| Type | Representation |
| --- | --- |
| PROGBITS | Owning raw bytes |
| NOTE | Owning opaque note bytes |
| NOBITS | Explicit zero-fill size with no stored file payload |
| OS/processor-specific content | Byte-preserving opaque sections under the model contract |

Flags, alignment, and entry size are retained. Nonallocated sections stay in the
object but do not enter the linked addressed image. Allocated sections contribute
bytes or zero fill at aligned addresses.

Allocated OS/processor-specific sections require an explicit
`opaque_section_types` target declaration. Such a declaration authorizes byte
preservation, not interpretation of embedded symbol indexes or special allocation
semantics. Indexed link/info contracts and symbol-indexed contents need adapters.
Native unwind content, for example, remains opaque unless the host supplies its
own interpretation and execution requirements.

## 13.5 Checked ELF64 ingestion

`load_elf` accepts ELF64 ET_REL containers using direct section indexes, one symbol
table, and explicit-addend RELA. It validates header/table ranges, shapes, null
entries, string termination, local/nonlocal partitions, symbol/section bounds,
alignment, and overlapping file storage.

Unknown relocation type numbers remain inspectable and can be re-emitted. Linking
requires an explicit encoding contract for every fixup applied to the image.
This preserves container information without guessing a relocation formula.

```cpp
auto object = limestone::bin2bin::load_elf(elf_bytes, "unit.o");
if (!object) return report(object.error());
auto emitted = limestone::bin2bin::emit_elf(object.value());
```

The host supplies an input byte span and reporter. The loaded object owns its
contents and can outlive the input span. Emission lays out content sections,
local-first symbols, strings, and per-section RELA tables deterministically.
The source object is not mutated.

## 13.6 Relocation formulas and checked arithmetic

Each relocation contract declares storage width of 1–8 bytes, a contiguous bit
field, positive scale, signedness, and absolute or PC-relative kind. PC-relative
forms may declare a signed `pc_bias`.

Let `S` be resolved symbol address, `A` the RELA addend, and `P` the address of
relocation storage. The encoded value is:

```text
absolute:    (S + A) / scale
PC-relative: (S + A - (P + pc_bias)) / scale
```

Division must be exact. The result must fit the declared signed/unsigned field.
Other bits in the storage unit are retained. Disjoint relocation fields can share
storage; overlapping relocation bits fail.

Checked arithmetic handles full-width addresses and negative displacements,
including signed-64-bit minimum values. Host integer overflow must not turn an
out-of-range address into a seemingly valid small relocation.

An instruction's PC-relative codec and an ELF relocation are separate contracts.
The object relocation uses storage address `P` and its declared bias; do not assume
it uses the instruction codec's end-relative base.

## 13.7 Deterministic symbol resolution

Local symbols are scoped to their input object. Global and weak symbols participate
in named resolution:

- a strong definition replaces a weak one;
- duplicate strong definitions fail;
- the first weak definition wins deterministically;
- missing strong references require explicit external addresses;
- unresolved weak references resolve to zero;
- hidden/internal undefined references cannot use external fallback.

Input order therefore matters for weak resolution and section layout. The linked
image's resolved symbol map is lexically ordered for stable inspection.

Machine number, endianness, ABI identity, and flags must match the object target.
Matching ELF identity still does not establish that every function uses the host's
expected call signature. ABI lowering and invocation remain separate boundaries.

## 13.8 Linking to an addressed image

```cpp
auto target = limestone::bin2bin::object_target(metadata);
if (!target) return report(target.error());

limestone::bin2bin::LinkOptions options;
options.base_address = installation_address;
options.externals["host_symbol"] = host_symbol_address;
auto image = limestone::bin2bin::link_objects(objects, target.value(), options);
```

This fragment assumes owning metadata/objects and actual installation/external
addresses. Allocated sections are placed in input/declaration order at aligned
addresses. Padding and NOBITS storage are zeroed. `LinkedImage` owns bytes, section
placements, resolved symbols, and base address.

All allocated fixups are applied transactionally. The installation base is part
of the relocation calculation. Installing the same bytes at an unrelated base
does not preserve absolute or address-dependent fixups. Obtain the intended
mapping/address contract before final addressed linking.

The image has no executable mapping. The host owns permissions, instruction-cache
maintenance where applicable, callable ABI, guest state, and memory lifetime.

## 13.9 Compiler packaging and raw-code wrapping

`limestone::make_object(module,target,symbol)` packages an encoded module and
converts backend named relocations to target numeric relocation types. Metacode
object targets retain architecture identity, which must agree with the module.
The backend owns external-reference generation and physical ABI lowering.

`code_object` packages raw code bytes and optional named relocations. Wrapping is
a container operation: it does not verify arbitrary bytes as a complete function.

```sh
printf '%s\n' '(add 20 22)' | build/limestone-cli \
  --target-isa tests/fixtures/native-constant.isa --allocate --encode \
  --object native_entry -o /tmp/opencode/native-entry.o
build/limestone-cli --inspect-object /tmp/opencode/native-entry.o
```

For caller-provided bytes:

```sh
build/limestone-cli --wrap-object target.isa entry code.bin -o code.o
build/limestone-cli --link-objects target.isa \
  --input-object code.o --input-object helper.o \
  --base-address 4096 --external-symbol host_symbol 8192 -o image.bin
```

The second example assumes contracts and input objects named by the host. Numeric
CLI addresses are nonnegative decimal values.

## 13.10 C and Python ownership

C builders validate additions transactionally. Section/symbol/relocation outputs
remain unchanged on failure. Inspection strings and spans are borrowed until
object mutation or destruction. Serialized data handles and linked image handles
are immutable owners independent of the input objects.

`limestone_module_object` additionally uses `Limestone::core` to package a compiler
module. Its result outlives the source module and object target.

Python `ObjectTarget`, `ObjectFile`, and `LinkedImage` provide context managers.
`emit`, section/symbol properties, and image bytes return Python-owned copies.
Linking retains input handles for the synchronous call, then returns an independent
image. Chapter 19 shows builder and linking examples.

## 13.11 Budgets and adapter boundaries

Default object bounds are 64 MiB for input/output and expanded section/name bytes,
16,384 content sections, and 1,048,576 symbols and relocations each. Linking also
bounds aggregate counts, object count, external names, and image size. C++
`ObjectLimits` and `LinkOptions` expose applicable configuration; C/Python use
default object limits.

ELF32, executables/shared objects, archives, REL implicit addends, extended indexes,
compressed sections, COMDAT/groups, TLS/GOT/PLT construction, dynamic binding,
and noncontiguous or multi-instruction fixups need additional adapters. Unsupported
allocated flags and symbol kinds are explicit errors.

## 13.12 Verifying container and executable behavior

Test deterministic load/emit round trips, malformed tables, note/NOBITS retention,
opaque allocated sections, strong/weak/local resolution, external addresses,
range/scale boundaries, and overlapping fixups. Then validate actual code through
independent execution when the target ABI permits it.

The repository exercises both its addressed linker and the system compiler/linker
with native fixtures returning `42`. These tests establish a bounded callable
contract while keeping general platform linking and runtime policies explicit.

[Previous: Binary translation](12-bin2bin-decoding-and-translation.md) · [Contents](README.md) · [Next: Runtime and caches](14-runtime-translation-and-caching.md)
