# Chapter 6. Unisel Global Instruction Selection

[Previous: Graphs and UMD](05-program-graphs-and-umd.md) · [Contents](README.md) · [Next: Limeburg](07-limeburg-burs-and-generated-specifications.md)

## 6.1 Selection as a compatible graph cover

Unisel chooses target instructions over a machine-independent SSA/CFG graph.
Each instruction pattern may cover several source computations and expose
boundary inputs and outputs. Selection must choose a compatible collection that
implements every required operation, retains shared values, preserves effects,
and forms a legal emitted dependency graph.

This is why a collection of locally cheapest matches is not enough. A cheap
candidate can overlap another required candidate, hide a value needed elsewhere,
or create an illegal dependency after several computations are fused. The global
selector uses the supplied Satie substrate behind a private adapter to minimize
total scalar cost subject to the complete selection model.

The public API is [unisel/unisel.hpp](../unisel/unisel.hpp), exported by
`Limestone::unisel`. Its public candidate and clause model contains no Satie
implementation types.

## 6.2 The algorithm lifecycle

The independent workflow is:

```text
program + patterns
        |
  validate / prepare
        |
   candidate matching
        |
  coverage and compatibility model
        |
  global or greedy selection
        |
  Scheduler IR emission
```

A caller with a program and pattern vector can use:

```cpp
auto prepared = limestone::unisel::prepare(program);
if (!prepared) return report(prepared.error());

auto model = limestone::unisel::build_model(prepared.value(), patterns);
if (!model) return report(model.error());

auto selected = limestone::unisel::solve(prepared.value(), patterns);
if (!selected) return report(selected.error());

auto region = limestone::unisel::emit_scheduler(
    prepared.value(), patterns, selected.value());
```

This is an API fragment: `program`, `patterns`, and `report` are host-supplied.
Model construction is shown separately so its data can be inspected before
solving. `match` exposes candidate discovery, while `validate` checks the combined
source/pattern contract.

## 6.3 Candidate information

`Candidate` records:

| Field | Interpretation |
| --- | --- |
| `pattern` | Stable target pattern identity |
| `root` | Source root at which the pattern matched |
| `covered` | Required source computations implemented internally |
| `inputs` | Values read at the candidate boundary |
| `outputs` | Values that remain defined at its boundary |
| `cost` | Local scalar cost for this implementation |
| `reason` | Inspectable matching explanation |

A pattern matching `add(x,const(7))` can cover the addition and right constant
while reading `x` as a boundary. A general `add(x,y)` candidate covers only the
addition and reads both children. The candidates compete on total cover cost,
including the instructions needed to produce their boundary inputs.

Candidate discovery does not mutate the source graph. Competing alternatives
remain available until selection. Deterministic ordering uses stable roots,
costs, and pattern identities. Selected matches are later exposed in emission
order, which is a different useful ordering.

## 6.4 Hard constraints

The public `ConstraintModel` contains candidates and clauses of signed, one-based
candidate literals. A positive literal selects a candidate; a negative literal
excludes it. This inspectable layer captures selection legality without exposing
private objective or solver machinery.

The principal obligations are:

**Coverage.** Every required operation must have a selected implementation.
If candidates `1` and `4` cover a node, a clause such as `[1,4]` requires at least
one. An empty coverage clause is an explicit statement that the model cannot
cover that operation.

**Compatibility.** Candidates cannot overlap illegally. A clause `[-1,-2]`
prevents two incompatible candidates from being chosen together.

**Boundary definitions.** A selected consumer must receive values defined by
selected producers or legitimate external boundaries. Fusing a shared internal
node must not remove a value that another consumer or output still reads.

**Dependencies and placement.** Selected operations must preserve legal source
block placement, semantic/effect ordering, and acyclic emitted dependencies.
The scheduler receives a valid dependency model; it is not a repair mechanism
for an impossible selection.

These examples explain clause meaning rather than promising an exact clause
order for every implementation revision. Use the public model to inspect the
actual problem you supplied.

## 6.5 Typed and structured matching

Unisel matches normalized flat or structured pattern trees. Constraints can
require concrete types, source register classes, repeated identity bindings,
immediate ranges, and the shared declarative operand predicates.

Suppose an immediate addition is cheaper than a register addition. A legal
`ADDI` candidate requires proof of the immediate's range and scale. If the value
is unknown, no numeric predicate succeeds. If it has the wrong class or type,
the candidate is filtered before solving. The solver only optimizes over admitted
legal candidates.

Repeated `?x` bindings compare source identities. They do not require two virtual
values to receive the same physical register. Fixed and tied physical requirements
are carried to RegTL through target instruction contracts after selection.

Patterns capable of side effects must opt in explicitly. A memory, trap, or
control computation remains subject to its source contract even when a target
instruction name suggests an equivalent operation. Semantic equivalence is an
input to pattern construction, not something mnemonic matching proves.

## 6.6 Global cost selection

`solve` minimizes the sum of selected pattern costs under the model's legality
constraints. Costs are checked scalar integers supplied by the target. They can
represent instruction count or another documented target objective; Unisel does
not independently manufacture latency, code size, or energy weights.

The cost must be interpreted at the cover level. A fused instruction with local
cost `3` can be better than a local cost `2` instruction that needs three more
materializations. Conversely, a cheap fused form may be unavailable because its
internal value is shared.

Global here refers to compatible selection over the supplied graph and candidate
set. It does not imply joint optimization of allocation, resource scheduling,
ABI lowering, and binary layout. Those decisions use the emitted region and
separate downstream models.

The public API reports structured errors for malformed input and no-solution
cases. Do not treat resource exhaustion or an internal solver failure as a proof
that no target cover exists. Preserve the original error category when presenting
the diagnostic to an embedding application.

## 6.7 Greedy selection

`solve_greedy` is the explicit deterministic alternative. It uses the same
source/pattern semantics, but makes incremental candidate choices rather than
searching for the minimum-cost global cover.

Greedy selection is useful for fast experiments and comparisons. It may choose
a more expensive complete cover, and may fail even when a globally feasible cover
exists. This is an algorithm property, not permission to weaken verification.
Its successful result must still satisfy the emitted region's legality checks.

Compare strategies with the connected CLI:

```sh
build/limestone-cli --compile-umd --selector global \
  tests/fixtures/arithmetic-includes.umd
build/limestone-cli --compile-umd --selector greedy \
  tests/fixtures/arithmetic-includes.umd
```

Compare candidate sets, total selection cost, final allocation, and emitted bytes
when available. An opcode difference alone does not explain whether a strategy
improved the final program.

## 6.8 Emission into Scheduler IR

`emit_scheduler` turns the selection into operations with instruction identities,
definitions, uses, consumed immediates, origins, memory effects, control flow,
blocks, and dependencies. Source outputs remain visible boundaries.

The target instruction model then supplies explicit timing, resources, implicit
architectural state, result-latency overrides, ties, early definitions, and fixed
operand constraints. In the full pipeline this attachment happens even when
scheduling is disabled, because architectural effects and storage constraints are
still semantically relevant.

Selected instruction order is not yet an allocation result or binary layout.
Schedrow may choose legal issue assignments; RegTL may insert transfers; final
encoding uses the final order. Retain the source-node/provenance relationship
while following a selected instruction through these stages.

## 6.9 Owning C inspection

The standalone interface in [limestone/il.h](../limestone/il.h) links
`Limestone::il`:

```c
limestone_error error;
limestone_unisel_document *document =
    limestone_unisel_load_file("tests/fixtures/arithmetic-includes.umd", &error);
if (!document) return 1;

limestone_selection_model *model = limestone_unisel_analyze(document, &error);
limestone_unisel_document_destroy(document);
if (!model) return 1;

/* Inspect candidates and clauses here. The model owns the source snapshot. */
limestone_selection *selection =
    limestone_selection_run(model, LIMESTONE_SELECT_GLOBAL, &error);
limestone_selection_model_destroy(model);
if (!selection) return 1;

/* limestone_selection_text(selection) is a canonical Schedrow handoff. */
limestone_selection_destroy(selection);
```

This fragment belongs inside a C function with the IL header included. Models
and selections are independent owners. Candidate metadata strings are borrowed
from their model or selection. Copy them before destroying that owner if they
must remain in an application diagnostic record.

Analysis can succeed for a well-formed uncovered graph. Inspect its empty clause
and candidate data; solving returns `LIMESTONE_UNSATISFIABLE`. That separation
allows an editor or target-development tool to explain missing patterns without
pretending a valid selection exists.

Python exposes the same lifecycle through `UniselDocument`, `SelectionModel`,
and `Selection`, with copied candidate/clause properties. Chapter 19 gives
complete convenience-layer examples.

## 6.10 Diagnosing no solution

Work from the earliest failed obligation:

1. Validate node IDs, inputs, arities, types, outputs, CFG, and source effects.
2. Inspect whether the missing operation has any candidate.
3. Check immediate predicates, repeated bindings, and class constraints.
4. Inspect shared values that fused candidates would hide.
5. Inspect incompatible overlaps and source dependency paths.
6. Verify the target patterns actually declare the intended source semantics.
7. Compare global selection when a greedy attempt failed.

Preserve the original graph, patterns, prepared dependencies, and model listing
as a reproducible case. The most informative regression normally asserts the
expected candidates or constraints and then checks the emitted observable
behavior. Chapter 7 presents the complementary BURS selector and its different
state-based explanation model.

[Previous: Graphs and UMD](05-program-graphs-and-umd.md) · [Contents](README.md) · [Next: Limeburg](07-limeburg-burs-and-generated-specifications.md)
