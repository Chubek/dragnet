# Object files and addressed linking

`Limestone::bin2bin_object` exposes the owning C++ model in `bin2bin/object.hpp`.
`Limestone::object` exposes the C ABI in `limestone/object.h`. ELF container
handling and symbol resolution are independent of decoding, selection, scheduling,
allocation, and executable installation.

## Target contract

An authoritative ISA description or host adapter supplies `tooling.object_file`.
The independent version-1 schema is `metacode/infobank/object-file.schema.json`:

```text
tooling { object_file = {
  format = elf64; version = 1;
  machine = 62; endianness = little; flags = 0;
  osabi = 0; abi_version = 0; text_alignment = 16;
  relocations = [
    { type = 1; name = abs64; kind = absolute;
      storage_bytes = 8; bit_offset = 0; bits = 64;
      scale = 1; signed = false; },
    { type = 2; name = pc32; kind = pc_relative;
      storage_bytes = 4; bit_offset = 0; bits = 32;
      scale = 1; signed = true; }
  ];
}; }
```

This example declares the ELF x86-64 absolute-64 and PC-relative-32 encodings;
`tests/fixtures/object-x86.isa` includes the tested native call relocation too.
`format` accepts `elf32` or `elf64`; `tests/fixtures/object-i386.isa` supplies
an ELF32 fixture with explicit REL addend interpretation.
Numeric machine and relocation identities are target data. No ISA identity,
relocation formula or calling convention is inferred from architecture names.
The synthetic register-machine fixture declares its own distinct ELF identity.

Top-level identity/alignment/relocation fields are required. An optional
`opaque_section_types` array declares byte-preserving OS/processor-specific
sections, such as the native fixture's unwind section. Such sections must have
no indexed link/info contract; special symbol-indexed contents need an adapter.
Each relocation has a unique numeric `type`
and `name`, a storage width of 1–8 bytes, a contiguous bit field, a positive scale,
and explicit signedness. `kind` is `absolute` or `pc_relative`; optional signed
`pc_bias` applies only to PC-relative encodings. Unknown fields and expressions,
invalid widths, duplicate identities, and non-power-of-two alignment fail before
emission/linking.

An absolute field encodes `(S + A) / scale`. A PC-relative field encodes
`(S + A - (P + pc_bias)) / scale`, where `S` is the resolved symbol address, `A`
the explicit RELA addend and `P` the relocation storage address. Division must be
exact and the result must fit the declared signed/unsigned width. Other bits in
the storage unit are retained. Disjoint fields may share a unit; overlapping
relocation bits fail. Checked arithmetic supports full 64-bit addresses and
negative displacements, including the signed 64-bit minimum.

For REL, the addend comes from the original section contents, not the relocation
record or partially patched image. The optional Boolean
`implicit_addend_signed` is required to link a REL record: it specifies whether
the same declared bit field is sign-extended or unsigned, independently of the
result's `signed` range check. The extracted integer is multiplied by `scale`
with overflow checking before applying the relocation expression. Other bits
are not part of the addend. NOBITS provides zero. Encodings with a different
implicit-addend layout require an adapter; missing contracts fail explicitly.

## Owning object model

`ObjectFile` contains ELF identity, declared sections, symbols and relocations.
Section/symbol references are stable zero-based indexes. `object_undefined` and
`object_absolute` are separate symbol-section identities. PROGBITS, NOTE and
NOBITS sections preserve flags, alignment, entry size, raw contents and zero-fill
size. OS/processor section contents are preserved opaquely; allocated instances
require an explicit target type contract when linked. ELF notes are opaque owning contents; interpretation and host execution
requirements remain with the installation/ABI adapter.

`load_elf` accepts ELF32 and ELF64 relocatable objects in either byte order, with
direct section indexes, a single symbol table and REL or RELA records (including
mixed tables). It checks ranges, table shapes, null entries,
local/nonlocal partitions, string termination, symbol bounds, alignment and
overlapping file storage. Unknown relocation type numbers remain inspectable
through loading and emission; linking requires their target encodings.
`emit_elf` deterministically lays out content sections, a local-first symbol table,
strings and per-section REL/RELA tables, retaining record order within each table.
References are remapped to emitted indexes.
Symbols retain binding, type and visibility. Input object data is never mutated.
`ObjectFormat::elf_class` defaults to `ElfClass::Elf64`; ELF32 is explicit and
part of link compatibility. ELF32 section/symbol values, eight-bit relocation
types, 24-bit relocation symbol indexes and signed 32-bit explicit addends are
validated before serialization; values are never silently truncated.
`ObjectRelocation::implicit_addend` distinguishes REL from RELA. REL records
must have zero in the model's explicit `addend` member; their bytes are preserved
without interpreting target semantics during loading or emission.

Defaults bound input/output and expanded section/name bytes to 64 MiB, content
sections to 16,384, and symbols/relocations to 1,048,576 each. C++ `ObjectLimits`
can configure these budgets. C and Python use the defaults. Invalid metadata,
containers and references produce structured diagnostics; resource exhaustion
does not publish partial output.

## Linking

```cpp
auto target = limestone::bin2bin::object_target(metadata);
auto object = limestone::bin2bin::load_elf(elf_bytes, "unit.o");
if (!target || !object) return report_error();
limestone::bin2bin::LinkOptions options;
options.base_address = installation_address;
options.externals["host_symbol"] = host_address;
auto image = limestone::bin2bin::link_objects(
    std::span{&object.value(), 1}, target.value(), options);
```

Allocated sections are laid out in input/declaration order at aligned addresses.
Padding and NOBITS storage are zeroed. Images own bytes, section placement records,
and lexically ordered resolved global/weak symbols. Local symbols are object-scoped.
A strong definition replaces a weak definition, duplicate strong definitions
fail, and the first weak definition wins deterministically. Missing strong
references require explicit external addresses; unresolved weak references use
zero. Hidden/internal undefined references cannot use an external fallback.
ELF class, machine, byte order, ABI identity and flags must match the target contract.
Nonallocated sections stay in objects but are not part of the addressed image.

Linking bounds the aggregate content-section/symbol/relocation counts by the
default object budgets and the object count to 16,384. External symbol names
must be nonempty and NUL-free, with at most 1,048,576 external entries.
Linking applies all allocated-section fixups transactionally, with an explicit
image byte limit and installation base. It returns bytes; the embedding runtime
owns memory mapping, permissions, invocation, ABI compatibility and lifetime.
The native integration test loads two compiler-produced objects, resolves a call,
installs the image at its declared base, and executes result **42**. A separate
test re-emits an object and links/executes it with the system compiler/linker.
The ELF32 interoperability test assembles actual i386 REL records, links them
with Limestone, then re-emits them for the system linker. It needs no 32-bit C
runtime and does not execute 32-bit code.

Executables/shared objects, archives, extended indexes, compressed
sections, COMDAT/group processing, TLS/GOT/PLT construction, dynamic binding and
composed, noncontiguous or multi-instruction fixups, and symbol-indexed address-significance
tables require additional adapters. The raw
linker admits ordinary allocated write/alloc/execute flags and explicit scalar
symbol types; unsupported allocated flags and symbol kinds fail explicitly.

## Compiler and language APIs

`limestone::make_object(module, target, symbol)` packages an encoded module and
converts its backend's named relocations. Metacode-backed object targets retain
architecture identity, which must agree with the encoded module. ABI lowering
and the generation of external references remain backend responsibilities.

The C API creates/loads/builds/inspects/serializes objects and links owning images.
Builder operations validate transactionally; section/symbol/relocation output
storage is preserved on failure. Inspection spans and strings are borrowed until
object mutation/destruction. Serialized data and linked images are immutable and
independent of source handles. `limestone_module_object` also requires
`Limestone::core`. All destroy functions accept NULL.
The C descriptor layouts are unchanged. `limestone_object_elf_class` returns
32 or 64 (zero for NULL). `limestone_object_add_relocation_ex` takes an explicit
0/1 implicit-addend flag, and `limestone_object_get_relocation_ex` returns it
alongside the unchanged relocation descriptor. The original builder creates
RELA; the original getter rejects REL rather than misreporting a zero addend.
Invalid flags and REL records carrying explicit addends fail transactionally.

Python `ObjectTarget`, `ObjectFile` and `LinkedImage` provide the same context-manager
lifecycle. `ObjectFile.emit()`, `sections`, `symbols`, and image `bytes`/`symbols`
return Python-owned copies. `ObjectTarget.link(objects, base_address=...,
externals={...})` retains the input handles for the synchronous call and returns
an independent image.
`ObjectFile.elf_class` and `relocations` expose class and copied relocation
records; `add_relocation(..., implicit_addend=True)` builds a REL entry using
the existing section bytes as its addend.

CLI operations:

```sh
limestone-cli --compile-umd --target-isa target.isa --allocate --encode \
  --object entry graph.umd -o code.o
limestone-cli --inspect-object code.o
limestone-cli --wrap-object target.isa entry code.bin -o code.o
limestone-cli --link-objects target.isa --input-object code.o \
  --input-object helper.o --base-address 4096 -o image.bin
```
