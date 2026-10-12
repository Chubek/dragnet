# AGENTS.md -- Limestone Core

This directory contains the top-level Limestone orchestration library and CLI. It is intentionally thin: the core coordinates the specialized Limestone subsystems rather than reimplementing their algorithms.

## Responsibilities

- Accept a machine-independent input representation.
- Construct and coordinate the late-stage pipeline.
- Expose stable C and C++ entry points.
- Preserve diagnostics and pipeline configuration.
- Lower/hand off to Unisel, Limeburg, Schedrow, RegTL, Tunah, Bin2Bin, and MachineIR as appropriate.

## Non-responsibilities

Do not place BURS tables, SAT/SMT constraints, register-allocation algorithms, scheduler resource models, e-graph internals, or ISA-specific facts in this directory. Those belong to their owning subsystem.

## API rules

The C ABI in `limestone.h` must remain usable from C and C++. Ownership is explicit: every opaque object returned by a create/compile operation has a corresponding destroy operation. C++ wrappers may provide RAII but must not change semantics.

The C++ API in `limestone.hpp` uses `limestone::` types and returns structured results. Recoverable errors must not terminate the process.

## Pipeline rules

The default conceptual order is:

```text
parse/ingest
  -> optimize
  -> instruction selection
  -> scheduling
  -> register allocation
  -> MachineIR
  -> final backend/encoding
```

Stages may be configured or skipped, but skipping a stage must not silently manufacture information normally supplied by that stage.

## Determinism

Pipeline output must not depend on pointer addresses, unordered-container iteration, or host thread scheduling. Stable IDs and explicit sorting are preferred for externally visible output.

## Tests

Changes to orchestration should have an end-to-end smoke test in addition to focused subsystem tests. Public ABI changes require both C and C++ compilation coverage.
