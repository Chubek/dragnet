# 8. Sources and diagnostics

[Contents](README.md) · [Next: SSA model](09-ssa-model.md)

Library functions report structured diagnostics rather than printing directly.
The caller decides whether to render them in a terminal, collect them in an IDE,
or associate them with a larger compilation request.

## Registering source text

`SourceManager` owns source names and contents. `add` returns a nonzero file ID;
`location` maps a byte offset to a source coordinate.

```cpp
mlvm::SourceManager sources;
mlvm::DiagnosticEngine diagnostics;
auto file = sources.add("sample.mlvm", "mlvm 1 { block() {} }");
auto start = sources.location(file, 0);
diagnostics.error("client.example", "An application diagnostic", start);
```

Lines and columns are one-based byte positions, not Unicode display columns.
The all-zero location denotes unknown or invalid provenance; EOF is a valid
location. Keep this distinction when adapting editor positions.

## Diagnostic contents

A `Diagnostic` contains severity, code, message, primary location, related
locations, and fix-it ranges. `DiagnosticEngine::diagnostics()` exposes the
accumulated values, while `has_error()` summarizes failure severity.

```cpp
for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << sources.format(diagnostic) << '\n';
}
```

This fragment requires `<iostream>`. Use `clear()` only when intentionally
starting a new diagnostic scope; preserve failed-transaction diagnostics long
enough for the caller to understand the failure.

## Provenance through transformations

Operations carry a `SourceLocation`. Builtin folding preserves locations, but
canonical text does not serialize provenance. Reparsing printed output creates
locations in the new textual source. Cephyr diagnostics after preprocessing can
refer to a separate `<preprocessed>` buffer rather than the original macro site.

Reference: [`SourceManager` and `DiagnosticEngine`](../include/mlvm/core.hpp).
