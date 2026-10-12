# Sandstone

Sandstone is a SymAS-driven assembler framework. A SymAS databank
(`.symas`, magic `0xCEEB09`) encapsulates everything about an
architecture's assembly dialect — mnemonics, instruction forms, operand
constraints, Algol-like encoding functions, syntaxes, sections, linking,
relocations, and per-model optimization data — and every Sandstone tool
reads that bank instead of hard-coding an architecture.

```sh
python3 scripts/spec-compiler.py --validate -o x86.symas \
    compiler/stdspec/x86.dsymspec
sandstone-as --bank x86.symas --syntax intel -o out.bin prog.s
```

## Layout

| Path | Contents |
| --- | --- |
| `SYMAS.md` | Normative SymAS format reference |
| `decoder/` | SymAS databank library (`libsandstone`: reader, queries, encoder, builder) |
| `include/` | Public C/C++ headers (`sandstone.h`, `sandstone.hpp`; `domsymas.*` shims) |
| `compiler/` | Spec compiler: JSON documents → `.symas` (`sandstone-spec-compiler`) |
| `compiler/stdspec/` | Shipped `.dsymspec` architecture specifications (Lua DSL) |
| `assembler/` | Assembler framework (`libsandstone_assembler`) + `sandstone-as` CLI |
| `bindings/` | SWIG 4.0 interface (`sandstone.i`) + `generate-bindings.sh` |
| `isa/` | Small `.isa` sources with Python (`simple_isa.py`) and C++ loaders |
| `scripts/` | `spec-compiler.py` (`.dsymspec` → `.symas` driver), `compile-specs.py` |
| `tests/` | C++ smoke test, Python ISA goldens, end-to-end pipeline test |
| `docs/` | Guides (this file's siblings) + Doxygen setup |

`GUIDE.md` walks through common tasks; `INSTALL.md` covers building,
installing, and platform notes. The historical `DomSymAs` names survive
as header shims (`domsymas.h`), library alias (`domsymas`), and record
prefixes (`DOMSYMAS_*`, `DomSymAs_*`), which the format reserves for
stability.

## Quick start

Build (see `INSTALL.md` for prerequisites):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Compile a bank and assemble:

```sh
python3 scripts/spec-compiler.py -o riscv64.symas \
    compiler/stdspec/riscv64.dsymspec
./build/assembler/sandstone-as --bank riscv64.symas -o hello.bin hello.s
```

An `add` assembles identically through the `.symas` bank and the small
`.isa` loader (`isa/simple_isa.py`), which is handy for cross-checking.

## SandSLAB code generation

The C++20 [SandSLAB library and Forth compiler example](sandslab/README.md)
use the instruction forms in `isa/` as a machine-near IR, with assembly and
binary emission and local branch fixups. Build with
`SANDSTONE_BUILD_SANDSLAB=ON` (default); set `DOMDSL_INCLUDE_DIR` if DomDSL is
outside the sibling `domlibs/domdsl` directory.
