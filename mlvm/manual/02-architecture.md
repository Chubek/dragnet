# 2. Architecture and repository guide

[Contents](README.md) · [Next: Building](03-building.md)

The architectural progression is source representation, typed high-level IR,
mid-level SSA, low-level ABI operations, machine representation, and binary
artifacts. Dialects own semantics; conversions connect representations. A
conversion graph permits multiple routes rather than requiring one universal
lowering sequence.

## Source map

| Directory | Role |
| --- | --- |
| `include/mlvm/` | Implemented public core, IR, pass, object, and optimizer APIs |
| `src/core`, `src/ir`, `src/dialect` | Contexts, SSA, text, builtin operations |
| `src/pass`, `src/conversion` | Transformation infrastructure |
| `src/codegen`, `src/assembler`, `src/linker` | Baseline native toolchain |
| `src/binutils` | MLVM-owned Limestone adapters |
| `tools/` | Executables and Blackstone catalog tooling |
| `optilisp/` | Reader, loader, rewriting, and integration tests |
| `cephyr/` | Optional C frontend |
| `examples/`, `tests/` | Executable usage examples and regressions |
| `cmake/` | Dependency detection and build adapters |

Use the API contract and exported targets to identify supported entry points.
The presence of additional interface headers or target directories alone is not
evidence of an implemented executable feature.

## Dependency direction

The generic IR core is independent of any particular language or native target.
`MLVM::IR` builds on `MLVM::Core`; `MLVM::Object` builds on
`MLVM::Codegen`. Cephyr and Optilisp integrations add their own layers without
requiring executable tools as library dependencies.

`libstone/` is a read-only source dependency. MLVM-owned adapters isolate its
types and behavior. Public consumers should not have to include Limestone or
Blackstone private implementation headers to use an MLVM object or module.

When extending the system, first decide which representation owns the new
semantics. Then implement its verifier and transformation boundary. This avoids
putting language-specific rules into generic containers.

Reference: [architecture contract](../docs/ARCHITECTURE.md).
