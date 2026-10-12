# AGENTS.md -- Limestone

Limestone is the late-stage compiler framework in the CCWeave stack. It consumes machine-independent program representations and progressively lowers them toward machine-oriented code through instruction selection, scheduling, register allocation, optimization, binary translation, and machine-code-facing representations.

## Architecture

The intended pipeline is:

```text
Machine-independent IR
        |
        +--> Unisel       universal instruction selection
        |        |
        |        +--> Limeburg  BURS/tree selection
        |        v
        +--> Schedrow     scheduling IR/model
        |
        +--> RegTL        register-allocation IR
        |
        +--> Tunah        equality-saturation optimization
        |
        +--> Bin2Bin      binary lifting/translation/rewrite
        |
        +--> TraceML      metatracing frontend
        |
        v
     MachineIR / target backend
```

Metacode is the horizontal machine-information layer. The Infobank `.isa` descriptions are authoritative input; consumers must not silently invent target properties that are absent from them.

## Component boundaries

- **Metacode:** parses and normalizes target/ISA metadata.
- **MachineIR:** architecture-neutral machine-level IR, implemented in D under `metacode/machine-ir`.
- **Unisel:** global/constraint-oriented instruction selection. Solver dependencies remain behind an adapter.
- **Limeburg:** BURS-based tree instruction selection.
- **Schedrow:** scheduling representation and scheduling algorithms; preserve semantic dependencies separately from scheduler-only dependencies.
- **RegTL:** register-allocation representation and allocators; physical register assignment happens here or in a later allocator.
- **Tunah:** equality saturation; rewrites must be semantically justified and deterministic.
- **Bin2Bin:** decoding, lifting, rewriting, translation, caching, and binary-analysis infrastructure.
- **TraceML:** metatracing language frontend and lowering pipeline.
- **Exolayer:** C ABI/FFI boundary. Public symbols use the `exl_` prefix.
- **VMWeave:** Lua VM-description DSL; generated runtimes may consume Limestone facilities.

## Dependency rules

Prefer this direction:

```text
Metacode -> ILs -> algorithms -> orchestration -> MachineIR
```

Do not introduce reverse dependencies merely for convenience. Do not make solver, parser, allocator, scheduler, or dynamic-loader implementation types part of unrelated public APIs.

Third-party implementations named by a component's `AGENTS.md` are integration boundaries. If they are absent from a checkout, provide an internal fallback or a clearly isolated adapter rather than silently changing the public model.

## Build

The repository uses CMake and C++20 for the C/C++ components. `metacode/machine-ir` is a D package and is built/tested with D's package tooling when available.

A normal validation sequence is:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Do not report a D test as passing when no D compiler/package manager is installed.

## General coding rules

- Keep APIs deterministic.
- Prefer explicit ownership and RAII in C++.
- Keep C APIs opaque and ABI-stable.
- Preserve provenance and source locations when transforming machine metadata.
- Separate semantics from cost, scheduling, allocation, and encoding information.
- Reject malformed or semantically unsupported input rather than silently degrading it.
- Use stable identifiers instead of pointer addresses for externally visible ordering.
- Add focused positive and negative tests for new behavior.
- Do not introduce LLVM/MLIR/QBE or an unrelated parser/solver framework unless explicitly required.

## Generated artifacts

Grammar sources and generated parser sources must retain a clear source-of-truth relationship. Do not hand-edit generated output when a generator is available.

## Definition of done

A component is complete when its core model, public API, implementation, diagnostics, tests, and build integration are present, and its behavior respects the local `AGENTS.md`. If an external dependency is unavailable, the limitation must be documented explicitly rather than hidden.
