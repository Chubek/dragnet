# Chapter 2. Building, Installing, and Validating

[Previous: Architecture](01-framework-and-architecture.md) · [Contents](README.md) · [Next: First workflows](03-first-programs-and-workflows.md)

## 2.1 Build prerequisites

The C and C++ framework uses CMake 3.20 or newer, a C compiler, a C++20 compiler,
and Perl. C++ extensions are disabled in the main build. The parser generator,
SAT substrate, e-graph engine, and language-tooling integrations are supplied
under `third_party/`; their types remain behind component interfaces.

The default configuration also enables persistent translation caching and native
foreign calls. Those features require LMDB and libffi development packages. An
LMDB runtime library alone is insufficient because configuration checks for
`lmdb.h`. Similarly, native invocation needs `ffi.h` and the libffi library.
Package names depend on the operating system.

Optional tools extend the validation matrix:

- SWIG 4 and Python interpreter/development-module headers build Python bindings.
- Lua 5.4 or 5.3 development packages build VMWeave's C++/Kaguya frontend. A Lua
  interpreter additionally enables the standalone builder compatibility test.
- `dub` and a D compiler build/test the MachineIR package.
- `dmd` or `ldc2` enables the C++/D/C++ exchange integration test.

The D package is a separate language build. A successful C++ build is not evidence
that D tests ran. Inspect CTest registration and tool discovery when interpreting
validation results.

## 2.2 A normal development build

Run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

The configure step selects compilers, resolves optional dependencies, and writes
the build system. The build step compiles the vendored DParser generator, creates
parser tables and owning syntax-tree code, then compiles the public libraries,
tools, and registered test programs. CTest runs the tests present in that
configuration.

`CMAKE_EXPORT_COMPILE_COMMANDS` is enabled by the project. The resulting
`build/compile_commands.json` is useful for editor integration and tools that
need accurate include paths and definitions.

Choose a separate binary directory for a different compiler, feature combination,
or sanitizer configuration. This avoids mixing incompatible object files and
keeps test results attributable to a specific configuration.

## 2.3 Configuration options

| Option | Default | Behavior |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Build and register the framework's tests |
| `LIMESTONE_ENABLE_PERSISTENT_CACHE` | `ON` | Enable Bin2Bin's LMDB storage adapter |
| `LIMESTONE_ENABLE_NATIVE_FFI` | `ON` | Enable Exolayer's isolated libffi invocation adapter |
| `LIMESTONE_BUILD_PYTHON_BINDINGS` | `OFF` | Build the SWIG Python module |
| `CMAKE_BUILD_TYPE` | Generator-dependent | Select debug/release flags for single-configuration generators |
| `CMAKE_INSTALL_PREFIX` | CMake default | Set the installation prefix |

For a build using memory caches and host callbacks without native invocation:

```sh
cmake -S . -B build-minimal \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLIMESTONE_ENABLE_PERSISTENT_CACHE=OFF \
  -DLIMESTONE_ENABLE_NATIVE_FFI=OFF
cmake --build build-minimal -j 4
ctest --test-dir build-minimal --output-on-failure
```

Disabling persistent storage preserves in-memory translation caching. Explicitly
opening persistent storage still reports unsupported behavior; it does not
quietly choose memory storage. Disabling native FFI preserves callback registry
operations, extension loading, and the independent scalar layout interface.
Native registrations and native type construction require the corresponding
backend and report unavailability when it is absent.

For a release build, use `-DCMAKE_BUILD_TYPE=Release`. The tests are written to
retain correctness assertions where the build integration requires them. Select
the desired configuration with `--config` when using a multi-configuration
generator.

## 2.4 Presets and explicit commands

The repository supplies a `default` configure/build/test preset:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Its configure preset uses Unix Makefiles, the `build/` directory, Debug mode,
and testing. The preset file uses schema version 6, so preset users need a CMake
release that understands that schema. The explicit `cmake -S/-B` commands remain
the baseline for environments using the project's minimum CMake version.

Presets do not replace dependency installation. They also do not automatically
enable Python bindings or provide a D toolchain.

## 2.5 Generated parsers and specifications

Two distinct generation workflows exist.

**Parser generation** uses each `parsers/<language>.g` grammar and its matching
`.absyn` syntax-tree schema. Generated C tables and C++ tree code live in
`build/generated/parsers/`. A normal build regenerates them through dependencies.
To invoke the generation target explicitly:

```sh
cmake --build build --target limestone-generate-parsers
```

**Infobank specification generation** produces the checked-in Limeburg `.lburg`
files and coverage report from authoritative metadata:

```sh
cmake --build build --target limeburg-regenerate-specs
build/limeburg-generate-specs --check metacode/infobank limeburg/specs
```

The first command writes regenerated specifications. The second is read-only and
fails on stale or missing output. It is appropriate in validation and continuous
integration. A parser table is a build artifact; an Infobank-derived specification
is repository data with an explicit regeneration/freshness relationship.

## 2.6 Understanding the public library targets

The build exports namespaced targets that can be used both inside the checkout
and through an installed package:

| Target | Typical use |
| --- | --- |
| `Limestone::foundation` | Shared C++ result/ownership utilities |
| `Limestone::parsers` | Owning syntax trees and visitors |
| `Limestone::metacode` | ISA ingestion and metadata normalization |
| `Limestone::unisel` | Graph selection and UMD loading |
| `Limestone::limeburg`, `Limestone::limeburg_umd` | Core BURS and graph/UMD adaptation |
| `Limestone::limeburg_text`, `Limestone::limeburg_infobank` | Text documents and Infobank rule generation |
| `Limestone::schedrow`, `Limestone::schedrow_text` | Scheduling model/algorithms and text loading |
| `Limestone::regtl`, `Limestone::regtl_text`, `Limestone::regtl_schedrow` | Allocation, text loading, and scheduling/spill adapters |
| `Limestone::tunah`, `Limestone::tunah_unisel`, `Limestone::tunah_bin2bin` | Saturation and concrete IL adapters |
| `Limestone::bin2bin`, `Limestone::bin2bin_codegen`, `Limestone::bin2bin_object` | Binary facilities, compiler encoding, and objects |
| `Limestone::machineir_bridge` | Versioned C++ region exchange |
| `Limestone::traceml`, `Limestone::exolayer` | Frontend and native interface |
| `Limestone::core` | Connected compiler and runtime C/C++ entry points |
| `Limestone::il`, `Limestone::optimization`, `Limestone::object` | Standalone opaque C APIs |

Select the narrowest target that provides the interface you need. The exported
package carries implementation link dependencies; applications do not need to
reconstruct the internal static-library order or vendor include paths.

## 2.7 Installing Limestone

After building:

```sh
cmake --install build --prefix /tmp/opencode/limestone-prefix
```

With the usual GNU directory layout, installation includes:

- `bin/limestone-cli` and `bin/limeburg-generate-specs`;
- public headers under `include/`, preserving component paths;
- static libraries and the CMake package under the configured library directory;
- Infobank data under `share/limestone/infobank`;
- Tunah tuners, Limeburg specifications, and the VMWeave Lua module;
- component documentation and this manual under `share/doc/limestone`.

Library directories and data directories follow `GNUInstallDirs`, so avoid
hard-coding `lib` when packaging for a platform whose convention differs. Python
installation has its own configurable directory, described in Chapter 19. The
manual lives in `manual/` in the checkout and `share/doc/limestone/manual` in the
default installation layout. Source-code and fixture links refer to the checkout;
chapter navigation also works within the installed copy.

The installed-consumer test installs and relocates a prefix before building
external C and C++ programs against it. This tests the package boundary rather
than relying on source-tree include paths.

## 2.8 An external CMake consumer

Create a consumer project with:

```cmake
cmake_minimum_required(VERSION 3.20)
project(LimestoneConsumer LANGUAGES CXX)
find_package(Limestone CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Limestone::core)
```

Its `main.cpp` can be:

```cpp
#include <limestone/limestone.hpp>
#include <iostream>

int main() {
  auto result = limestone::run_pipeline("(add 20 22)");
  if (!result) {
    std::cerr << result.error().message << '\n';
    return 1;
  }
  std::cout << result.value().machine_ir;
}
```

Configure that consumer with the installation prefix:

```sh
cmake -S consumer -B consumer-build \
  -DCMAKE_PREFIX_PATH=/tmp/opencode/limestone-prefix
cmake --build consumer-build
```

For a C source using a static C++ implementation, an external CMake project can
enable both C and CXX and set the executable's `LINKER_LANGUAGE` to CXX. The public
C header remains valid C; the final link must still supply the C++ runtime.

## 2.9 Focused and optional validation

List tests before interpreting a result:

```sh
ctest --test-dir build -N
ctest --test-dir build -R 'selection|operand-constraints|limeburg-specs' \
  --output-on-failure
ctest --test-dir build -R 'objects|native-pipeline|backend' --output-on-failure
```

Python has a separate build option and test. D tests are registered only when
tools are found. VMWeave tests likewise depend on Lua discovery. Native executable
integration tests have platform-specific prerequisites, including architecture
and compiler support. Test counts therefore describe a configuration, not a
permanent universal number.

To run D package tests directly:

```sh
cd metacode/machine-ir
dub test
```

When multiple CMake builds point at the same source checkout, avoid concurrently
running `dub test` in that shared source directory. Other build-local tests can
run in parallel when their resources do not overlap.

## 2.10 Sanitizer configuration and build troubleshooting

A separate GNU/Clang sanitizer build can use:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DLIMESTONE_ENABLE_PERSISTENT_CACHE=OFF \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize -j 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build-sanitize --output-on-failure
```

Investigate failures at the appropriate layer. A missing LMDB header is a
configuration dependency problem. A missing Perl executable prevents syntax-tree
generation. A generated-header error may indicate a grammar/schema mismatch.
An installed consumer that works only with the source checkout nearby points to
an export/include-path issue. A Python import failure often indicates a module
path or interpreter-version mismatch.

Record the compiler, configuration flags, discovered tools, test registration,
and failing command when reporting a build problem. These facts make it possible
to reproduce the same boundary instead of comparing unrelated configurations.

[Previous: Architecture](01-framework-and-architecture.md) · [Contents](README.md) · [Next: First workflows](03-first-programs-and-workflows.md)
