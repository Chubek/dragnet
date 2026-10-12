# Verification

Configure `MLVM_BUILD_TESTS=ON`, build, then run `ctest --test-dir build
--output-on-failure`. C++ checks remain active in release builds; C ABI tests are
compiled as C. Suites cover stable IDs/locations, malformed IR/use-def/dominance,
transactional passes/conversions, analysis invalidation, modular folding/effects,
ELF truncation/corruption, archives, resolution and relocation boundaries. Optional
Cephyr tests cover preprocessing, Tree-sitter syntax and semantic/codegen limits.

When Python is available, `mlvm-toolchain` runs all CLI families and the real
compile/assemble/archive/link slice. Linux/x86-64 hosts execute the generated ELF
and require exit status 42. Other hosts still verify deterministic outputs. GNU
readelf is an optional independent format oracle. No golden files are generated.

When `MLVM_ENABLE_BINUTILS` resolves to `ON`, `mlvm-binutils` checks ELF32/64 in
both byte orders, REL/RELA, NOBITS, groups, transactional edits, target contracts
and raw linking. `mlvm-binutils-cli` checks view flags, filters, strings, indexed
long-name archives, exact extraction, raw images/maps and overwrite protection.
GNU `ar`/`nm`/`as`/`readelf` provide optional independent interoperability checks.
The installed consumer also links and exercises `MLVM::Binutils` when available.

Verify installed package and C/C++ library consumers with:

```sh
cmake --install build --prefix "$PWD/install"
cmake -S tests/package -B build-consumer -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build build-consumer
ctest --test-dir build-consumer --output-on-failure
```

The dependency-free library configuration is:
`-DMLVM_ENABLE_CEPHYR=OFF -DMLVM_ENABLE_BINUTILS=OFF -DMLVM_BUILD_TOOLS=OFF`; its tests require no Python,
Tree-sitter, libstone build, LLVM, or network access.
