# Initial implementation verification (regiment 2)

Verified on Linux with GCC 16.2.1, CMake's Makefile generator, installed
Tree-sitter 0.26.9, the checked-in TypeScript/TSX 0.23.2 parsers (ABI 14), and
MLVM 0.1.0. No dependency download or grammar regeneration was used. Build
directories and the external consumer probe live outside the source tree.

## Dragscript Debug and differential suite

```sh
cmake -S dragscript -B /tmp/opencode/ds2 \
  -DCMAKE_BUILD_TYPE=Debug -DDRAGSCRIPT_DIFFERENTIAL_TESTS=ON
cmake --build /tmp/opencode/ds2 -j 4
ctest --test-dir /tmp/opencode/ds2 --output-on-failure
```

Result: **8/8 test groups passed** (frontend, runtime, backend, embedding,
limits, lowering, fuzz-smoke, differential). The plain build compiles with
**zero warnings** under `-Wall -Wextra -Wpedantic`.

Group coverage:

- **frontend** — source modes, `.jsx` opt-in, syntax recovery and missing-node
  diagnostics, source positions, roughly 50 rejected constructs (spread,
  destructuring, methods, labels, `for-in`, chained assignment forms, private
  names, optional chaining, modules, classes, generators, async, templates,
  update/compound forms), and the accepted surface.
- **runtime** — number formatting, both conversion directions, strings and
  UTF-16 code units, objects, arrays, property access, truthiness, `typeof`,
  all loops, per-iteration bindings, updates, compound assignment, TDZ and const
  enforcement, closures, completion values, and their failure paths.
- **backend** — the same programs on both execution backends, artifact
  interchange between engines, and agreement on failure codes.
- **embedding** — host call ordering, arity and boundary failures, translation
  of host exceptions, shadowing, per-engine isolation, artifact reuse,
  reentrancy rejection, and compile-only behavior.
- **limits** — exact-limit and one-over-limit checks for source bytes, nodes,
  compile depth, parse checks, diagnostics, steps, call and evaluation depth,
  logical heap, string units, array length, object property count and property
  name length, on both backends.
- **lowering** — canonical text round trips, provenance on the lowered module
  (not the reparsed text), conservative effects, and 31 verifier-negative
  mutations covering regions, yields, child shapes, tags, payloads, flags,
  bindings, operators and use lists.
- **fuzz-smoke** — 900 deterministic mutations of 14 seeds, each compiled and run
  on both backends with bounded limits; asserts agreement, budget compliance,
  no internal errors, and that a failed request leaves the engine usable.

## Differential suite against QuickJS

The oracle is the vendored QuickJS sources, built as a test-only library. Each
case is evaluated by QuickJS in strict mode and by **both** Dragscript backends,
which must agree with each other and with the oracle.

Result: **737 supported-subset cases passed on both backends**, covering primitive
values, signed zero/NaN/infinities, `Number::toString` formatting, radix and
whitespace string conversions, arithmetic and comparison matrices, short-circuit
evaluation, `typeof`/`void`, objects, arrays, dense array growth, string indexing,
`join` semantics, property assignment, updates and compound assignment, all loop
forms with `break`/`continue` and completion values, `for-of` over arrays and
strings, functions, closures and recursion.

Three real defects were found by this suite and fixed:

1. `Number::toString` read the exponent with `std::atoi` past the `to_chars`
   output, which is not NUL-terminated, producing results that depended on stale
   stack bytes. The exponent is now parsed strictly inside the formatted range.
2. `while` evaluated its body before its test on the first iteration, so a loop
   with a false test still executed once. `while` now always tests first.
3. `break` and `continue` replaced a block's accumulated completion value with
   undefined. Both V8 and QuickJS preserve it (`while (true) { 1; break; }`
   evaluates to `1`), so blocks now merge their accumulated value into break and
   continue completions and loops absorb them.

A fourth divergence was a deliberate spec correction: `Number("-0x10")` is NaN
because `NonDecimalIntegerLiteral` carries no sign.

## ASan/UBSan

```sh
cmake -S dragscript -B /tmp/opencode/ds-asan \
  -DCMAKE_BUILD_TYPE=Debug -DDRAGSCRIPT_SANITIZERS=ON \
  -DDRAGSCRIPT_DIFFERENTIAL_TESTS=ON
cmake --build /tmp/opencode/ds-asan -j 4
ctest --test-dir /tmp/opencode/ds-asan --output-on-failure
```

Result: **8/8 passed**, with no sanitizer findings. Dragscript, the generated
grammars, MLVM Core/IR and the QuickJS oracle were instrumented; the externally
installed Tree-sitter shared library was not rebuilt.

## MLVM dependency regression suite

```sh
cmake -S mlvm -B /tmp/opencode/dragscript-mlvm-check \
  -DCMAKE_BUILD_TYPE=Debug -DMLVM_BUILD_TOOLS=OFF -DMLVM_BUILD_TESTS=ON \
  -DMLVM_ENABLE_CEPHYR=OFF -DMLVM_ENABLE_BINUTILS=OFF -DMLVM_ENABLE_OPTILISP=OFF
cmake --build /tmp/opencode/dragscript-mlvm-check -j 4
ctest --test-dir /tmp/opencode/dragscript-mlvm-check --output-on-failure
```

Result: **9/9 passed** (smoke, core, IR, passes, conversion, object, C API and
the two Blackstone catalog tests). Optional Cephyr, library-backed binutils,
Optilisp execution and the MLVM CLI suites were not configured.

## External embedding

A Release CMake consumer under `/tmp/opencode/dragscript-consumer/` uses
`add_subdirectory(dragscript)`, `DRAGSCRIPT_BUILD_TESTS=OFF`, includes only
public headers, and links only `Dragscript::Core`. It compiles a typed page
script, inspects both debug dumps, registers a fake `log` capability and runs the
script under both backends, printing `page-ready:6` twice and returning `12`.
Only the required MLVM Core/IR and grammar dependencies were compiled.

## Recorded gaps

- The Tree-sitter checkout lacks `lib/`; only the installed-runtime fallback was
  exercised here. The vendored-runtime build branch was not exercised.
- The pinned grammar misparses `(-3) < (-3)` in TS and TSX; it is a documented
  compile-time rejection with a regression test.
- No actual Dragworks checkout/interface, DOM, networking, native execution,
  general conformance suite, coverage-guided fuzzer or performance benchmark was
  run. Ninja is unavailable in this environment, so the Makefile generator was
  used throughout.
- An incremental (stale build directory) run produced misleading failures early
  in this regiment; all reported results come from clean or fully rebuilt trees.