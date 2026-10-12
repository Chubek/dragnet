# 36. Cephyr preprocessing and parsing

[Contents](README.md) · [Next: Cephyr code generation](37-cephyr-codegen.md)

Cephyr is an optional C frontend using the supplied Tree-sitter C grammar.
Configure with `MLVM_ENABLE_CEPHYR=ON` to require its dependencies. The selected
language mode is C17 freestanding, with no implicit hosted headers or libc.

```sh
build/cephyr -E examples/answer.c
build/cephyr --parse-only examples/answer.c
build/cephyr -fsyntax-only examples/answer.c
```

These commands have different purposes: `-E` preprocesses, `--parse-only` checks
the parser grammar, and `-fsyntax-only` additionally validates the implemented
semantic subset. Parser acceptance is not a conformance or code-generation claim.

## Preprocessor subset

Supported behavior includes object-like macros, recursive-expansion suppression,
escaped-newline splicing, comments and quoted tokens, `#undef`, quoted/angle
includes, `#ifdef`/`#ifndef`, `#if 0/1`, `#else`/`#endif`, and `#error`.
CLI options include `-D`, `-U`, `-I`, and `-include`.

Predefined values are `__STDC__=1`, `__STDC_VERSION__=201710L`, and
`__STDC_HOSTED__=0`. Function-like macros, stringification/token pasting, general
conditional expressions, `#elif`, `#line`, and pragmas are rejected.

## Source locations and limits

Line breaks are retained within one source, but include expansion and splicing
shift coordinates. Parser/lowering diagnostics refer to a separate
`<preprocessed>` buffer; exact original macro/include provenance is pending.

Expansion is bounded at depth 128 and 16 MiB per line. Includes are bounded at
depth 64 and total output at 64 MiB. Include cycles are diagnosed, including
recursion that would otherwise rely on guard handling. Avoid assuming a system
header set will work simply because the parser accepts much of its syntax.

The frontend is separately linkable as `MLVM::Cephyr`; clients do not need to
invoke a subprocess compiler. The build uses the checked-in grammar without
regeneration.

Reference: [Cephyr feature matrix](../docs/CEPHYR.md), [frontend header](../cephyr/include/cephyr/frontend.hpp).
