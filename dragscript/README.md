# Dragscript 0.2 — embedded TypeScript/TypeScript-family engine

Dragscript is an embedded-first, synchronous JavaScript/TypeScript **subset**
engine for Dragnet. It parses with Tree-sitter, builds owned semantic IR (DSIR),
lowers that tree to a registered and verified MLVM dialect, and can execute the
result through either of two backends that share one set of runtime contracts.
The library exposes compile-only and reusable-artifact execution APIs.

Implemented: primitive values with full number/string conversions, numeric
arithmetic, objects and arrays with property access, lexical `let`/`const`, blocks
and conditionals, `while`/`do`/`for`/`for-of` loops with `break`/`continue`,
function declarations, calls, returns, captured mutable bindings, identifier-only
updates and compound assignment, and explicitly registered host functions.
Selected TypeScript type-only constructs are erased. See the
[feature matrix](docs/feature-matrix.md) for the precise scope.

## Build and test

From the Dragnet root (no downloads, grammar generation, Node, or `tsc`):

```sh
cmake -S dragscript -B /tmp/opencode/dragscript-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/dragscript-build -j 4
ctest --test-dir /tmp/opencode/dragscript-build --output-on-failure
```

Use any writable out-of-source build directory. Requirements are CMake 3.20,
a C compiler, C++20, sibling `mlvm/`, and the checked-in TypeScript grammar C
sources. The build prefers `third_party/tree-sitter/lib/` when present. **This
checkout lacks that runtime directory**: it uses `pkg-config` to locate an
installed Tree-sitter >= 0.25 instead. Verification here used runtime 0.26.9,
grammar package 0.23.2 (ABI 14), and MLVM 0.1.0. Missing runtime dependencies
produce a configure error. The upstream grammar's regenerating CMake is bypassed.
Upstream MIT license notices remain in the dependency directories.

Options:

- `DRAGSCRIPT_BUILD_TESTS=OFF`: library-only build.
- `DRAGSCRIPT_DIFFERENTIAL_TESTS=ON`: additionally build the existing vendored
  QuickJS sources as a test-only oracle and compare the common JS subset on both
  backends.
- `DRAGSCRIPT_SANITIZERS=ON`: AddressSanitizer and UBSan with GCC/Clang.
- `DRAGSCRIPT_BUILD_REPL=OFF`: skip the `dragscript-repl` shell.
- `DRAGSCRIPT_BUILD_REPL_TESTS=OFF`: skip the headless REPL tests.
- `DRAGSCRIPT_DEPENDENCY_ROOT=/path/to/dragnet`: override sibling dependency root.

The public target is `Dragscript::Core`. A host can use
`add_subdirectory(path/to/dragscript)` and link that target. Public headers do
not expose MLVM or Tree-sitter types. The initial build provides a static library;
an installed package and stable binary ABI are future work.

The default suite has eight groups: frontend, runtime, backend, embedding,
limits, lowering, a 900-case deterministic mutation smoke test that runs each
case on both backends, and the headless REPL tests. With the oracle enabled, a
ninth group compares 737 supported-subset cases against QuickJS on both backends.

## REPL

`dragscript-repl` is an interactive shell built on `third_party/replxx`:

```sh
cmake -S dragscript -B /tmp/opencode/dragscript-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/dragscript-build -j 4 --target dragscript-repl
/tmp/opencode/dragscript-build/dragscript-repl
```

It keeps the text of every accepted submission and recompiles the session for each
new one, so declarations persist across lines. See [docs/repl.md](docs/repl.md) for
the session model, commands, and limitations.

## Embedding

```cpp
#include <dragscript/engine.hpp>
#include <iostream>

int main() {
    dragscript::EngineOptions options;
    options.backend = dragscript::Backend::mlvm_ir; // Execute the verified MLVM tree.
    dragscript::Engine engine(options);
    if (!engine.register_host_function("log", [](auto args, auto&) {
        std::cout << args[0].display() << '\n';
        return dragscript::HostResult{};
    }, {1, 1})) return 1;

    auto compiled = engine.compile({"page.ts", R"(
        function twice(x: number): number { return x * 2; }
        let total = 0;
        for (const item of [1, 2, 3]) { total += item; }
        log('page-ready:' + total);
        twice(total);
    )"});
    if (!compiled.ok()) {
        for (const auto& error : compiled.diagnostics)
            std::cerr << error.code << ": " << error.message << '\n';
        return 1;
    }
    dragscript::RunOptions limits;
    limits.max_steps = 1000;
    auto result = engine.run(compiled.script, limits);
    if (!result.ok()) return 1;
    return result.value.number() == 12 ? 0 : 1;
}
```

Host names are simple identifiers, e.g. `log`, rather than dotted property paths.
Each run has a fresh lexical environment. Host bindings belong to the Engine;
compiled artifacts and primitive results can outlive it and run in another
Engine, under that engine's limits. Engines require external synchronization, and
same-engine reentrancy is rejected. `CompiledScript::dump_dsir()` and
`dump_mlvm()` provide stable inspection formats for debugging. See
[architecture](docs/architecture.md) and [runtime semantics](docs/runtime-semantics.md)
for ownership, errors, budgets and host callback contracts.

## Milestones

M0–M5 have an executable baseline with focused tests. M6 has a fake page/log
capability test; actual Dragworks interfaces are absent from this checkout. M7 is
partially delivered: functions, closures, objects, arrays, property access and
control flow are executed and differentially tested, while exceptions, modules
and async remain. M8 groundwork exists — a verified MLVM dialect with a second
execution backend, contract-checked by negative verifier tests and cross-backend
agreement — but no optimization kernels or native execution exist yet. Parsing and
verified IR do not establish general ECMAScript or TypeScript compatibility.