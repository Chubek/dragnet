# Chapter 22. Testing, Diagnostics, and Development

[Previous: Targets and adapters](21-targets-backends-and-adapters.md) · [Manual contents](README.md)

## 22.1 Verify the boundary that owns the invariant

Limestone's correctness is distributed across representations. A parser establishes
syntax; a semantic loader establishes valid declarations; selection establishes
coverage; scheduling establishes availability/resources; allocation establishes
storage legality; encoding establishes operand representation; a runtime establishes
executable ownership and validity.

The most effective debugging strategy finds the first boundary where a program
becomes incorrect. Keep owning snapshots of the input and intermediate results,
run the applicable verifier, and compare with an independent semantic model.
Investigating final bytes first can hide a much simpler wrong graph edge or
missing source effect.

The top-level [AGENTS.md](../AGENTS.md) and each component's local instructions
describe architectural responsibilities. Public headers, source behavior,
grammars, and tests establish the current concrete contract. Architectural goals
should not be presented as already completed implementation.

## 22.2 Validation commands and configuration identity

The normal sequence is:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure -j 4
```

Record compiler versions, feature options, available D/Lua/Python tools, and test
registration. A passing test count belongs to that configuration. Optional tests
that were never registered have not passed.

```sh
ctest --test-dir build -N
ctest --test-dir build -R 'metadata|parsers|syngen' --output-on-failure
ctest --test-dir build -R 'selection|operand-constraints|limeburg-specs' \
  --output-on-failure
ctest --test-dir build -R 'scheduling|allocation|backend|contracts' \
  --output-on-failure
```

Use focused tests while developing the affected boundary, then the connected
integration path. For a cross-cutting public representation or target change,
run the normal suite after focused checks succeed. Keep concurrent D package
tests from sharing the same source working directory.

## 22.3 The repository's test map

| Tests or sources | What they establish |
| --- | --- |
| `metadata`, `tests/metadata.cpp` | Infobank ingestion, provenance, string/numeric bounds |
| `parsers`, `syngen` | Grammar/schema fidelity and reproducible AST generation |
| `text-il`, `text-il-cli` | Semantic IL loading, canonical printing, CLI handoffs |
| `selection`, `operand-constraints` | Matching, global/BURS legality, typed operands |
| `limeburg-specs*` | Corpus provenance, coverage, generation freshness, examples |
| `scheduling` | Hazards, timing domains, resources, groups, CFG/modulo verification |
| `allocation` | Liveness/interference, constraints, all allocation strategies |
| `optimization`, `tunah-tuners`, `tunah-cli` | Rule loading, sound corpus behavior, bounds/costs |
| `traceml`, `pipeline`, `contracts` | Source semantics, stage invariants, adapter errors |
| `backend`, `native-pipeline` | Independent execution of encoded graph/native slices |
| `binary` | Codecs, lifting, translation, cache/runtime behavior |
| `objects*`, `object-c-api` | ELF, symbols, relocations, native/system-linker integration |
| `machineir-bridge`, `machineir-exchange`, `machine-ir` | C++/D envelopes, analyses, language round trips |
| `interop`, `exolayer-data`, `exolayer-manifest` | Callback/native ABI, ownership, aggregates, exports |
| `c-api`, `optimization-c-api`, `c-abi-symbols` | Public C ownership, diagnostics, ABI symbol consistency |
| `vmweave` | DSL validation, generated C compilation/dispatch/hooks |
| `python-bindings`, `python-optimization-adapter`, `installed-consumer` | Binding ownership and relocated external package use |

Some entries depend on tools/platforms. Consult CMake registration for the actual
configuration rather than assuming every name is present.

## 22.4 Reading error categories

| Category | Typical interpretation |
| --- | --- |
| Invalid argument | Malformed structure, field shape, handle, or operand |
| Parse | Invalid source syntax or token encoding |
| Not found | Missing named entity, symbol, or required reference |
| Conflict | Duplicate declarations, incompatible state, or forbidden active operation |
| Unsupported | A semantic/backend contract needs an adapter |
| Unsatisfiable | No legal selection/allocation under the supplied complete problem |
| Timeout / interrupted | Explicit time/cancellation outcome where the API reports it |
| Resource limit | A bounded input/search/extraction cannot continue within its limits |
| Internal | A framework or integration invariant failed unexpectedly |

The error message adds source, target, instruction, or active-stage context where
available. Do not replace an explicit callback status with a generic internal
error. Conversely, do not convert a search limit into an unsatisfiability proof.

Tunah can stop at a budget and successfully extract an equivalent term with
`limit_reached=true`. That result is different from a failed operation. Runtime
handles similarly distinguish owned bytes, compiled state, and validity.

## 22.5 Selection diagnosis

For Unisel, inspect prepared graph, candidates, signed clauses, boundary inputs
and outputs, and selected matches. An empty coverage clause identifies an uncovered
required node. Nonempty local candidates with no global solution suggest overlap,
hidden shared values, dependency legality, or contradictory constraints.

For Limeburg, inspect node/nonterminal states and rule attempts. Work inward from
the failing root to the first missing child category. Check concrete types,
immediate ranges, predicates, repeated bindings, classes, and effect capability.
An improvement flag means a state update occurred at that time; it does not
identify the final root derivation.

Preserve target source origins. A failed generated `ISA_*` rule may indicate a
missing source adapter contract, while a generic `selection_tree` failure may
indicate illegal operands. Lowering a cost does not solve either problem.

## 22.6 Scheduling and allocation diagnosis

For scheduling, compare explicit and augmented dependencies, result-latency domains,
resource capacities/reservations, alternative choices, slots, groups, and issue
vector. Check zero-latency sequential order as well as cycle inequalities. CFG
cycles are block-local; sorting all issues globally by cycle can break layout.

Memory diagnosis needs read/write, atomicity, ordering, alias sets, and address
spaces. Even relaxed atomic read/read operations preserve coherence when aliases
are unknown. Explicit disjoint alias sets or spaces can establish independence.
Preserve that distinction in list, modulo, and sequential verification.

For allocation, compare block liveness, lifetime segments, exact interference,
classes, aliases, reservations, ties, early definitions, fixed/allowed/forbidden
choices, and clobbers. An allocator cannot fix missing source liveness.

After spills, inspect original decisions separately from final temporaries,
scratch assignment, slots, frame size, transfer placement, physical hazards, and
final order. Protected units require joint scratch feasibility. Control/boundary
transfers require their own adapter.

## 22.7 Binary, object, and runtime diagnosis

For a decode error, establish codec/domain, form boundaries, fixed masks, field
coverage, endianness, and complete input bytes. For a translation error, inspect
status, state model, instantiated semantics, target equivalence, addresses,
control boundaries, and relaxation choices.

For object errors, inspect format identity, section ranges/alignment, symbol
binding/scope, explicit addends, relocation types/fields, scale, and actual link
base. A valid object containing an unknown relocation can round-trip while still
being un-linkable under the current target.

For runtime errors, inspect guest bytes/addresses, transform identity/cacheability,
observation heat, region validity, compiled state, installer outcome, and active
ownership. A stale region retains diagnostic bytes but must not be invoked.
Failed installation still adopts its returned executable record for cleanup.

Keep deterministic binary facts distinct from supplemental decompiler analysis.
An LLM-assisted interpretation, if added through a plugin, cannot establish or
override the authoritative ISA semantics or verified control/dataflow.

## 22.8 Independent semantic execution

Round trips check that two readers/writers agree. They can both share the same
mistake. Independent execution gives a stronger check that the selected and
encoded program preserves observable source behavior.

The backend tests interpret emitted register-machine bytes independently,
covering branches, fused memory, shared values, and spills. Native tests invoke
bounded functions under declared ABIs. Object tests compare addressed linking
with system-linker execution. C++ → D → C++ exchange tests edit admitted values,
re-encode, and execute the returned program.

For a new operation, choose semantic boundary inputs: signed extrema, immediate
limits, zero/negative scales where rejected, aliasing memory, branch targets,
call clobbers, and scarce storage. Include a negative case that demonstrates why
an illegal input cannot be admitted. Assertions should describe meaningful
semantics or invariants rather than merely copying implementation choices.

## 22.9 Ownership and callback regression cases

Exercise destruction after snapshot creation, results outliving documents,
independent copied strings/bytes, failed registration, failed installers,
self-invalidation, reentrant operations, exactly-once release, and shared-library
close during an active call.

Ownership transfer is interface-specific. Optimizer userdata is adopted only
after successful registration. Runtime executable records are adopted on every
installer outcome. Exolayer callback userdata remains host-owned. Native type
parents/registrations copy immutable child descriptions.

Check caller output buffers and scalar outputs on failure. Data-call results and
many C inspection/build outputs are transactional. Native side effects and host
callbacks are not rolled back by those result-publication guarantees.

## 22.10 Resource-budget interpretation

| Boundary | Default or important limit |
| --- | --- |
| Generated parser ingress | 16 MiB, one million tree nodes, depth 512 |
| UMD/BURS include graph | 16 MiB cumulative, 128 occurrences, 32 levels |
| Tunah term input | 4 MiB, nesting depth 256 |
| Tunah default run | 20 iterations, 10,000 nodes/classes |
| MachineIR exchange | 4 MiB, depth 256 |
| Object model | 64 MiB, 16,384 sections, 1,048,576 symbols/relocations |
| Exolayer expanded native type | 1,024 nodes, depth 32, 1 MiB |
| Exolayer native arguments | 32 |
| TraceML execution | 100,000 steps/events by default |
| Runtime observation | Hot threshold 10, residency 1,024 by default |

These limits apply at different stages. Parser tree bounds are checked after
DParser finishes its tree, so they do not preempt GLR engine work. Tunah time and
cancellation checks are cooperative, not an interruption mechanism inside vendor
operations or host analyses. Document the actual bounded operation when adding
a new limit.

## 22.11 Reproducibility and performance

Use stable IDs, sorted map output, deterministic tie policies, explicit block
layout, and owning configuration snapshots. Avoid pointer-address identities in
visible ordering and cache keys. Record rule/metadata/context versions and
source/target addresses with binary results.

Time-dependent optimization can produce different valid equivalents. Its
cacheability policy must acknowledge that behavior. Deterministic fixed-budget
tests are better reproducibility baselines than wall-clock-dependent runs.

Measure preparation, matching/model generation, solving, scheduling, allocation,
materialization, encoding, cache behavior, and total latency separately. Also
measure final code quality under the target model. A faster selector that increases
spills can worsen total performance. Tunah's existing benchmark notes distinguish
semantic validation from optimizer throughput measurements.

## 22.12 Generated-source and contribution workflow

When changing syntax, update `.g` and `.absyn`, regenerate, and test both syntax
and semantic loaders. When changing Infobank-derived rules, update authoritative
metadata or the adapter, regenerate, and run freshness checks:

```sh
cmake --build build --target limeburg-regenerate-specs
build/limeburg-generate-specs --check metacode/infobank limeburg/specs
ctest --test-dir build -R 'syngen|parsers|limeburg-specs' --output-on-failure
```

Review all constructors, printers, verifiers, adapters, consumers, public headers,
and bindings when changing a representation. Keep dependency direction explicit:
metadata → ILs → algorithms → orchestration/hand-off. External implementations
stay behind their defined adapters.

Add source/provenance diagnostics and meaningful positive/negative tests for new
behavior. Validate C and C++ consumers for public ABI changes, and validate an
installed consumer for export/layout changes. Documentation changes should check
links and run their complete examples against the actual APIs.

## 22.13 A useful failure report

A reproducible report includes the smallest source/target or binary/object input,
exact command/options, compiler and dependency versions, configuration flags,
stage/model listings, structured status, and expected observable behavior.
For cache/runtime problems, also include addresses, context identity,
invalidation sequence, and ownership timeline.

Identify the first failed verifier and any independent execution mismatch.
That gives a maintainer a concrete boundary to repair and a regression to retain.
The framework's long-term usefulness depends on these explicit contracts remaining
deterministic, inspectable, and semantically justified across every stage.

[Previous: Targets and adapters](21-targets-backends-and-adapters.md) · [Manual contents](README.md)
