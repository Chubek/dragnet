# Installing Sandstone

## Prerequisites

| Requirement | Minimum | Notes |
| --- | --- | --- |
| CMake | 3.16 | Build configuration |
| C++ compiler | C++20 (GCC 10+, Clang 12+) | Core library, compiler, assembler |
| C compiler | C11 | C ABI + assembler |
| zlib | any | **Required**: default SymAS payload codec |
| Python | 3.8+ | `scripts/spec-compiler.py`, ISA loader, tests |
| stock Lua | 5.3+ | Running `.dsymspec` specs (any `lua` on `PATH`) |
| libzstd | any | Optional: zstd banks (auto-detected) |
| SWIG | 4.0+ | Optional: language bindings |
| Doxygen | any | Optional: API docs (`-DSANDSTONE_BUILD_DOCS=ON`) |
| Lua headers/lib | 5.1+ | Optional, run-time only: the Lua hook loads liblua via `dlopen()`; the framework builds and runs without it |

Debian/Ubuntu:

```sh
sudo apt install cmake g++ zlib1g-dev python3 lua5.4 libzstd-dev swig doxygen
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build
```

Useful flags:

```sh
-DSANDSTONE_BUILD_COMPILER=ON   # spec compiler (default ON)
-DSANDSTONE_BUILD_ASSEMBLER=ON  # assembler + sandstone-as (default ON)
-DSANDSTONE_BUILD_TESTS=ON      # smoke + pipeline + ISA tests (default ON)
-DSANDSTONE_BUILD_DOCS=ON       # Doxygen reference (default OFF)
-DSANDSTONE_WITH_MUNE=ON        # legacy DomMune CLI (needs DomWEAVE sources)
-DCMAKE_INSTALL_PREFIX=/usr/local
```

Without libzstd the build still succeeds; requesting or opening zstd
banks then fails with `DOMSYMAS_ERR_BAD_COMPRESSION`.

## Install

```sh
cmake --install build
```

Installs libraries (`sandstone`, `sandstone_assembler`,
`sandstone_spec_core`), binaries (`sandstone-spec-compiler`,
`sandstone-as`), public headers, the `stdspec`/`isa` data directories,
and (with docs enabled) the HTML reference. Downstream CMake projects
can target `sandstone`, `sandstone_assembler`, or `sandstone_spec_core`
after `find_package` wiring, or simply add this directory with
`add_subdirectory`.

## Verify

```sh
python3 scripts/spec-compiler.py --list-arches
python3 scripts/spec-compiler.py --validate -o /tmp/x86.symas \
    compiler/stdspec/x86.dsymspec
printf '.text\n_start:\n    nop\n' > /tmp/t.s
./build/assembler/sandstone-as --bank /tmp/x86.symas -o /tmp/t.bin /tmp/t.s
xxd /tmp/t.bin   # 90
```

## Platform notes

- Linux x86-64 is the tested platform. The code is portable C11/C++20
  with no OS-specific calls outside standard I/O and `dlopen()` (Lua
  hook); MSVC needs C++20 `/std:c++20` and zlib.
- Cross assemblers need no target toolchain: the bank fully describes
  the target, and output is validated (`ctest` runs the `.dsymspec` →
  `.symas` → assembled-bytes pipeline for x86 and RISC-V).
