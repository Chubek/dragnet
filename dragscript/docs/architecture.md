# Initial architecture and ownership

## Pipeline and boundaries

1. `Engine::compile` owns the supplied `Source`. Mode inference is extension-based
   and can be overridden by `SourceKind`.
2. `frontend/frontend.cpp` creates a request-local RAII Tree-sitter parser/tree.
   It validates UTF-8, selects TS or TSX, checks grammar ABI assignment, bounds
   parser progress, walks **all** nodes for syntax/missing nodes and resource
   limits, then converts supported syntax to DSIR. Unknown syntax fails closed.
3. DSIR is an owned flat node arena with stable indices, tagged kinds, normalized
   operators, primitive literals, lexical declarations, function parameters,
   per-iteration loop bindings, child indices and original half-open source spans.
   A script owns one source record. Raw Tree-sitter handles never survive
   compilation.
4. `semantic/validator.cpp` checks duplicate lexical names, parameter/body
   conflicts, return placement, `break`/`continue` targets and block requirements
   for branch declarations. Type-only syntax is erased in the converter; this is
   not type checking.
5. `mlvm/lowering.cpp` registers builtin and `ds` dialects in a request-local
   Context owned by the artifact, lowers to a high-level MLVM tree representation,
   verifies the Module, and publishes its canonical debug text alongside DSIR only
   on success.
6. Two execution backends consume that artifact. `runtime/interpreter.cpp` walks
   DSIR nodes; `mlvm/tree_interpreter.cpp` walks the verified `ds.*` operations.
   Both delegate values, coercions, scopes, hosts and budgets to
   `runtime/session.hpp`, so the difference is dispatch only. Their agreement is a
   tested property, enforced by every semantic test and by the fuzz sweep.

The public API is in `include/dragscript/`. No parser, module, context intern ID,
runtime arena handle or owning host pointer appears in it. Library layers return
structured diagnostics rather than printing.

## MLVM tree dialect

The first IR deliberately uses **lazy semantic child regions**, not generic CFG
blocks. Each DSIR node becomes one `ds.*` operation with one result:

- expressions return `ds.value` (a tagged runtime value, including internal
  callable, object and array handles);
- statements return `ds.completion` (kind, empty/valued flag, and payload,
  represented abstractly at this stage);
- each child region is one argument-free block containing one child semantic
  operation and a terminating `ds.yield` of its result;
- the top-level block contains `ds.program` and its terminating `ds.yield`.

Operations are `ds.program`, `ds.block`, `ds.sequence`, `ds.empty`,
`ds.expression`, `ds.declaration`, `ds.function`, `ds.if`, `ds.return`,
`ds.literal`, `ds.identifier`, `ds.unary`, `ds.binary`, `ds.assign`, `ds.call`,
`ds.property`, `ds.object`, `ds.array`, `ds.member`, `ds.assign_member`,
`ds.while`, `ds.do`, `ds.for`, `ds.for_of`, `ds.break`, `ds.continue`,
`ds.update`, `ds.compound` and `ds.yield`. Source byte ranges are required
attributes; operations also carry MLVM one-based source locations from a
`SourceManager` owned by the artifact. Literals have explicit tags and payloads:
decimal binary64 text, boolean spelling, empty nullish payload, or hexadecimal
UTF-16 units. `ds.update` and `ds.compound` bind an identifier in `text` and an
operator in `increment`/`operator`, because `text` is the binding name.

Optional `ds.for` parts are marked by `init`/`test`/`update` flags so the region
layout is explicit; per-iteration binding names travel in `binding_N`. Verifiers
check result representation, operation cardinality, child-region shape and yield
types, value-versus-statement child classification, required source, name,
operator, parameter and literal attributes, const initializer requirements, and
for-loop flag agreement. MLVM checks SSA identity, visibility, use lists and
terminators. Lexical and contextual validation remains in DSIR. These contracts do
not claim generic multi-block dominance, a memory layout, or safe generic rewrites.

All definitions have conservative `unknown` effects because they may allocate,
fail, read/write cells, call a host or contain such children. No optimizer, pass
or kernel is applied.

The Context and SourceManager are declared before the Module and owned by the
shared `LoweredModule`, so both may outlive the compilation that produced them.
Lowering appends through MLVM APIs and resizes regions before retaining child
references. Printed text loses MLVM location objects but retains byte attributes;
DSIR and the owned source remain the diagnostic source of truth. Reparsing
printed text describes the new buffer, so provenance assertions are made against
the lowered module rather than a reparsed copy. There is no public API to
import or execute arbitrary textual MLVM IR.

## Lifecycle and memory

`Engine` owns options and its host registry. Each run creates a fresh
environment arena, object and array arenas, and function arena. Environment
parents and closures are checked indices; bindings store primitives or
non-owning arena/registry handles. Cycles such as a function capturing the cell
that holds it contain no owning reference cycle. The entire arena is destroyed at
run completion, including failure. Immutable UTF-16 strings are shared owning
values without backlinks to arenas or engines.

`CompiledScript` is an immutable, shared artifact owning source, DSIR, the
verified MLVM module and its debug text. It can outlive its compiling Engine and
be run in another Engine, whose host bindings and runtime/string limits apply.
Primitive `Value` results can outlive both. Script functions and objects cannot
cross the public result or callback boundary; `DS_BOUNDARY` is explicit. There
are no persistent script globals, realms, modules, finalizers or pending async jobs
in this version.

An Engine requires external synchronization. Separate Engines and immutable
artifacts can be used independently; callers synchronize their own callbacks.
Same-engine compile/run/registration during a request is rejected, including from
inside a host callback. Host argument spans and `HostContext` functions are valid
only during the callback. Copying a primitive argument is safe. Destroying the
executing Engine from its callback is outside the API lifetime contract.
Cancellation pointers must outlive requests.

## Dependency/build decisions

The core links only MLVM Core/IR, Tree-sitter, and the two generated grammar
libraries. MLVM's existing `MLVM::IR` target is reused when available; otherwise
its sibling subdirectory is configured locally with optional components disabled.
Existing parser targets are reused. The missing runtime checkout is handled by an
explicit installed-runtime fallback rather than a new vendored copy. QuickJS is
optional and test-only. Browser capabilities, the resolver, event loop, DOM,
optimization kernels and machine-code backends remain separate future layers.