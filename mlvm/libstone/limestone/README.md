# Limestone

Limestone is CCWeave's late-stage compiler framework. It combines target metadata,
instruction selection, scheduling, register allocation, equality saturation,
binary translation, metatracing, and machine-level IR through explicit adapters.

```text
Metacode Infobank -> target contracts
                         |
machine-independent graph -> Tunah -> Unisel / Limeburg
                                          |
                                       Schedrow
                                          |
                                        RegTL
                                          |
                           final scheduling / spill transfers
                                          |
                           MachineIR exchange -> Bin2Bin encoding -> ELF objects
```

TraceML supplies a metatracing frontend. Exolayer supplies the native C ABI/FFI
boundary. VMWeave compiles Lua descriptions through STK-00 into embeddable C
runtimes with optional subsystems, plus MachineIR-backed assembly, native shared
images and baseline JIT execution through the host C toolchain.

## Manual

The [Limestone manual](manual/README.md) provides 22 comprehensive chapters covering
architecture, build and installation, worked examples, every subsystem, C/C++ and
Python embedding, target development, and diagnostics. Start with
[the first-program workflows](manual/03-first-programs-and-workflows.md) for a
hands-on introduction.

## Build and test

Requirements: CMake 3.20+, a C compiler, a C++20 compiler, and Perl. The parser,
solver, e-graph, and language-tooling dependencies are supplied in `third_party/`.
The default build also requires LMDB, libffi, and Lua 5.4 (or 5.3) development packages.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

C++ AddressSanitizer and UndefinedBehaviorSanitizer validation can use a separate
Clang build:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize -j 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-sanitize --output-on-failure -j 4
```

Runners using `ptrace` may prevent LeakSanitizer from starting. In that environment,
prefix both the sanitizer build and test commands with
`ASAN_OPTIONS=detect_leaks=0`; address and undefined-behavior checks remain enabled,
but leak checking is unavailable.

Configuration options:

| Option | Default | Purpose |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Subsystem, C/C++, CLI, installation, and integration tests |
| `LIMESTONE_ENABLE_PERSISTENT_CACHE` | `ON` | Bin2Bin LMDB storage; `OFF` retains memory caching |
| `LIMESTONE_ENABLE_NATIVE_FFI` | `ON` | Exolayer scalar native invocation through libffi |
| `LIMESTONE_BUILD_PYTHON_BINDINGS` | `OFF` | SWIG 4/Python development-module bindings |

With `LIMESTONE_ENABLE_NATIVE_FFI=OFF`, callbacks and extensions remain available;
`exl_native_available()` reports zero. D MachineIR tests are registered when both
`dub` and a D compiler are found. The C++/D exchange test additionally needs `dmd`
or `ldc2`. VMWeave's C++ and generated-runtime tests run in the normal build;
a Lua interpreter additionally enables the legacy standalone-DSL test.

The D package can also be tested directly:

```sh
cd metacode/machine-ir
dub test
```

## Use the CLI

```sh
printf '%s\n' '((lambda x (add x 2)) 40)' | build/limestone-cli
printf '%s\n' '(if (lt 1 2) (add 20 22) 0)' | build/limestone-cli --trace-execution
build/limestone-cli --select-burs tests/fixtures/selection.limeburg
build/limestone-cli --schedule-il tests/fixtures/scheduling.schedrow
build/limestone-cli --allocate-il --allocator color tests/fixtures/allocation.regtl
build/limestone-cli --isa metacode/infobank/isa/riscv64.isa
build/limestone-cli --help
```

Default TraceML compilation evaluates a closed integer program and produces
portable machine IR. `--trace-execution` retains the executed arithmetic and
branch guards. Executable target compilation uses a source graph, explicit target
patterns, allocation contracts, and an encoding/backend adapter; see
[`limestone/README.md`](limestone/README.md).

## Install and embed

```sh
cmake --install build --prefix /path/to/limestone
```

Consumers use a relocatable CMake package:

```cmake
find_package(Limestone 0.1 CONFIG REQUIRED COMPONENTS core il)
target_link_libraries(my_backend PRIVATE Limestone::core Limestone::il)
```

Headers retain their source-tree layout, for example
`<limestone/limestone.hpp>`, `<limestone/limestone.h>`, and `<limestone/il.h>`.
The installed package exports individual algorithm and adapter targets, including
`Limestone::unisel`, `Limestone::limeburg`, `Limestone::schedrow`,
`Limestone::regtl`, `Limestone::optimization`, `Limestone::tunah_unisel`, `Limestone::tunah_bin2bin`, `Limestone::bin2bin`,
`Limestone::bin2bin_object`, `Limestone::object`,
`Limestone::machineir_bridge`, and `Limestone::exolayer`. Enabled LMDB/libffi
libraries are resolved when the package is loaded. Infobank, tuner rules, and
VMWeave are installed under `share/limestone/` with default GNU install paths.

The C API uses owning opaque handles and structured diagnostics. Returned text,
instruction strings, and byte spans are borrowed from their handles. Independent
handles can be used independently; synchronize access to a shared mutable handle.
The C++ API returns `limestone::Result<T>` and keeps vendor implementation types
behind subsystem adapters.

## Component guides

- [Pipeline, target contracts, and C API](limestone/README.md)
- [Graph selection and UMD](unisel/README.md)
- [BURS selection and textual rules](limeburg/README.md)
- [Scheduling IL and verification](schedrow/README.md)
- [Register allocation and spill materialization](regtl/README.md)
- [Equality saturation and graph optimization](tunah/README.md)
- [Binary encoding, translation, and runtime ownership](bin2bin/README.md)
- [ELF objects, symbol relocation, and addressed linking](bin2bin/OBJECTS.md)
- [TraceML execution and trace lowering](traceml/README.md)
- [D MachineIR and C++/D exchange](metacode/machine-ir/README.md)
- [Exolayer callbacks and native invocation](exolayer/README.md)
- [VMWeave generation](vmweave/README.md)
- [Python bindings](bindings/README.md)
- [Grammar/AST generation](parsers/README.md)

[`IMPLEMENTATION.md`](IMPLEMENTATION.md) records implemented behavior, validation
coverage, and the remaining target/runtime adapters. Infobank ingestion alone does
not establish complete production code generation for every described ISA.
