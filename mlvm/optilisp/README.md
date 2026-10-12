# Optilisp

C++20 reader, validated kernel loader, and optimizer integration. Public APIs
use `mlvm::optilisp` and reside in `include/mlvm/optilisp/`.

## Targets and builds

| Target | Responsibility |
| --- | --- |
| `MLVM::Optilisp` | sfsexp-backed parsing, value-owned AST, source diagnostics |
| `MLVM::OptilispLoader` | Registry loading, contract validation, transactional installation, tree rewriting |
| `MLVM::OptilispPass` | MLVM pass-manager adapter; available in the root build |
| `MLVM::NodeIR` | Four header-only, standard-library-only NodeIR headers |

From the MLVM root:

```sh
cmake -S . -B build -DMLVM_ENABLE_OPTILISP=ON -DMLVM_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`MLVM_ENABLE_OPTILISP` accepts `AUTO` (default), `ON`, or `OFF`. `AUTO`
enables the library when the supplied sfsexp sources exist; `ON` requires them.
The build compiles DOMWeave's root `third_party/sfsexp/src/` directly and performs
no downloads. From the MLVM directory this is `../third_party/sfsexp`, rather
than `mlvm/third_party/sfsexp`. Populate it from the DOMWeave root with
`bash clone.sh sfsexp` or `.\Clone.ps1 -Packages sfsexp`.
`MLVM_THIRD_PARTY_DIR` overrides the root dependency directory;
`MLVM_SFSEXP_SOURCE_DIR` overrides sfsexp specifically. The full MLVM build and
standalone Optilisp build use the same paths for detection and compilation.
The MLVM-owned `cmake/MLVMSfsexp.cmake` adapter builds the upstream C sources
as `mlvm_sfsexp`, disables sfsexp's node caches, and exports the static library
with the installed package. `AUTO` checks the required sources and headers.

The reader and loader also support a standalone build:

```sh
cmake -S optilisp -B build-optilisp -DMLVM_BUILD_TESTS=ON
cmake --build build-optilisp
ctest --test-dir build-optilisp --output-on-failure
cmake --install build-optilisp --prefix /desired/prefix
```

Installed standalone consumers use `find_package(MLVMOptilisp CONFIG REQUIRED)`
and link `MLVM::OptilispLoader`. sfsexp support and LGPL notices are installed
with the libraries.

## Reader

`parse()` and `parse_file()` return forms or diagnostics, never partial success.
The reader supports lists, symbols, keywords, strings, decimal numeric literals,
`true`, `false`, `nil`, and semicolon comments. String escapes are `\n`, `\r`,
`\t`, `\0`, `\"`, and `\\`. Numbers retain their textual representation;
there is no implicit host arithmetic. Quote syntax, binary atoms, atom escapes,
empty keywords, and raw NUL bytes are rejected.

Lexical validation enforces byte, node, and nesting bounds before sfsexp parses
each expression. AST conversion retains file/line/column locations and owns all
data; no C parser pointers escape. sfsexp calls are serialized because its error
and allocator state are global. AST equality ignores source locations.

## Loader

```cpp
auto registry = load_registry("libstone/blackstone/optilisp/stdlib");
if (!registry.ok()) { /* report registry.diagnostics */ }
Loader loader(registry.registry);
auto catalog = loader.load_directory("tools/blackstone/kernels");
if (!catalog.ok()) { /* report catalog.diagnostics */ }
RewriteSystem rewrites;
Diagnostics diagnostics;
bool installed = loader.install(catalog, rewrites, diagnostics);
```

`load()` also accepts an explicit file span. Files and resulting kernels are
deterministically ordered. Validation covers required metadata and documentation,
duplicate fields and IDs, stage compatibility, capability/analysis resolution,
contradictory conditions, analysis-contract consistency, transformation shape,
duplicate contracts, and prerequisite cycles. Any diagnostic discards the
entire catalog. Installation revalidates descriptors before invoking a backend.

This loader defaults to the supplemental strict contract profile. All 50
supplemental kernels load successfully. Original upstream records currently
fail this profile; findings are documented in
`tools/blackstone/UPSTREAM-ISSUES.md`. Registry monoids are read syntactically;
their algebraic laws are not executed or proved by this loader.
`Loader::Profile::blackstone` explicitly accepts the original documentation
profile and defaults an absent analysis contract to consuming declared analyses,
invalidating all registered analyses, and preserving none. `load_source()` loads
in-memory author definitions with the same validation as files. See
`../docs/KERNELS.md` for the generic integer executor, textual declarations,
CLI authoring, and transactional `run_kernels()` API.

## Rewrite execution

The tree backend installs `(rewrite PATTERN RESULT)` contracts. Constructor
heads are literal symbols; operand symbols bind terms. Repeated bindings require
structural equality. Other atom kinds are literal. Result variables must be
bound by the pattern. Computed results and other transformation verbs require
a host optimizer implementation and produce an explicit backend diagnostic.

`RewriteContext` supplies the current stage, namespaced global facts
(`capability:ID`, `analysis:ID`, `history:ID`), and a candidate-specific
`ConditionProver`. The prover receives the kernel, matched input, proposed result,
and bindings. Every precondition requires `proven_true`; every forbidden condition
requires `proven_false`. Unknown, absent, or invalidated evidence blocks rewriting.
The backend does not infer semantic legality from a syntactic match.

Optimization uses preorder traversal and stable kernel-ID order until a fixed
point. Each successful rewrite invalidates unpreserved analysis facts and records
the kernel's history fact. Source provenance is retained for bound terms and
assigned from the matched input for newly constructed terms. Cycles, exhausted
budgets, resource-limit violations, and callback exceptions discard all changes
to both term and facts. Callbacks must be side-effect-free; their external state
cannot be rolled back by the library. Installation and optimization on a shared
mutable instance require caller synchronization.

## Host optimizer integration

`KernelConsumer` and `Installation` support arbitrary rewrite systems and
optimizers. A transaction copies descriptors, publishes the complete batch on
commit, and rolls back on destruction before commit. Backends must explicitly
reject unsupported transformations.

`kernel_pass()` binds a loaded descriptor to a `ModuleOptimizer`. The host
provides supported transformations, stage identification, facts, and actual IR
execution. MLVM's pass manager supplies required analyses, invalidation,
copy-on-run rollback, and post-pass verification. Global condition facts queried
by this adapter must establish legality for the host's complete application;
candidate-specific checks belong in `ModuleOptimizer::apply()`.

The tree engine operates on Optilisp expression trees. The NodeIR graph backend
below preserves explicit definition identity. MLVM dialect transformation
semantics remain responsibilities of host adapters.
Tests exercise loader rejection, transactional installation, guarded rewriting,
analysis invalidation, cycles and limits, and MLVM pass rollback/verification;
they do not establish general semantic equivalence for arbitrary kernels.

## Header-only NodeIR

The complete NodeIR implementation consists of:

```text
include/mlvm/NodeIR/Commo.hpp
include/mlvm/NodeIR/NodeHIR.hpp
include/mlvm/NodeIR/NodeMIR.hpp
include/mlvm/NodeIR/NodeLIR.hpp
```

Types reside in `mlvm::nodeir`. Each stage is a distinct `Graph<Stage>` type;
stages cannot be implicitly interchanged. Graphs own nodes, use monotonic IDs,
and store roots, operand edges, control successors, type/effect metadata,
source provenance, and extensible attributes. Verification checks identities,
dangling references, literal shape and resource bounds. Data and control cycles
are representable; SSA, dominance, opcode signatures, target semantics and
arithmetic domains require explicit host contracts.

`Pattern` implements constructor-headed kernel patterns, variable bindings and
exact literal matching. `compile_node_pattern()` compiles parsed Optilisp into
this header-only representation. `(and x x)` matches two references to the same
definition, not merely two structurally identical computations. `(integer a)`
matches an `integer` operation whose operand is bound to `a`; keyword/string
patterns such as `(machine-op :kind "ADD")` match ordered literal operands.
This pattern compiler performs no arithmetic evaluation.

`NodeRewriteSystem<NodeHIR>`, `NodeRewriteSystem<NodeMIR>` and
`NodeRewriteSystem<NodeLIR>` accept transactional installation through `Loader`:

```cpp
NodeRewriteSystem<mlvm::nodeir::NodeHIR> system;
Diagnostics diagnostics;
loader.install(selected_hir_catalog, system, diagnostics);
NodeRewriteContext<mlvm::nodeir::NodeHIR> context;
// Supply context.facts and a candidate-specific context.prove callback.
auto result = system.optimize(graph, context);
```

`selected_hir_catalog` must be a successful stage-filtered load. Installing a
kernel for another stage fails atomically. The backend supports `rewrite` forms;
folding and global transformation verbs require dedicated host implementations.

Graph optimization replaces all uses of a matched definition and preserves
bound node IDs, sharing and metadata. It rejects replacement of control nodes
and replacements depending on the replaced definition. Unreachable definitions
remain allocated for identity/provenance; matching visits only reachable nodes.
Canonical graph fingerprints retain sharing and control structure but ignore
allocation IDs and locations, permitting cycle detection after node creation.

`NodeRewriteContext::materialize` optionally constructs or evaluates result
patterns on a private graph candidate, supplying host-defined types, effects,
and attributes. Default construction retains source locations but leaves type
and effects unknown. `prove` receives original and proposed graphs, node IDs,
kernel metadata, and bindings; it must prove legality across every replaced use.
Generic structural verification does not establish semantic equivalence.
Budget, proof, materialization or structural failures roll back graph and facts.

## Translation to and from other IRs

Header-only adapters use callbacks returning newly owned values:

```cpp
auto imported = mlvm::nodeir::translate_to<mlvm::nodeir::NodeHIR>(foreign_ir, importer);
auto exported = mlvm::nodeir::translate_from<ForeignIR>(*imported.value, exporter);
```

Check each result's `ok()` before accessing `value`. `importer(const ForeignIR&)`
returns a stage-specific NodeIR graph; `exporter(const NodeHIR&)` returns the
foreign IR. Both may throw useful diagnostics for unsupported constructs.
Input graphs are verified and imports are verified before publication.
Adapters explicitly map opcodes, types, effects, blocks, successor edges,
shared definitions and provenance; no implicit lowering or guessed arithmetic
semantics occurs. A callback targeting another NodeIR stage also provides an
explicit stage conversion.

The loader backend provides a complete transactional pipeline:

```cpp
auto output = system.optimize_translated(foreign_ir, context, importer, exporter);
// On output.ok(), output.value owns the rewritten foreign IR.
```

Import, optimization and export must all succeed before analysis/history facts
commit. The foreign source is const. Callbacks must avoid external side effects;
the library cannot roll back external state. `optilisp/tests/nodeir.cpp` contains
a working bidirectional foreign-SSA adapter, shared-definition rewrites, MIR/LIR
graph checks, and export-failure rollback. A separate executable links only
`MLVM::NodeIR` to verify the header-only implementation.
