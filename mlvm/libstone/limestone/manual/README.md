# The Limestone Manual

Limestone is CCWeave's late-stage compiler framework: a collection of connected
representations, algorithms, metadata readers, and embedding interfaces for
lowering computations toward machine code. This manual explains both the complete
pipeline and the individual components that can be used independently.

The chapters describe the implementation in this repository. Public headers,
grammars, authoritative Infobank descriptions, and executable tests are linked
throughout. Architectural extension points are explained alongside the contracts
an adapter must supply. An instruction inventory, a selectable instruction, a
binary encoding, and an executable ABI are different levels of support; the manual
keeps those distinctions explicit.

## Contents

| Chapter | Subject |
| --- | --- |
| [1. Framework and architecture](01-framework-and-architecture.md) | Component responsibilities, pipeline contracts, representations, ownership, and terminology |
| [2. Building, installing, and validating](02-building-installing-and-validating.md) | Dependencies, configuration, generated parsers, installation, CMake consumers, and validation configurations |
| [3. First programs and workflows](03-first-programs-and-workflows.md) | Evaluation, portable compilation, UMD compilation, BURS, optimization, binary translation, and native objects |
| [4. Metacode and the Infobank](04-metacode-and-the-infobank.md) | ISA syntax, source-located metadata, registers, semantics, schemas, and capability interpretation |
| [5. Program graphs and UMD](05-program-graphs-and-umd.md) | SSA identities, patterns, types, CFGs, effects, includes, and preparation |
| [6. Unisel global instruction selection](06-unisel-global-instruction-selection.md) | Candidates, coverage clauses, Satie solving, greedy selection, and inspectable results |
| [7. Limeburg BURS and generated specifications](07-limeburg-burs-and-generated-specifications.md) | Dynamic programming, rules, states, rejection traces, shared values, and Infobank-derived `.lburg` files |
| [8. Schedrow instruction scheduling](08-schedrow-instruction-scheduling.md) | Dependencies, latency domains, resources, issue slots, groups, CFG scheduling, and modulo scheduling |
| [9. RegTL allocation and spilling](09-regtl-allocation-and-spilling.md) | Liveness, interference, four allocators, fixed operands, transfers, scratch storage, and spill frames |
| [10. Tunah equality saturation](10-tunah-equality-saturation.md) | Rule semantics, predicates, costs, limits, graph reconstruction, and binary semantic adapters |
| [11. MachineIR and C++/D exchange](11-machineir-and-cpp-d-exchange.md) | Architecture-neutral D IR, analyses, versioned JSON, envelope preservation, and checked handoffs |
| [12. Bin2Bin decoding and translation](12-bin2bin-decoding-and-translation.md) | Codecs, statuses, lifting, semantic matching, CFG recovery, encoding, and branch relaxation |
| [13. Object files and linking](13-object-files-and-linking.md) | ELF64 ET_REL/RELA, sections, symbols, relocation contracts, addressed images, and compiler packaging |
| [14. Runtime translation and caching](14-runtime-translation-and-caching.md) | Observation heat, immutable regions, executable ownership, invalidation, reentrancy, and LMDB identity |
| [15. TraceML and metatracing](15-traceml-and-metatracing.md) | Source semantics, lexical lazy closures, checked primitives, execution events, guards, and target lowering |
| [16. Exolayer native interoperability](16-exolayer-native-interoperability.md) | Callback registries, native signatures, aggregate types, data calls, extensions, and ABI ownership |
| [17. VMWeave VM generation](17-vmweave-vm-generation.md) | Lua declarations, deterministic generated C, dispatch, hooks, and runtime integration |
| [18. C and C++ embedding](18-c-and-cpp-embedding.md) | Public libraries, structured errors, opaque handles, pipeline configuration, snapshots, and lifetime management |
| [19. Python bindings](19-python-bindings.md) | Building SWIG bindings, convenience handles, inspection, optimization, translation, runtime observation, and objects |
| [20. CLI and textual languages](20-cli-and-textual-languages.md) | Command modes, option compatibility, text formats, syntax trees, parser generation, and serialization |
| [21. Targets, backends, and adapters](21-targets-backends-and-adapters.md) | Building an explicit target, legal patterns, timing, allocation, encoding, ABI boundaries, and extension design |
| [22. Testing, diagnostics, and development](22-testing-diagnostics-and-development.md) | Verification layers, failure diagnosis, differential execution, reproducibility, resource budgets, and contribution workflow |

## Suggested reading paths

**First-time users:** read Chapters 1–3, then follow the chapter for the subsystem
you want to use. Chapter 20 is the command reference; it complements the tutorials
rather than replacing them.

**Compiler backend authors:** read Chapters 4–11 and 21. Keep Chapter 22 open while
adding target contracts and validating the generated code. Chapters 12 and 13
explain the encoding and object interfaces used at the end of the pipeline.

**VM and emulator authors:** read Chapters 12–17 and 18. These chapters explain
guest/host state boundaries, addressed translation, executable ownership, and
where the embedding runtime participates.

**C, C++, and Python embedders:** start with Chapter 18 or 19, then read the
component chapter behind the API you call. Ownership is part of the API contract,
including when a result can outlive its source handle.

**Contributors:** read Chapters 1, 20, 21, and 22, plus the local `AGENTS.md` for
the component you are changing. The implementation-status document provides a
compact companion to this longer manual.

## Conventions used in the chapters

- Shell commands run from the repository root unless another working directory is
  stated. `build/` means the CMake binary directory chosen for that example.
- Temporary examples use `/tmp/opencode`. Create it with `mkdir -p /tmp/opencode`
  before saving snippets or redirecting output there.
- Installed tools can be invoked without the `build/` prefix when their `bin`
  directory is on `PATH`.
- Installed chapters are in `share/doc/limestone/manual` with the default GNU
  layout. Links to source headers, grammars, and fixtures refer to the repository
  checkout; chapter-to-chapter links work in either copy.
- Complete snippets are usable examples. Smaller API fragments explicitly assume
  caller-supplied objects, callbacks, or target contracts.
- Fixture timing is a test model. It is not a measured timing claim about a
  production processor.
- `%` identities usually denote virtual values or instructions; `$` identities
  denote physical registers in register-oriented languages. Each grammar defines
  its own identity domain.
- C++ names are in `limestone` and its component namespaces. The public compiler
  C interface uses `limestone_`; Exolayer's public symbols use `exl_`.

## Companion references

- [Repository introduction](../README.md)
- [Implementation status and boundaries](../IMPLEMENTATION.md)
- [Top-level development instructions](../AGENTS.md)
- [Pipeline API overview](../limestone/README.md)
- [Infobank specification coverage](../limeburg/specs/coverage.json)
- [Public C++ pipeline header](../limestone/limestone.hpp)
- [Public compiler C header](../limestone/limestone.h)

Begin with [Chapter 1: Framework and architecture](01-framework-and-architecture.md).
