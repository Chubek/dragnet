# API contracts

## Shared IR model

`NodeId`, `BlockId`, and `ValueId` are `std::size_t`; `invalid_id` is the maximum
value. IDs belong to their owning representation. `DirectedGraph` IDs are
append-only; removal leaves a tombstone. Container references are borrowed and
may be invalidated by mutation. Copying an object copies its storage.

`Type` describes integers, floats, pointers, memory state, and function values.
Integer and floating types require positive widths; boolean is `integer(1)`.
Pointer names distinguish pointee descriptions; memory is a single state type.
`Operand` represents a register/value reference, signed 64-bit literal, double
literal, or named symbol. Literal types are explicit. No implicit conversion or
target-specific arithmetic semantics are introduced.

`Instruction` contains an opcode, optional result, result type, operands, branch
targets, phi predecessor labels, and an annotation. Calls use the annotation for
callee metadata. `verify_instruction` checks structural and scalar type
contracts. Memory operations require pointer addresses; calls carry caller-defined
signatures. `Diagnostics` contains stable codes, messages, node IDs and instruction
positions. Empty diagnostics indicate success.

Construction throws `std::invalid_argument` for rejected input, `std::out_of_range`
for invalid IDs, and `std::logic_error` for invalid construction order. Verifiers
return diagnostics for representable malformed programs. Derived analyses reject
invalid input before constructing their results.

## CFG and SSA

Create CFG blocks before referring to branch targets. The first block becomes the
entry; `set_entry` overrides it. `append` adds non-terminators. `jump`, `branch`,
`return_value`, or `terminate` close a block and create its control edges. A closed
block cannot accept additional instructions. Verification requires every block
to terminate, including unreachable blocks.

```cpp
firestone::ir::SimpleSSAGraph ssa;
auto entry = ssa.cfg().create_block("entry");
auto x = ssa.parameter(firestone::ir::Type::integer());
auto sum = ssa.define(entry, firestone::ir::Opcode::Add,
    firestone::ir::Type::integer(),
    {firestone::ir::Operand::ref(x), firestone::ir::Operand::constant(1)});
ssa.cfg().return_value(entry, firestone::ir::Operand::ref(sum));
auto errors = ssa.verify();
```

SSA parameters are globally available. Every other value must have exactly one
definition. Use `reserve_value` followed by `define_reserved` or `phi_reserved`
for forward references in loops. Phis precede all non-phi instructions and label
every distinct predecessor exactly once. A phi use occurs on its incoming edge;
ordinary uses occur at the instruction position. Verification checks both block
dominance and same-block instruction order. Unreachable blocks have only
self-dominance, so cross-block unreachable uses are rejected.

Postdominance uses a synthetic exit for multiple returns and is conditioned on
paths reaching an exit. Non-exiting SCCs have self-postdominance only.
Control dependence follows immediate postdominators. Graph queries and
algorithm tie-breaking use stable IDs for deterministic ordering.

`compute_liveness` excludes phi operands from successor-local use sets and adds
them on predecessor edges. `InterferenceGraph::from_ssa` walks instructions
backward and accounts for simultaneous entry parameters. `color` applies
degree-ordered greedy coloring; failure requests a spill or a larger palette and
does not establish that no coloring exists.

## Dependence and memory graphs

Data-dependence nodes retain block/instruction positions. Parameters have
`invalid_id` positions. Edge ports identify operand positions. A slice includes
its root. Program dependence adds edges from controlling block terminators to
instructions in dependent blocks. Memory dependence is represented separately
by `MemorySSAGraph`.

MemorySSA conservatively treats all stores and calls as clobbering the single
memory partition; loads use the current version. Non-entry blocks with
predecessors receive explicit memory phis, including single-predecessor blocks.
Cycles are allowed. `reaching_definitions` traverses phis with cycle detection and
returns stores/calls or live-on-entry. The CFG entry must have no predecessors.
No alias precision or dead-store conclusion is inferred from this model.

## Other representations

- 3AC checks register availability in textual order, not path-sensitive definite
  assignment. Registers may be reassigned. Branch targets name instruction indices.
- Triplets use earlier instruction positions as implicit results; quadruplets
  use explicit registers. Both admit at most two operands.
- ANF uses ordered, unique bindings and one final atomic result. Operations with
  explicit effects can occur as bindings; control transfers cannot.
- DataflowGraph admits pure typed operations only, with producer-to-consumer
  edges. Its scheduling order is topological.
- CPS continuations have independent parameter/binding scopes. Values cross
  continuation boundaries through typed tail arguments. Stores and void calls
  use `effect`; value-producing operations use `bind`. Tails are halt, jump, or
  boolean branch, and all continuations require a tail.
- Sea-of-nodes values and control inputs are distinct. Effectful operations
  consume one control input and produce control. Branches produce two control
  projections, indexed zero/one; regions merge control. Phi input counts match
  region predecessor counts. `set_inputs` supports cyclic loop construction;
  `verify` checks shape and types, not global scheduling or control dominance.
- RVSDG ports stay within their region. Structural nodes explicitly transfer
  values through subregion arguments/results. Gamma has two branches with
  matching output signatures. Theta body results are `(boolean, next-state...)`;
  a true predicate repeats and a false predicate exits. Lambda body results
  match the declared return signature. Stateful simple nodes end their input
  and output signatures with `Type::memory()`. Load is
  `(pointer, memory) -> (value, memory)`; store is
  `(pointer, value, memory) -> memory`. Literal nodes use `constant`.

## Rewriting

Terms are operator strings with ordered child terms. The parser accepts atoms,
parenthesized applications, and quoted atoms with quote, backslash, newline and
tab escapes. Parsing rejects empty atoms, unclosed terms and trailing input.

Pattern variables are named `?variable` leaves. Repeated occurrences must match
equal terms. Rule construction rejects RHS variables absent from the LHS.
Guards receive complete substitutions. `Rewriter` performs bottom-up passes,
trying rules in insertion order and applying at most one rule per visited node
per pass. Identity replacements do not count as applications. Results include
the term, stop reason, pass/application counts, and applied rule names.
An optional `RewriteOrder::TopDown` processes a node before its replacement's
children. Both orders apply at most one rule to each visited node per pass.
Failed `match_into` calls preserve the caller's substitution.

## Equality saturation

E-nodes contain an opaque operator and child e-class IDs. `add` hash-conses
canonical children; `merge` records an equality. Call `rebuild` after merges
before inspecting nodes. Rebuilding repeatedly canonicalizes and merges
congruent parents to a fixed point. E-class IDs remain valid after merges;
`find` returns the current representative. `equivalent`, `search`, and `extract`
rebuild automatically.

E-matching considers all operator alternatives in a class, checks repeated
variables by canonical class identity, and deduplicates substitutions. Patterns
remain finite even when the e-graph contains cycles. Guards inspect a rebuilt
graph and receive canonical substitution IDs.

`Saturator::run` enforces iteration, monotonic node-allocation, per-rule match,
and total application budgets. Application counts include accepted redundant
equalities. Stops distinguish saturation from each exhausted budget. Saturation
means a complete pass made no node or equality change. Budget exits return a
rebuilt, usable graph; a node-budget exit can retain partial RHS construction.

Extraction minimizes the sum of finite, nonnegative local e-node costs. The
default cost is one per tree node. Equal costs prefer smaller trees, then stable
operator/child ordering, preventing zero-cost cycle selection. Extraction returns
a finite tree, duplicating shared subexpressions as necessary. Rules and costs
are caller-controlled; operators carry no implicit arithmetic or effect semantics.

Mutable handles require external synchronization when shared between threads.

## CFG algorithms and transformation pipelines

`DirectedGraph::shortest_path` returns a deterministic unweighted BFS path,
including both endpoints; an empty path means the destination is unreachable.
`reverse_postorder` uses an iterative DFS and visits only nodes reachable from
the supplied root. Both accept an optional edge-kind filter. CFG convenience
methods expose reverse postorder and unreachable blocks.

`DominatorTree` owns a snapshot of dominance or postdominance sets. It provides
parents, children, depth, and nearest common dominators. Unreachable blocks and
independent postdominator roots form separate trees; a common dominator across
trees is absent. Construct a new snapshot after changing the CFG.

`natural_loops` groups dominance backedges by header and combines their latches,
body blocks, exit edges, and nearest enclosing loop header. Unreachable blocks
are excluded. `irreducible_regions` reports maximal cyclic SCCs having multiple
reachable entry blocks; it is an SCC-level report, not a decomposition of every
possible nested irreducible subregion.

`solve_dataflow<State>` takes an initial lattice state, meet callback, transfer
callback, explicit boundary states, flow direction, and evaluation budget. In
forward mode, transfer maps block input to output; in backward mode it maps
output to input. Boundary states replace the meet at their block. Only
entry-reachable blocks participate. The state type supports equality and copying.
Callers supply monotone lattice operations. A budget exit preserves partial
states with `converged == false`.

`PassManager::add` registers a named `bool(SimpleSSAGraph&)` transformation.
`run` validates the initial program, snapshots each pass, and validates its
result. An invalid result restores that snapshot and stops subsequent passes;
reports retain the diagnostics and commit decision. A throwing pass also rolls
back and then propagates the exception. Earlier committed passes are retained.

## CPS tools (`firestone::cps`)

`CPSGraph` sets its entry to the first continuation; `set_entry` overrides it.
The continuation accessor, value count, and binding annotations support tooling.
Tail operands are checked for scalar/literal validity as well as local scope.

`signature` extracts parameter types. `check_transfer` validates argument count,
types, literals, and availability in the source continuation's local scope.
`CPSBuilder` assigns unique nonempty names and checks jump/branch transfer
signatures before construction. `finish` validates and returns an owning graph
copy. `graph()` exposes low-level binding, effect, halt, and entry construction.

`analyze_liveness` propagates demands through tail arguments to a fixed point,
including continuation cycles. Its `required_parameters` sets identify demanded
parameters; `dead_bindings` identifies binding indices whose results are unused.
Loads, stores, and calls are always retained as potentially effectful operations.
Every continuation is analyzed, including those unreachable from the entry.

`lower_to_ssa` returns an SSA graph and original-to-lowered continuation/value
maps. Entry parameters become global SSA parameters; other parameters become
phis. Each tail transfer gets a forwarding block so two branch arms targeting
one continuation can carry different values. Local binding order and annotations
are preserved. The entry must have no incoming transfers; nonentry parameters
require incoming transfers. The result is verified, including SSA dominance;
unreachable cross-continuation uses that fail SSA's unreachable-block rules are
rejected.

## Memory (`firestone::memory`)

`Allocation` owns raw aligned bytes, is move-only, and uses matching aligned
new/delete. A zero-size allocation has null storage. `align_up` checks
power-of-two alignment and size overflow. `Arena` grows in independently owned
blocks; allocated addresses stay stable until reset/destruction. Neither raw
allocator constructs objects or runs their destructors.

`Pool` has a fixed capacity, a rounded block stride, and deterministic reuse.
Exhaustion throws `std::bad_alloc`. Deallocation rejects foreign pointers,
interior pointers, and double frees. `owns` describes block-start membership,
including currently free blocks. As with raw allocators, a reused address does
not encode an allocation generation.

`ArenaResource` and `PoolResource` adapt those owners to `std::pmr` containers.
The owner must outlive the resource and its clients. Arena deallocation is a
no-op; resetting the arena invalidates every client allocation. Pool requests
larger than its stride or alignment fail rather than allocating elsewhere.

`Buffer` is an owning, deep-copyable byte sequence with checked `at`, reserve,
resize, and append. Growth zero-initializes newly visible bytes. Append snapshots
its input, so self-append remains valid across reallocation. Moves leave the
source empty. Borrowed byte pointers can be invalidated by growth.

`RefCounted` has atomic retain/release and a virtual destructor. Construct clients
with `make_intrusive<T>` or `IntrusivePtr<T>(raw_pointer)`, which retains the
pointer. Copies retain, moves transfer, and the final release deletes the object.
Reference counts protect lifetime; mutable payloads still need synchronization.
Cycles require an explicit ownership break.

`GC<T>` is an explicit tracing heap with append-only handles and owned payloads.
Declare outgoing edges via `allocate(value, references)` or `set_references`;
handles cannot refer to reclaimed slots. `collect(roots)` traverses those edges
and pinned handles, preserving reachable cycles and reclaiming unreachable ones.
Collection destroys reclaimed payloads and returns retained/reclaimed counts.
Invalid roots are rejected before sweeping. References borrowed from the heap
can be invalidated by allocation or collection. Edges embedded in `T` are not
automatically traced.

## Type systems (`firestone::type`)

`Atomic` aliases the shared `ir::Type`, with validity and display helpers.
Structural `type::Type` represents atomic, pointer, array, record, function, and
variable types. Function `parts` are `(result, arguments...)`; record field names
correspond to `parts` in declaration order. Records are structural, including
their optional name. `validate` rejects malformed shapes and duplicate fields.
Void is accepted only in function results and as a pointer pointee. Type trees
are finite; recursive type references must be modeled externally.

`layout` returns size, alignment, and record field offsets. `LayoutPolicy`
controls pointer size/alignment and maximum scalar alignment (default 8 bytes).
Scalars occupy enough bytes for their bit width, rounded to natural alignment;
arrays use padded element strides, and records include trailing padding. Memory
state and unresolved type variables have no storage layout. Function values
have pointer storage. All size arithmetic is checked. These are explicit
compiler-model layouts, not an assertion about any platform's native ABI.

`to_string` renders structural types. `unify` performs first-order structural
type unification with an occurs check; failure returns an empty optional.
`apply_substitution` recursively resolves bindings and rejects cycles. Conversion
classification recognizes identity and same-kind width widening; integer/float
conversions, narrowing, and differing pointer descriptions require explicit
conversion. Signedness, target floating encodings, and precision-loss policy
belong to the caller.

`FType` represents System F atomic types, variables, arrows, and `forall`.
`free_variables`, `alpha_equivalent`, `substitute`, and `instantiate` support
shadowing and capture-avoiding type substitution. `FTerm` represents variables,
typed lambdas, applications, type abstractions, and type applications. `infer`
checks explicitly annotated terms against a term-variable environment and
returns their type; it does not infer missing lambda annotations or insert
implicit type arguments. Type abstractions must not capture free type variables
in the environment. Ill-typed terms throw `std::invalid_argument`.

## Objects (`firestone::object`)

`ObjectGraph` owns append-only object IDs, class names, and named variant fields:
null, boolean, signed integer, double, string, or `ObjectRef`. Reference fields
maintain memory edges, with ports corresponding to sorted field-name order.
Setting a reference requires a live object. Removing an object referenced by
another object is rejected; self-references do not prevent removal.

`VTable` assigns stable method slots. Reinstalling a name overrides the target
only when result and parameter types match exactly. `ClassHierarchy` supports
single inheritance from an existing class, inherited fields, inherited vtables,
and exact-signature overrides. A base is frozen when derived from, preserving
inherited metadata; `freeze` can also finalize a class explicitly. Fields cannot
duplicate inherited names or use the reserved `$vptr` name.

`object_layout` lays out an optional leading virtual-table pointer and
base-to-derived fields using `LayoutPolicy`. `dispatch` resolves a dynamic class
or object to a method slot/target after checking argument types. ObjectGraph
itself is a general object graph and does not enforce class field schemas.
`visit_reachable` visits each reachable object once in ID order; clients must not
mutate the graph during visits. `clone_reachable` snapshots first, allocates all
copies, then remaps fields, preserving sharing and cycles even when cloning
within the same graph.

## Semantic checking (`firestone::sema`)

`SymbolTable` models lexical scopes with unique local declarations and nearest-
scope lookup. Child scopes can shadow parents. `check_symbols` validates named
operands against declared types, optionally using a block-to-scope map.
Function calls use a separate `FunctionSignatures` registry keyed by instruction
annotation; `check_call`/`check_calls` check result, fixed arguments, and optional
variadic arity. `check_types` reuses representation verifiers and can add these
call-signature checks to SSA validation.

`check_blocks` adds configurable reachability and entry-predecessor requirements
to CFG verification. `check_returns` checks return value presence/types against
one explicit function signature.

`check_definite_assignment` runs a forward must-analysis over entry-reachable
blocks using a supplied set of initially assigned values. It checks uses in
instruction order, accounts for simultaneous phi definitions, and checks phi
operands on predecessor edges. It supports reassigned registers; SSA dominance
remains a separate, stronger contract. The result includes per-block states and
positioned diagnostics.

`EffectPolicy` controls reads, writes, and unsummarized calls. Unknown calls
conservatively have all three effects. A named empty summary declares a pure
call. `EffectPolicy::pure()` permits only operations with empty effect summaries;
policy checks report positioned diagnostics rather than modifying the program.

`checked_integer` evaluates signed add/subtract/multiply/divide/remainder,
negation, and shifts at widths 1–64 without performing overflowing intermediate
arithmetic. Results distinguish overflow, zero divisors, invalid shifts, and
unsupported opcodes. Left shift requires a nonnegative input and an in-range
count; right shift is explicitly sign-extending. `check_overflow` checks literal
ranges and constant arithmetic. Boolean literals use zero/one validation;
checked signed arithmetic otherwise uses two's-complement signed ranges. No
dynamic range, unsigned arithmetic, or arbitrary-width arithmetic is inferred.

## Additional rewriting and e-graph tooling

`Position` is a vector of child indices. Traversal provides preorder positions,
checked subterm access/replacement, and bottom-up transformation. Term ordering
and hashing are structural. First-order `rewrite::unify` permits variables on
both sides, rejects infinite terms with an occurs check, and returns a recursively
resolved substitution. `critical_pairs` standardizes rule variables apart and
enumerates nonvariable overlaps of two unguarded rules, dropping trivial pairs.
It reports potential local divergences; it does not establish confluence.

`Strategy` is an optional-replacement callback. `rule_strategy`, left-biased
`choice`, optional-stage `sequence`, `bottom_up`, and bounded `repeat` support
custom rewrite orchestration. Identity replacements count as no change.

`EGraph::lookup` finds an existing canonical e-node without allocating;
`reachable_classes` follows all alternatives with cycle detection.
`analyze` computes caller-supplied semilattice data for every rebuilt e-class to
a fixed point, with an iteration budget. Callbacks supply monotone node transfer
and associative/commutative/idempotent joins. Results are versioned snapshots;
access through `at(graph, id)` rejects a subsequently changed graph. Re-run
analysis after adding nodes or equalities.

`CostModel` assigns finite nonnegative operator costs with configurable default
and per-operand overhead. `extract_dag` minimizes the existing tree cost, then
shares identical subtrees in a topologically ordered DAG; `expand` validates and
reconstructs that tree. It does not solve global minimum-DAG-cost extraction.
Statistics and DOT output rebuild before reading the graph and preserve cycles.

## Virtual machines (`firestone::vm`)

`Module` owns constants, bytecode functions, and native signatures. `Function`
declares a stack or register model, parameter/result kinds, slot counts, and a
`CodeTape`. Verification checks structural contracts, reachable stack joins,
and path-sensitive slot availability. `compile` transactionally lowers verified
SSA to either model and implements parallel phi assignments on split edges.

`execute`, `StackVM::run`, and `RegisterVM::run` return an owning execution
snapshot at completion or budget exhaustion. `resume` continues the snapshot
with the same module and environment. Calls may cross VM models. Runtime errors
carry a diagnostic code and code position. Host callbacks implement native calls
and optional compilation backends. Heap/global effects remain in `Environment`.

`encode`/`decode` use bounded FSVM v1 binary serialization. `Loader` owns immutable
loaded modules and validates native bindings. `MethodJIT`, `TraceJIT`, and `AOT`
expose backend lifecycle and interpreter fallback; machine-code generation is
supplied by the embedding host. `Guard`, `OSRPlan`, and `deoptimize` make entry
assumptions and baseline-state reconstruction explicit.

See [VM contracts](VM.md) for instruction fields, scalar semantics, binary layout,
snapshot lifetime, dispatch representations, and backend obligations.

## Hardening contracts

The `hardening` suite pins cross-component edge behavior: rejected operand
references and empty symbols, reserved-value and phi-label validation, CPS
continuation and tail atomicity, region-checked RVSDG ports, guarded sea-of-nodes
construction, verified loop queries, pool/heap lifetime rules, null-safe
intrusive pointers, strict object-field erasure, signature diagnostics instead of
throws, widths above 64 diagnosed as unsupported, empty records occupying one
byte, address-space-sensitive pointer conversions, VM program-counter validation,
observer failure attribution, snapshot destination checks, OSR bounds checks,
cache statistics reset, module lookup diagnostics, shift/precision guards, HLSL
stage coverage, SPIR-V framing validation, scheduler cancellation, and
noexcept adapter/thread teardown.
