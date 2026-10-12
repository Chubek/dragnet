# SymAS databank format (reader's guide)

This page summarizes [`SYMAS.md`](../SYMAS.md) for tool implementers. The
spec is normative; when the two disagree, the spec wins.

## Container

A `.symas` file is a 23-byte little-endian header followed by one
compressed payload:

- magic `CE EB 09`, format version, flags, compression id (`0` none,
  `1` zlib, `2` zstd), uncompressed/compressed sizes, CRC32 checksum.
- The payload holds a databank header (`SDB1`), a section directory, a
  shared string table, and the section bodies.

Section ids 1–14 are the encoding sections (`STRINGS` … `DEBUG`); ids
15–20 are the optional optimization sections (`ISEL`, `SCHED`, `UNITS`,
`MODELS`, `TUNING`, `FUSION`), written with the `OPTIONAL` directory flag
so older readers skip them by size. A bank without optimization sections
is complete: it describes encoding, not optimization.

## Records

- **TARGET**: architecture, address sizes, alignment, byte order, object
  format, ABI, calling convention, feature set.
- **REGISTERS**: name, class (GPR/segment/control/debug/vector/mask/
  FP/status/special), width, encoding number, subregister links, flags.
- **FEATURES**: named capabilities gating forms and selection records.
- **MNEMONICS → FORMS**: one mnemonic maps to several instruction forms
  differing in operands, encoding, features, or syntax.
- **OPERANDS**: positional descriptors (kind, width, register class,
  immediate range, access, visibility).
- **ENCODERS**: Algol-like functions (`emit8`, `emit_modrm`,
  `emit_relocation`, `require`, `if/else`, `fail`) executed with operand
  bindings under step/output/relocation limits.
- **SYNTAX**: per-dialect spelling (operand order, register/immediate
  prefixes, comment delimiter, separator, template).
- **SECTIONS / LINKING / RELOCATIONS**: object-section flags, symbol
  binding/visibility/type, relocation width/offset/signedness/PC-relative
  behavior/addend behavior/overflow rules.
- **Optimization** (`ISEL`/`SCHED`/`UNITS`/`MODELS`/`TUNING`/`FUSION`):
  operation-to-form selection, per-form costs and pipeline resources,
  machine models, per-model overrides, fusible pairs.

## Library mapping

- C++: `domsymas::Databank` (queries, `encode`, `disassemble`), and
  `domsymas::Builder` (producer side; record ids equal list indices).
- C: `DomSymAs_Bank` with the same coverage (`domsymas_*` in
  `include/sandstone.h`). Borrowed pointers stay valid until the bank
  closes; encode/disassemble outputs are caller-owned.
- The pure-C probe (`domsymas_probe_magic`, `domsymas_crc32`) compiles
  without C++ for minimal readers.

## Compression layering

The `.symas` payload compression (none/zlib/zstd, chosen per build) is
independent of any outer archive layer. The Sandstone library drives zlib
directly and libzstd when `SANDSTONE_HAS_ZSTD` is set; without zstd, zstd
banks fail with `DOMSYMAS_ERR_BAD_COMPRESSION`.
