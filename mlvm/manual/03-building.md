# 3. Configuring and building

[Contents](README.md) · [Next: Installation](04-installation.md)

The standalone build requires CMake 3.20 or newer and a C++20 toolchain. C
compilation is also enabled. Run from the MLVM directory:

```sh
cmake -S . -B build -DMLVM_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Selecting components

| Variable | Default | Meaning |
| --- | --- | --- |
| `MLVM_BUILD_TOOLS` | `ON` | Build command-line executables |
| `MLVM_BUILD_TESTS` | `ON` | Build and register tests |
| `MLVM_ENABLE_WARNINGS` | `ON` | Enable strict compiler warnings |
| `MLVM_ENABLE_CEPHYR` | `AUTO` | Optional Tree-sitter C frontend |
| `MLVM_ENABLE_BINUTILS` | `AUTO` | Optional library-backed binary tools |
| `MLVM_ENABLE_OPTILISP` | `AUTO` | Optional sfsexp-backed kernel integration |

The three `AUTO` features also accept `ON` and `OFF`. `AUTO` detects available
sources or runtimes; `ON` makes missing prerequisites a configuration error.
Use `ON` in a build intended to demonstrate a specific optional feature so that
its absence cannot be mistaken for a successful feature build.

For the dependency-free baseline:

```sh
cmake -S . -B build-minimal \
  -DMLVM_ENABLE_CEPHYR=OFF \
  -DMLVM_ENABLE_BINUTILS=OFF \
  -DMLVM_ENABLE_OPTILISP=OFF
cmake --build build-minimal --parallel
```

## Dependency locations

Cephyr needs the supplied C grammar and either supplied Tree-sitter runtime
sources or an ABI-15-compatible installed runtime. Binary tools use the supplied
Bin2Bin, klib, and tomlc99 sources. Optilisp uses sfsexp, normally under the
monorepo's `third_party/sfsexp`, one directory above MLVM.

`MLVM_THIRD_PARTY_DIR` and `MLVM_SFSEXP_SOURCE_DIR` override the Optilisp
dependency paths. Configuration does not download dependencies or regenerate
grammars. Read configuration messages before assuming a tool is available.

Reference: [CMake build](../CMakeLists.txt), [Optilisp build details](../optilisp/README.md).
