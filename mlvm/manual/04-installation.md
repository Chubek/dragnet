# 4. Installation and embedding setup

[Contents](README.md) · [Next: First IR workflow](05-first-ir.md)

Install a completed standalone build into an explicit prefix:

```sh
cmake --install build --prefix "$HOME/.local/mlvm"
```

The package exports CMake targets and public headers. A minimal C++ consumer
project can use this `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(mlvm_client LANGUAGES CXX)
find_package(MLVM CONFIG REQUIRED)
add_executable(client main.cpp)
target_link_libraries(client PRIVATE MLVM::IR)
```

Configure the consumer from its own directory:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local/mlvm"
cmake --build build
```

## Choosing a library

| Target | Use |
| --- | --- |
| `MLVM::Core` | Contexts, diagnostics, C context ABI |
| `MLVM::IR` | SSA, parsing, verification, passes, conversions |
| `MLVM::Codegen` | Baseline target and instruction encoding |
| `MLVM::Object` | Baseline ELF, assembly, archives, linking |
| `MLVM::NodeIR` | Header-only stage-specific graphs |
| `MLVM::Binutils` | Optional broader binary-object APIs |
| `MLVM::Cephyr` | Optional frontend |
| `MLVM::OptilispPass` | Optional generic-module kernel execution |

Optional targets exist only when their component was enabled. The standalone
`optilisp/` package instead uses `find_package(MLVMOptilisp CONFIG REQUIRED)`
and exports the reader/loader libraries for independent consumers.

Static Cephyr consumers need the configured system Tree-sitter runtime when that
fallback was selected. Optilisp and binary-tool dependency archives are exported
with their respective package configurations.

## Kernel data after relocation

Optilisp-enabled installations include `share/mlvm/blackstone`, containing the
catalog and registry. When installing with a prefix different from the configured
one, point the CLI at the actual location with `--blackstone-dir=DIR`. C++ callers
supply registry and kernel paths explicitly.

Reference: [public API contract](../docs/API.md).
