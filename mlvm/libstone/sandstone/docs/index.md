# Sandstone documentation

Sandstone is a SymAS-driven assembler framework: every fact about an
assembly dialect — mnemonics, instruction forms, operand constraints,
Algol-like encoding functions, syntaxes, sections, linking, relocations,
and per-model optimization data — lives in a compressed `.symas`
databank (magic `0xCEEB09`), and every tool reads that bank instead of
hard-coding an architecture.

The normative format reference is [`SYMAS.md`](../SYMAS.md). This directory
holds the tool documentation:

- [`format.md`](format.md) — how a SymAS databank is organized and what the
  reader/builder APIs expose.
- [`compiler.md`](compiler.md) — compiling `.dsymspec` specifications into
  `.symas` banks (`scripts/spec-compiler.py`, `sandstone-spec-compiler`).
- [`assembler.md`](assembler.md) — the assembler framework and the
  `sandstone-as` CLI: parsing, directives, the two-pass binarizer, and the
  operand-binding convention.
- [`object.md`](object.md) — output formats: flat binaries and the JSON
  `sandstone-obj-1` object format.
- [`bindings.md`](bindings.md) — SWIG bindings for Python, Lua, and Java.

The Doxygen API reference is generated from `include/sandstone.h`,
`include/sandstone.hpp`, `assembler/assembler.h`, and
`compiler/include/domsymascc/domsymascc.hpp` via `docs/Doxyfile.in`.
