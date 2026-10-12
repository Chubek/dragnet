# Spec compiler

The compiler turns human-readable architecture specifications into
compressed `.symas` databanks. It has two stages with different
dependencies:

```text
.dsymspec (Lua DSL)  ->  JSON document  ->  domsymas::Builder  ->  .symas
(stock Lua + dsl.lua)    (either tool)      (sandstone-spec-compiler)
```

## The DSL (`compiler/stdspec/`)

Each `<arch>.dsymspec` requires `dsl.lua`, obtains a fresh builder with
`require("dsl").new()`, describes targets, features, registers, encoders,
mnemonics, forms, syntaxes, sections, linkages, relocations, and
optionally machine models, functional units, selection/scheduling/tuning/
fusion records, then prints `symas.emit()` — a single JSON document
(`schema: 1`, records referencing each other **by name**).

The DSL is restricted to the Lua 5.4 subset (no patterns, coroutines, or
`goto`), so stock `lua` runs it; the JSON schema is documented in
`compiler/stdspec/README.md`.

Form references in the optimization records resolve by mnemonic name with
optional `#N` (Nth form, 1-based) or `/encoder` suffixes, or lists
thereof. The C++ mapping (`compiler/src/spec_document.cpp`) expands them
to one record per matching form and resolves every name to the numeric id
the builder assigns.

## `scripts/spec-compiler.py`

The scripted driver for `.dsymspec` sources:

```sh
python3 scripts/spec-compiler.py --validate -o x86.symas \
    compiler/stdspec/x86.dsymspec
python3 scripts/spec-compiler.py --list-arches
python3 scripts/spec-compiler.py --emit-spec merged.json *.dsymspec
```

It runs each spec under stock Lua (`--lua` to override, `--spec-dir` for
`dsl.lua`), collects the JSON documents, and invokes
`sandstone-spec-compiler` (`--compiler` to override, otherwise `PATH` or
the CMake build tree) to build the bank. Several inputs fold into one
bank; `--compression none|zlib|zstd` and `--validate` are passed through.

## `sandstone-spec-compiler`

The dependency-free CLI (`compiler/src/spec_compiler.cpp`, library code
in `compiler/src/spec_document.cpp` over the minimal `json.hpp` model):

```sh
sandstone-spec-compiler --compression zlib --validate \
    -o riscv64.symas riscv64.json
```

It accepts one or more JSON documents, maps each onto a single
`domsymas::Builder`, and saves the bank. `--emit-spec [FILE]`
concatenates the documents instead of building. Exit status is `0` on
success, `1` for I/O or compile errors, `2` for usage errors.

The legacy DomMune-embedded driver (`compiler/src/main.cpp`) is kept for
the original DomWEAVE environment and builds only with
`-DSANDSTONE_WITH_MUNE=ON` when DomMune/DomCLI sources are present.

## `scripts/compile-specs.py`

The historical helper that embeds every `.dsymspec` databank as a JSON
document in a C++ header (`domas_symas_banks.inc`) for consumers that link
banks statically. It is unrelated to the `.symas` build above.
