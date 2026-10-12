# 30. Object files and relocations

[Contents](README.md) · [Next: Linking](31-linking.md)

MLVM has a baseline object model and an optional broader binary-object model.
Both own their data, but their accepted format subsets differ.

## Baseline object model

`mlvm::Object` contains sections, symbols, and RELA records. Sections own bytes,
flags, and alignment. Symbol section indexes are zero-based; `-1` means undefined
and `-2` means absolute. A relocation identifies the patched section, symbol,
offset, type, and signed addend.

`read_elf`, `write_elf`, and `verify_object` expose the baseline codecs. Their
implemented format is x86-64 little-endian ELF64 relocatable objects, supporting
WRITE/ALLOC/EXEC section flags and power-of-two alignments through 4096.
Relocations are ABS64, PC32, and statically resolved PLT32. Unsupported features
such as NOBITS, TLS, COMDAT, and REL records fail explicitly in this path.

## Library-backed objects

When enabled, `MLVM::Binutils` adds owning `BinaryObject`, `BinaryArchive`, and
`BinaryImage` types. Its relocatable ELF reader supports ELF32/64 in both byte
orders, REL/RELA, NOBITS, visibility, and section groups/COMDAT. Unknown relocation
numbers can be inspected and reserialized; applying them needs a target contract.

NOBITS carries logical size rather than a fabricated vector of file bytes.
Executables/shared objects, common symbols, extended section indexes, multiple
symbol tables, and unsupported indexed metadata remain outside this reader's
contract. Reading an ET_EXEC produced by the baseline linker is therefore not
an advertised inspection workflow.

## Relocation reasoning

Keep symbol identity and addends until layout is known. Check arithmetic and
field range before patching. A valid input record can still overflow after
placement; format validity alone is not link success.

Readers validate offsets and cumulative resource budgets before copying data.
The broader adapter bounds input/expanded output to 256 MiB and applies further
count and linking budgets detailed in the tools reference.

Reference: [object API](../include/mlvm/object.hpp), [binary API](../include/mlvm/binutils.hpp).
