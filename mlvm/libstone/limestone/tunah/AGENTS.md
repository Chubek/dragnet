# AGENTS.md -- Tunah

Tunah is Limestone's e-graph-based equality-saturation optimizer. It relies entirely on `third_party/equinox-ng` for core equality saturation and e-graph data structures. Optimization patterns across Limestone stages and intermediate languages (ILs) are expressed in S-expressions. Tunah provides specialized adapters for each intermediate language. It relies heavily on `third_party/metatk`:
- `DSLtk` provides a C++-native domain-specific language, combinatory parser utilities, and `dsl::node` representations.
- `EkippX` preprocesses Tunah rewrite and analysis specifications.
- `SExprTk` parses S-expressions into structured event streams for pattern compilation and ingestion.

This document defines architecture, operational rules, interface contracts, and contributor requirements for Tunah. Where specific repository paths or target names are not explicitly fixed below, follow repository-configured targets and directory layouts.

---

## 1. Scope and Non-Goals

### 1.1 In-Scope Responsibilities
- Provide equality saturation optimization across Limestone intermediate languages and pipeline stages.
- Ingest, validate, and compile S-expression rewrite rules using `third_party/metatk` components (`DSLtk`, `EkippX`, `SExprTk`).
- Interface with `third_party/equinox-ng` to construct e-graphs, apply rewrite rules, perform e-class analyses, and extract optimal terms according to defined cost models.
- Provide bi-directional intermediate language (IL) adapters converting between Limestone IL data structures and Equinox-NG e-graph representations.
- Maintain sound term rewrites, provenance tracking, and deterministic optimization passes across all supported architectures.

### 1.2 Non-Goals
- **Instruction Semantics Duplication**: Tunah does not define primary instruction semantics; it consumes them from authoritative ISA and language definitions.
- **Direct Code Generation**: Tunah is an IL-to-IL equality saturation optimizer; low-level machine emission and binary encoding are delegated to backend stages.
- **Ad-Hoc IL Rewrites**: Tunah prohibits unvalidated, hand-written mutating passes that bypass e-graph validation and cost-driven extraction.
- **Custom E-Graph Engines**: Tunah does not implement a bespoke e-graph runtime; all e-graph operations defer strictly to `third_party/equinox-ng`.

---

## 2. Repository Layout and Responsibilities

When directory layouts are configured by the build system, follow repository-configured targets. The conceptual Tunah layout is structured as follows:

```
src/tunah/
  include/tunah/
    core/             # Core equality saturation engine bindings and driver interfaces
    adapters/         # Per-IL bidirectional adapters and AST lowering contracts
    dsl/              # DSLtk integration, combinators, and C++-native AST nodes
    parser/           # SExprTk and EkippX parser pipelines and event dispatchers
    analysis/         # E-class analyses, abstract domains, and property lattices
    cost/             # Extraction cost models and heuristic weights
    diagnostics/      # Diagnostics engine, error reporting, and provenance models
  src/
    core/             # Driver implementation, Equinox-NG wrapper logic
    adapters/         # Implementation of IL adapters
    dsl/              # DSL definitions and node implementations
    parser/           # Specification parsers and validator implementations
    analysis/         # Analysis callbacks and transfer functions
    cost/             # Cost evaluation functions and term extractors
    diagnostics/      # Formatter and diagnostic emitter implementations
tests/tunah/
  unit/               # Unit tests for components, combinators, and adapters
  saturation/         # Equality saturation integration and convergence tests
  roundtrip/          # IL-to-EGraph-to-IL round-trip fidelity tests
  specs/              # Spec validation, malformed pattern diagnostics tests
rules/
  common/             # Shared algebraic, arithmetic, and logical rewrite rules
  stages/             # Stage-specific rewrite specifications (.sexpr / .dsl)
```

---

## 3. Architecture and Data Flow

Tunah processes input programs via a modular pipeline bridging Limestone ILs with Equinox-NG:

```
  [ Limestone IL Input ]
            |
            v
  [ IL Adapter: Ingest & Normalize ] ----> [ Legality & Type Verification ]
            |                                         |
            +-------------------+---------------------+
                                |
                                v
                   [ Equinox-NG E-Graph Builder ]
                                |
    [ S-Expression Specs ]      |
            |                   |
     (EkippX Preprocessor)      |
            |                   |
      (SExprTk Parser)          |
            |                   |
     (DSLtk Combinator)         |
            v                   v
     [ Compiled Rules ] ==> [ Saturation Engine (Equinox-NG) ]
                                |  ^
                                |  | (E-Class Analyses & Transfer Functions)
                                v  |
                            [ Saturated E-Graph ]
                                |
                                v
                            [ Cost Model & Extractor ]
                                |
                                v
                   [ IL Adapter: Reconstruct & Emit ]
                                |
                                v
                     [ Optimized Limestone IL ]
```

### 3.1 Step-by-Step Flow
1. **Spec Compilation**: S-expression rules undergo preprocessing via EkippX, lexical parsing via SExprTk into an event stream, and combinatory AST construction using DSLtk to build compiled Equinox-NG rewrite patterns.
2. **Ingestion & Normalization**: An IL adapter takes the Limestone IL, validates structural invariants, normalizes node representations, and populates the Equinox-NG e-graph.
3. **Saturation Execution**: Equinox-NG executes match-apply cycles, firing valid rewrites while updating attached e-class analyses until reaching saturation, resource limits, or iteration budgets.
4. **Extraction**: The extractor walks the e-graph, computing cumulative costs under the active cost model and selecting the optimal representative term.
5. **Reconstruction**: The adapter reconstructs a valid Limestone IL module from the extracted term, attaching verified debug info and provenance metadata.

---

## 4. Equinox-NG Integration Rules

- **Engine Authority**: `third_party/equinox-ng` is the sole engine for e-graph storage, congruence closure, e-class merging, and pattern matching.
- **No Concrete API Invention**: Implementations must adhere strictly to Equinox-NG's exported C++ headers and types as supplied in the repository.
- **Node Immutability & Canonicalization**: E-nodes registered with Equinox-NG must be canonical, immutable value types supporting strict ordering, equality, and hash operations.
- **Analysis Transfer Soundness**: All e-class analysis hooks attached to Equinox-NG must be monotonic transfer functions. Merging analysis values across unified e-classes must form a join-semilattice.
- **State Isolation**: Each optimization session must instantiate an isolated Equinox-NG e-graph context. Shared mutable global state across threads or saturation runs is strictly prohibited.

---

## 5. MetaTk Component Roles

Tunah leverages `third_party/metatk` for language processing and pattern compilation:

### 5.1 DSLtk
- Provides C++-native domain-specific language structures for programmatic rule construction.
- Supplies combinatory parser building blocks for grammar composition.
- Uses `dsl::node` as the standardized intermediate AST representation for pattern constructs before conversion to Equinox-NG rules.

### 5.2 EkippX
- Operates as the front-end preprocessor for Tunah rewrite specifications and configuration files.
- Handles macro expansion, include resolution, conditional feature gating, and pre-parsing transformations.
- Emits clean, normalized specification streams free of preprocessor directives.

### 5.3 SExprTk
- Parses S-expression text into an event stream (`on_open_list`, `on_atom`, `on_close_list`, etc.).
- Drives streaming compilation of rewrite rules without requiring unbounded intermediate memory allocations.
- Emits detailed source coordinates (byte offsets, line/column numbers) for fine-grained diagnostic attribution.

---

## 6. S-Expression Specification Rules and Validation

Optimization patterns and analysis hooks across Limestone stages are declared in S-expression files (`.sexpr` / `.dsl`).

### 6.1 Syntax and Structure
- Specifications must follow strict, well-formed S-expression grammar parsed via SExprTk.
- Standard form: `(rule <name> <pattern> <replacement> [:where <conditions>] [:cost <delta>])`.
- Pattern variables must be prefixed with `?` (e.g., `?x`, `?y`, `?imm`).
- Multi-arity operators must be explicitly defined in the operator signature table.

### 6.2 Validation Rules
- **Symbol Resolution**: All operator symbols in patterns and replacements must resolve to valid IL operators or Tunah intrinsic constructs.
- **Variable Binding Completeness**: Every variable appearing on the right-hand side (replacement) must be bound on the left-hand side (pattern) or produced by an evaluated `:where` clause.
- **Type Compatibility**: Types inferred or annotated on pattern variables must match operator operand requirements.
- **Malformed Specification Handling**: Malformed or unresolvable S-expression patterns must fail validation at compile/initialization time with precise line, column, and diagnostic messages. Silent discards are prohibited.

### 6.3 Implemented Generic Term/Rule API

The current public API is in `tunah.hpp`; usage and implementation limits are
documented in `README.md`. `Session::load_rules` accepts a source name, and
`load_rules_file` loads a bounded rule specification transactionally. Specifications
may declare `(operator name arity)`. Native EkippX `@define`/`@deflit` macros are
isolated per load; SExprTk events populate source-located DSLtk AST nodes.

`define_predicate(name, arity, callback)` registers host analysis predicates for
`:where (predicate ?variable literal ...)` and conjunctions. Missing predicates,
unbound arguments, and incompatible arities are compile-time errors. Predicates
inspect equivalent structured representatives and must return true only for
proved preconditions; false means unknown or inapplicable. Arithmetic sign-extension
distribution in the bundled scalar/vector tuners requires an overflow proof.

`parse_term`/`format_term` and structured `Session::saturate` provide a generic
term boundary. `CostModel` configures local non-negative literal/operator costs;
Equinox-NG still owns extraction. Checked additive costs prevent overflow from
creating falsely cheap terms. Prepared native patterns are immutable and private,
and saturation contexts remain isolated. `Limits::trace` controls admitted-match
traces with rule locations. Expanded-stream locations are explicitly marked when
EkippX changes the input. Concrete IL type/effect verification and reconstruction
remain responsibilities of the respective IL adapters.

`unisel_adapter.hpp` implements a concrete typed bidirectional graph adapter.
Hosts explicitly register pure concrete operator signatures and justified rules.
Ingress, extracted-term legality, transactional reconstruction, deterministic
value remapping, effect/CFG/live-out preservation, and pure dead-node pruning are
verified at the adapter boundary. Other IL adapters retain the same lifecycle.

`binary_adapter.hpp` supplies the lifted-semantic Bin2Bin adapter via the separate
`tunah_bin2bin` target. It snapshots a Session and requires host legality analysis
at ingress and extraction plus an explicit semantic/analysis context identity.
Operators, rules, conditions, costs and budgets enter its deterministic cache
identity. Cooperative time/cancellation limits disable translated-byte cache
reuse. Bin2Bin owns target matching, encoding, branch relocation and layout;
instruction/control boundaries remain intact. CFG-changing binary optimization
requires a region adapter rather than mutating those boundaries through a term.

`limestone/optimization.h` / `Limestone::optimization` supplies opaque owning C
sessions and results for the same term/rule engine, with costs, budgets, match
traces, proof predicates and cooperative cancellation. Callback userdata may
have a nonthrowing release retained by owning snapshots. The core's
`limestone_target_set_optimizer` copies typed graph declarations and session
configuration into a pipeline target; later source mutation/destruction does
not change it. Optional Python wrappers consume this C ABI.
`limestone_optimizer_binary_transform` exposes the same owning Bin2Bin adapter
through C/Python, requiring a legality callback and semantic context identity.
Offline translation and synchronous runtime creation copy the transform; budget
and cancellation policies preserve the C++ adapter's cache separation.

---

## 7. IL Adapter Contract, Lifecycle, Normalization, Legality, and Diagnostics

Every Limestone IL stage interfacing with Tunah must implement an IL Adapter adhering to the standard adapter lifecycle contract.

### 7.1 Adapter Lifecycle
1. **`Ingest(const StageIL& in, EGraphContext& ctx) -> RootHandle`**: Traverses the input IL, validates preconditions, normalizes terms, and populates the e-graph.
2. **`ValidateLegality(const ENode& node, const EGraphContext& ctx) -> bool`**: Verifies that proposed node combinations conform to IL structural and type constraints.
3. **`Reconstruct(RootHandle root, const ExtractedTerm& term, StageIL& out) -> AdapterResult`**: Reconstructs the validated Limestone IL data structure from the extracted e-graph term.

### 7.2 Normalization and Commutativity
- Adapters must normalize symmetric operands (e.g., commutative arithmetic, unordered operand lists) into canonical order during ingestion to maximize e-class sharing.
- Bit-width, signedness, and calling convention flags must be explicitly preserved in e-node discriminators.

### 7.3 Legality and Diagnostics
- Adapters must reject invalid IL constructs with clear diagnostics detailing node identifiers, IL locations, and violation reasons.
- Adapters must preserve debug locations, source provenance, and metadata across ingestion and reconstruction phases.
- Adapters must not alter semantic execution behavior or duplicate core saturation logic; their responsibility is strictly bidirectional translation.

---

## 8. E-Graph Invariants and Rewrite Soundness

### 8.1 Soundness Invariants
- **Semantic Equivalence**: A rewrite rule `L -> R` is sound if and only if for all valuations satisfying `:where` conditions, `eval(L) == eval(R)`.
- **Monotonicity**: Adding equivalent expressions to an e-class must never invalidate existing equalities or corrupt e-class invariants.
- **No Cyclic Divergence**: Self-referential or cycle-forming rewrites that do not simplify terms must be constrained by conditions or structural metrics to avoid infinite expansion.

### 8.2 Provenance and Auditability
- Every e-node created as a result of a rewrite must record the rule identifier that introduced it.
- Tunah must support rule tracing modes where every equality step can be reconstructed as a formal derivation chain for debugging and verification.

---

## 9. Analyses, Extraction, and Cost Models

### 9.1 E-Class Analyses
- Analyses attach auxiliary domain facts (such as constant values, known bits, interval bounds, side-effect summaries, and purity flags) to entire e-classes.
- When two e-classes merge, analysis values merge using an associative, commutative, and idempotent `join` operator.
- Transfer functions derive parent analysis values from child e-classes monotonically.

### 9.2 Cost Models
- Cost models evaluate extracted terms by assigning scalar or lexicographical cost tuples (e.g., `<latency, code_size, register_pressure>`).
- Cost computation must be strictly non-negative and monotonic: `cost(Op(c1, c2, ...)) >= sum(cost(ci)) + base_cost(Op)`.
- Extractor implementations use dynamic programming or shortest-path algorithms across the saturated e-graph to find the minimal-cost representative.

---

## 10. Termination, Resource Limits, and Failure Behavior

Equality saturation is inherently non-terminating on general rewrite systems without explicit resource bounds.

### 10.1 Resource Limits
Tunah exposes configurable saturation bounds:
- **Iteration Limit**: Maximum number of match-apply cycles per saturation pass.
- **Node Limit**: Maximum total e-nodes allowed in the e-graph before saturation halts.
- **E-Class Limit**: Maximum number of e-classes allowed in memory.
- **Time Budget**: Hard execution deadline for equality saturation per compilation unit/function.

### 10.2 Failure & Cancellation Behavior
- **Graceful Halt**: Upon hitting resource limits or cancellation signals, Tunah halts rewrite application, restores e-graph congruence closure, and proceeds to extraction.
- **Extraction Fallback**: If saturation terminates prematurely, extraction still yields a valid, semantically correct expression (at worst equivalent to the unoptimized input).
- **Hard Errors**: Fatal errors (such as internal invariant violations, unrecoverable memory exhaustion, or malformed adapter outputs) emit structured diagnostics and return standard failure codes without crashing the host process.

---

## 11. Limestone Pipeline/Stage Integration

- **Stage Isolation**: Tunah can be invoked across multiple Limestone stages (e.g., high-level IL optimization, loop normalization, low-level instruction selection). Each stage uses its respective IL adapter and stage-specific rule set.
- **Preservation of Pipeline Invariants**: Rewrites within a stage must not introduce opcodes or idioms reserved for subsequent stages unless explicitly targeted for stage lowering.
- **Metadata Propagation**: Optimization must maintain pipeline-level metadata including alias analysis attributes, branch probabilities, debug line records, and memory access tags.

---

## 12. C++ API, Style, Naming, Ownership, and Errors

### 12.1 Language and Style Standards
- Implementation adheres to modern standard C++ (C++20 or repository baseline).
- Code style follows Limestone repository conventions (ASCII punctuation, consistent naming, no tabs, 2-space or 4-space indent as configured).

### 12.2 Naming Conventions
- Types and Concepts: `PascalCase` (e.g., `EGraphDriver`, `RewriteRule`, `CostModel`).
- Functions and Methods: `CamelCase` or `snake_case` matching repository convention.
- Member Variables: `m_member` or `member_` suffix/prefix matching repository standard.
- Constants and Enum Values: `kConstant` or `UPPER_SNAKE_CASE`.

### 12.3 Ownership and Memory Management
- Strict RAII everywhere. Raw owning pointers are prohibited; use `std::unique_ptr` for exclusive ownership and `std::shared_ptr` only when shared ownership is strictly necessary.
- Pass non-owning view references via `std::string_view`, `std::span`, or `const &`.
- E-graph internal nodes are owned and managed by the Equinox-NG context.

### 12.4 Error Handling
- Use expected/result types (e.g., `Result<T, Diagnostic>`) for recoverable operational and parsing failures.
- Exceptions must not cross public C API boundaries (if exposed) and must conform to Limestone error handling policies.

---

## 13. Build, CMake, and Dependency Rules

- **Authoritative Targets**: Consume `third_party/equinox-ng` and `third_party/metatk` strictly via repository-configured CMake targets (e.g., `equinox::equinox-ng`, `metatk::dsltk`, `metatk::ekippx`, `metatk::sexprtk`).
- **Hermetic Dependencies**: Tunah must not add external or undeclared third-party dependencies outside of the approved Limestone toolchain.
- **Target Boundaries**: Tunah core library targets must depend only on Equinox-NG, MetaTk, and core Limestone foundation utilities. IL adapters depend on their respective IL definitions.

---

## 14. Testing and Validation Matrix

Every change to Tunah must be verified across the testing matrix:

| Test Suite | Purpose | Key Checks |
|---|---|---|
| **Unit Tests** | Verify MetaTk parsing and combinators | SExprTk event firing, EkippX macro expansion, DSLtk AST generation |
| **Adapter Round-Trip** | Verify adapter fidelity | `IL -> EGraph -> IL` identity without semantic drift |
| **Rewrite Soundness** | Verify individual rewrite rules | Property-based testing, equivalence checking against reference semantics |
| **Saturation & Convergence**| Test saturation behavior | Monotonic cost reduction, termination within limits, cycle avoidance |
| **Diagnostic Tests** | Test error handling | Accurate error reporting on malformed S-expressions and illegal rewrites |
| **Reproducibility Tests** | Test deterministic builds | Identical output bytecode/IL across runs regardless of host scheduling |

---

## 15. Performance, Observability, Debugging, and Reproducibility

### 15.1 Determinism and Reproducibility
- All iteration over e-classes, rule matches, and node applications must execute in deterministic order.
- Pointer-address-based hashing or sorting that introduces non-determinism across runs is strictly prohibited.

### 15.2 Observability and Tracing
- Provide tracing switches to dump:
  - Initial e-graph state (`.dot` or graph visualization formats).
  - Step-by-step rewrite rule firing with applied bindings.
  - Saturated e-graph state and extraction traversal decisions.
- Compile with zero-overhead tracing abstractions when tracing is disabled at runtime.

---

## 16. Documentation and Change Discipline

- Every new rewrite rule added to repository rule sets must be accompanied by docstrings explaining the semantic justification and formal equivalence precondition.
- API changes in Tunah core interfaces or adapter contracts require updating this document (`AGENTS.md`) and associated architecture guides.
- PRs modifying saturation heuristics, cost models, or extraction algorithms must include comparative benchmark metrics showing no regressions in compilation latency or code quality.

---

## 17. Pre-Submit Checklist

Before submitting changes to the Tunah subsystem, verify:

- [ ] `AGENTS.md` remains clean, valid Markdown beginning with `# AGENTS.md -- Tunah`.
- [ ] Code strictly follows C++ style, RAII ownership, and naming conventions without raw owning pointers.
- [ ] S-expression patterns parse cleanly through SExprTk and EkippX, with all variables bound.
- [ ] IL adapters pass round-trip tests (`IL -> EGraph -> IL`) preserving all debug metadata.
- [ ] All rewrite rules are semantically sound and guarded by explicit `:where` conditions where required.
- [ ] Analysis transfer functions are monotonic and merge operations form valid join-semilattices.
- [ ] Saturation passes respect iteration, node, and time budgets with graceful extraction fallbacks.
- [ ] Execution and extraction are completely deterministic across multiple consecutive runs.
- [ ] CMake target boundaries are respected without introducing undeclared dependencies.
- [ ] Full test matrix (unit, adapter, saturation, diagnostics, reproducibility) passes cleanly.
