# MLVM Documentation

- [`../manual/README.md`](../manual/README.md) — 40-chapter user and developer manual, from setup and IR to kernel authoring and native toolchain workflows.
- `ARCHITECTURE.md` — layers, dialects, conversions, and dependency direction.
- `TOOLS.md` — assembler, linker, and binutils contracts.
- `CEPHYR.md` — Cephyr C compiler stages and conformance strategy.
- `API.md` — public C/C++ API rules.
- `KERNELS.md` — applying Blackstone kernels and authoring Optilisp kernels via C++ and textual IR.
- `ROADMAP.md` — staged implementation milestones.

MLVM 0.1.0 implements a tested baseline: context/diagnostics, generic SSA IR,
versioned text, dialect definitions, transactional passes/conversions, and an
x86-64 ELF64 compile/assemble/archive/link/inspection slice. Supported behavior
and remaining contracts are recorded in `ROADMAP.md`, `TOOLS.md`, and `CEPHYR.md`.

## Build

```sh
cmake -S . -B build -DMLVM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

No network access, LLVM, or grammar regeneration is required. Cephyr defaults to
`MLVM_ENABLE_CEPHYR=AUTO`: use checked-in Tree-sitter runtime sources when present,
otherwise an installed ABI-15-compatible runtime and the vendored C grammar.
`ON` requires these dependencies; `OFF` disables Cephyr.
`MLVM_ENABLE_BINUTILS=AUTO` enables library-backed ELF32/64 and indexed-archive
tools using checked-in Limestone Bin2Bin, klib and tomlc99. `ON` requires the
sources; `OFF` selects the dependency-free binary-tool fallback. A fully
dependency-free configuration disables both features. Other DOMBuildFile
dependencies remain declared for future adapters.
`MLVM_BUILD_TOOLS=OFF` supports library-only builds.

## Compile and execute the example (Linux/x86-64)

```sh
build/cephyr -c examples/answer.c -o build/answer.o
build/mlvm-as examples/start.s -o build/start.o
build/mlvm-ld build/start.o build/answer.o -o build/answer
build/answer
# exit status 42
build/mlvm-objdump -d build/answer.o
build/mlvm --passes=constant-fold,dce examples/answer.mlvm
```

With library-backed tools enabled:

```sh
build/mlvm-ar q build/answer.o -o build/libanswer.a
build/mlvm-nm --extern-only build/libanswer.a
build/mlvm-readelf --sections --symbols build/answer.o
build/mlvm-ld --target-config examples/targets/x86_64.toml -O binary \
  build/start.o build/libanswer.a --map build/image.map -o build/image.bin
```

See `IR.md` for the versioned grammar and `API.md` for ownership and extension APIs.
