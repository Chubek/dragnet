# Installation

Requirements: CMake 3.20+, C++17, and the C++ standard library. Firestone needs
no runtime library, generated parser, package download, or third-party solver.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

Set `CMAKE_PREFIX_PATH` to the installation prefix in a consumer configuration.
The exported package is relocatable. Installed headers live under
`include/firestone`; exported targets add that directory to include paths.

```cmake
find_package(Firestone CONFIG REQUIRED COMPONENTS ir eqsat)
target_link_libraries(compiler PRIVATE Firestone::ir Firestone::eqsat)
```

Available components are `ir`, `rewrite`, `eqsat`, `cps`, `memory`, `object`,
`sema`, `type`, `vm`, `bignum`, `compute`, `concurrency`, `gfx`, and `intrin`.
`Firestone::firestone` links all components; individual
targets carry their public dependencies. Every `lib*/*.hpp` header is installed
and compiled independently by the test build.

For vendored embedding:

```cmake
add_subdirectory(path/to/firestone)
target_link_libraries(compiler PRIVATE Firestone::firestone)
```

For direct compilation, add the Firestone source directory to the include path
and compile with `-std=c++17` or newer. All definitions are inline or templated.
Thread consumers also need the platform's thread link flags (typically `-pthread`).
CMake propagates `Threads::Threads` through the concurrency and compute targets.

`-DFIRESTONE_ENABLE_OPENMP=ON` enables host parallel execution and requires the
compiler's OpenMP runtime. This dependency propagates into installed consumers.
Without that option, `OpenMP::parallel_for` and `parallel_reduce` execute serially.
CUDA, HIP, OpenCL, SYCL, Wayland, X11, DRM, and shader compilers integrate through
caller-provided callbacks; no vendor SDK is required to build or install Firestone.
See [foundation contracts](docs/Foundations.md) for adapter ownership and semantics.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

`BUILD_TESTING=OFF` disables test executables, independent header compilation,
and the installed-consumer test. Installation still exports all components.

For VM-only consumers, request `COMPONENTS vm`, link `Firestone::vm`, and include
`<libvm/Compiler.hpp>` and `<libvm/RegisterVM.hpp>` or `<libvm/StackVM.hpp>`.
The component carries its IR and checked-arithmetic dependencies.
See [VM contracts](docs/VM.md) for embedding and backend APIs.
