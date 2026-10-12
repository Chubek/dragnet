# Unisel

Unisel selects target instruction patterns over a machine-independent SSA/CFG
graph. `unisel.hpp` exposes nodes, patterns, candidates, constraints, solutions,
and Scheduler IR emission; vendor Satie types are private. Link
`Limestone::unisel`.

```cpp
auto prepared = limestone::unisel::prepare(program);
if (!prepared) return prepared.error();
auto solution = limestone::unisel::solve(prepared.value(), patterns);
if (!solution) return solution.error();
auto region = limestone::unisel::emit_scheduler(prepared.value(), patterns,
                                               solution.value());
```

`candidates` and `constraints` provide inspection before solving. The global
selector uses Satie CDCL with coverage, incompatibility, boundary-definition, and
dependency legality constraints, plus pseudo-Boolean cost bounds bit-blasted
through Satie's theory encoder; `solve_greedy` is an explicit alternative.
Selection minimizes scalar pattern costs and keeps physical allocation and issue
cycles for downstream stages. Nested pattern trees support concrete types,
repeated named bindings, immediate ranges, and register-class constraints.
Flat `Pattern::operands` entries are concrete source types; an empty entry or
`v` denotes an untyped value boundary. Matching, BURS adaptation, and canonical
UMD printing use the same normalized pattern contract.

## UMD

`umd.hpp` loads/validates source-located UMD independently of solving. A document
contains one machine and an optional source program. Example:

```text
machine scalar {
  regclass G = [$r0, $r1];
  operator const(0);
  operator add(2);
  instruction CONST { latency = 0; }
  instruction ADD { latency = 1; }
  pattern constant: const():i64 -> CONST;
  pattern sum: add(?lhs:i64, ?rhs:i64):i64 -> ADD;
  default_register_class = G;
}
program main {
  node %1 = const(20):i64;
  node %2 = const(22):i64;
  node %3 = add(%1, %2):i64;
  output %3;
}
```

The authoritative syntax is `parsers/unisel.g`. `print_umd` emits a deterministic
normalized machine description. `from_metacode` preserves instruction and
architecture metadata/provenance and constructs patterns only from explicit
`tooling.instruction_selection.selection_tree`. Semantic expressions remain
metadata; generic opcode classifications do not prove selectable equivalence.

`load_umd_file(path)` supports top-level `include "relative.umd";` directives.
Included declarations are expanded in order and validated as one machine and an
optional program. Include a machine-only file from a graph document to share
target contracts. Original file/line/column information is retained. File paths
are canonicalized for cycle detection, including symlink aliases.
Pattern `origin "..."` overrides provenance explicitly; canonical printing and
both selector adapters retain origins through Scheduler IR emission.

Text loaders perform resolution only when given `syntax::IncludeOptions::resolver`.
The callback receives the including identity and requested path, and returns an
owning `ResolvedSource` with a stable name and text. Custom resolvers also supply
the root when passed to the file loader. Default cumulative limits are 16 MiB,
128 documents (counting occurrences), and 32 include levels. Repeated includes
are ordinary declarations and still undergo duplicate validation. The CLI file
path, `limestone_target_load_file`, `limestone_compile_umd_file`, and Python
`compile_umd_file` use the file-loading contract.

Graph properties expose block, register class, origin, memory/effects, control
flow, and block targets. Unknown semantic graph properties, unsupported `where`
predicates, and string graph arguments are rejected pending adapters.

### Declarative operand legality

Unisel and Limeburg share Metacode's normalized `OperandConstraint` contract.
Bind a covered operator with `binding name`, or use a boundary's `?name`, then
attach an ordered conjunction of closed predicates:

```text
pattern scaled: add(?base:i64, const():i64 binding imm):i64 -> ADDI4
  where { constraints = [
    { kind = signed_bits; operand = imm; value = 8; },
    { kind = multiple_of; operand = imm; value = 4; }
  ]; };
```

`same_value` / `different_value` take `operand` and `other` binding names and
compare source identities, independent of eventual physical registers.
`immediate_eq`, `immediate_ne`, `immediate_lt`, `immediate_le`, `immediate_gt` and
`immediate_ge` compare a proven constant to signed 64-bit `value`. `multiple_of`
requires a positive divisor. `signed_bits` / `unsigned_bits` require widths 1–64;
`power_of_two` requires a positive constant and has no `value` argument. Unknown
constants do not prove immediate predicates. Invalid names, fields, argument
shapes and bounds fail during loading/validation. Canonical UMD printing retains
bindings and constraints. Infobank `tooling.instruction_selection.where` consumes
the same object. This specifies operand legality; it does not by itself establish
an instruction's semantic equivalence or encode its immediate bits.

## Graph/effect invariants

Required operations must be covered once by legal patterns. `required=false`
denotes external input values. Shared internal values cannot disappear inside a
pattern if another consumer needs them. Side-effect patterns explicitly opt in,
and emitted instructions retain source memory/control/trap semantics.
`prepare` establishes observable effect ordering before optimization/fusion.

SSA value edges are acyclic; CFG cycles are supported separately. Cross-block
definitions dominate uses. Selection stays in the source block, preserving CFG
layout and successor targets. Phi lowering and calling-convention transfers are
explicit adapters. `validate_cfg` and emission reject unsupported dataflow or
control semantics instead of relying on the scheduler to repair them.

The top-level opaque C graph/target APIs are in `limestone/limestone.h`.
Standalone handles in `limestone/il.h` link only `Limestone::il`:

```c
limestone_unisel_document *document = limestone_unisel_load(source, "graph.umd", &error);
limestone_selection_model *model = limestone_unisel_analyze(document, &error);
limestone_unisel_document_destroy(document);
/* Inspect candidates, covered/input/output values, source provenance, and signed
   1-based coverage/exclusion clauses before invoking the selected algorithm. */
limestone_selection *selection = limestone_selection_run(model, LIMESTONE_SELECT_GLOBAL, &error);
limestone_selection_model_destroy(model);
/* NULL indicates a structured error; otherwise selection owns its matches and
   the canonical Scheduler IR from limestone_selection_text(selection). */
limestone_selection_destroy(selection);
```

Check each returned handle before use. Models and results are independent owning
snapshots. Analysis succeeds for a well-formed uncovered graph and exposes empty
coverage clauses; solving that model returns `LIMESTONE_UNSATISFIABLE`. Global
selection minimizes total pattern cost; greedy selection may miss a feasible
covering. Candidate order is deterministic; selected matches use emission order.
The handoff preserves effects/CFG/operands, while timing, allocation and encoding
are downstream target contracts. Python provides context-managed
`UniselDocument`, `SelectionModel` and `Selection` wrappers with copied inspection
data. The installed C consumer verifies this API without linking the core.

Limeburg
shares the UMD contract through `Limestone::limeburg_umd`, with an independent
BURS algorithm.
