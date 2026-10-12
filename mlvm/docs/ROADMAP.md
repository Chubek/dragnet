# MLVM Roadmap

- M0: Build scaffold, API conventions, CLI help/version, smoke tests.
- M1: Core context, source manager, diagnostics, IR ownership and verifier.
- M2: Dialect registry, textual IR parser/printer, conversion framework.
- M3: Pass manager, analyses, canonicalization and test infrastructure.
- M4: Object model, safe readers, target registry and format adapters.
- M5: Assembler and format-neutral inspection tools for one target.
- M6: Static linker, archives, relocations, map files, link integration tests.
- M7: Cephyr preprocessing, parsing, semantic analysis and diagnostics.
- M8: Cephyr lowering, machine codegen, assembly/object output.
- M9: Object manipulation utilities, more targets/formats, hosted C runtime.

Each milestone requires implementation, tests, documentation, and explicit
unsupported-feature reporting. Do not silently expand claims beyond test coverage.

## 0.1.0 implementation status

The milestones above describe the complete architectural target, not all shipped
functionality. The baseline implements a narrow vertical slice across them:

- M0: Offline CMake build, optional tools/tests/Cephyr, installable exported library
  targets, implemented C context ABI and common CLI conventions.
- M1/M2: Context interning, source manager, structured diagnostics, owned generic
  SSA IR/use-def, verifier, operation registry, versioned round-trip text, full
  transactional conversion graph. CFG dominance/successors and rich type/attribute
  schemas remain pending.
- M3: Transactional module passes, analysis cache/preservation, instrumentation,
  timing/dumps, modular constant folding, pure DCE. Nested-scope scheduling pending.
- M4/M5: Verified x86-64 ELF64 object model/codecs, narrow target registry and machine
  encoder/decoder, assembler, inspection, size and ASCII strings.
- M6: Relocatable/static executable linking, strong/weak resolution, checked
  relocations, short-name archive create/list/read and linker member extraction.
  Advanced linker/archive features are enumerated in `TOOLS.md`.
- M7/M8: Tree-sitter C parsing, object-macro preprocessing, constant-return C17
  semantic subset, generic IR lowering, x86 assembly/object output. General C
  semantics, typed-C staged lowering and general machine backend remain pending.
- M9: Section remove/select/rename, single-section binary extraction, conservative
  local-symbol stripping. Additional formats/targets and hosted runtime pending.

## Regiment notes

- Arithmetic dialect: `arith.divsi`, `arith.remsi`, `arith.and`, `arith.or`,
  `arith.xor` with same-width type checking; `constant-fold` covers them with
  checked signed division (undefined cases stay unfolded).
- Passes: `canonicalize` rewrites identity operations only
  (`--passes=constant-fold,canonicalize,dce`); covered by unit and CLI tests.
- Machine backend: `add`/`sub $imm32,%r32`, `push`/`pop %r32` encode, decode,
  assemble, disassemble, link, and execute natively (exit-status test).
- Cephyr semantics: constant `/`, `%`, `&`, `|`, `^`, `<<`, `>>` with
  division-by-zero, overflow, and shift-range diagnostics.

The library-backed binary-tool subsystem adds:

- ELF32/64 in both byte orders, REL/RELA, NOBITS, symbol visibility and section
  groups through a narrow Limestone Bin2Bin adapter (`MLVM::Binutils`).
- Dedicated tool commands with real view/filter options and shared klib parsing.
- Indexed GNU archives with long names, byte-preserving create/replace/delete/
  extract/print and GNU/BSD ingestion; static linker long-name archive support.
- Group/relocation-preserving section edits and explicit local/debug/all stripping.
- Size formats, string offsets and aligned UTF-16 ASCII extraction.
- tomlc99-parsed explicit target contracts, addressed raw linking and map files.

Availability is controlled by `MLVM_ENABLE_BINUTILS=AUTO|ON|OFF`. Detailed limits
and unsupported features are recorded in `TOOLS.md`. The dependency-free fallback
remains available with `OFF`; libstone sources are consumed read-only. Future
Unisel/Limeburg/Schedrow/RegTL adapters follow the same MLVM-owned boundary.
