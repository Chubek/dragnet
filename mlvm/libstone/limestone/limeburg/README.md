# Limeburg

Limeburg is the independent BURS/tree selector. `limeburg.hpp` exposes typed
nodes, nested patterns, validated rules, dynamic-programming derivations, and
Scheduler IR emission. Link `Limestone::limeburg`; UMD and graph adaptation lives
in `target.hpp` / `Limestone::limeburg_umd`.

```cpp
auto selection = limestone::limeburg::select(nodes, root, rules, "reg");
if (!selection) return selection.error();
auto region = limestone::limeburg::emit_scheduler(nodes, rules, selection.value());
```

The selector minimizes additive checked integer costs. Equal costs are resolved
by rule priority and stable rule ID. Derivations retain rule IDs, child
nonterminals, covered node identities, and costs. Rules support concrete types,
immediate ranges, repeated bindings, and register classes. Effectful nodes require
explicit effect-capable rules; emitted operations retain their source semantics.
An `external_only` rule materializes graph boundaries without covering an internal
computation.

`analyze(nodes, root, rules, true)` computes every reachable state and records
rule attempts in deterministic postorder/priority/ID order. States use stable
nonterminal IDs and retain the cheapest derivation; attempts include costs,
state updates and precise type, range, binding, effect or child-derivation
rejections. The default `false` omits attempt tracing. `print_analysis` dumps
states and attempts with rule origins. Selection failures report available
nonterminals and candidate rejection reasons even when no root derivation exists.

The owning `limestone_burs_analyze` C handle exposes state/attempt inspection;
Python `BURSDocument.analyze()` returns a context-managed `BURSAnalysis` with
copied `states` and `attempts`. Analysis is available for well-formed trees that
selection cannot cover.

## Textual rules

`text.hpp` / `Limestone::limeburg_text` separates syntax loading from selection.
The authoritative grammar is `parsers/limeburg.g`.

```text
ruleset scalar {
  nonterminal reg = 0;
  terminal ADD(2);
  terminal CONST(0);
  rule 1 reg: CONST():i64 -> MOVI cost 2;
  rule 2 reg: ADD(reg, reg):i64 -> ADD cost 2;
  rule 3 reg: ADD(reg, CONST():i64[-8..7]):i64 -> ADDI cost 1;
}
tree sum {
  node %10 = CONST():i64 immediate 100;
  node %20 = CONST():i64 immediate 7;
  node %1 = ADD(%10, %20):i64;
  root %1:reg;
}
```

`load_rules` resolves terminals/nonterminals and validates arities, identities,
types, ranges, and rule costs. `print_rules` canonically serializes the document.
`load_rules_file(path)` expands `include "relative.rules";` declarations in order.
Top-level includes may contain rules and trees; includes inside a ruleset contain
nonterminals, terminals, and rules. Nested file paths are relative to their
including file. Cycle detection uses canonical identities, including symlinks.
The complete document graph defaults to 16 MiB, 128 document occurrences, and
32 include levels. Custom `syntax::IncludeOptions` configure these limits and an
owning resolver for virtual sources. Text loading requires an explicit resolver
to expand includes. Included rule/node origins survive canonical printing and
Scheduler IR emission; `origin "..."` provides an explicit provenance override.

The CLI file path, `limestone_burs_load_file`, and Python
`BURSDocument.from_file` use the same contract. Richer memory contracts require
an effect adapter.

```sh
build/limestone-cli --select-burs tests/fixtures/selection.limeburg
build/limestone-cli --analyze-burs tests/fixtures/selection-includes.limeburg
```

The standalone C document/selection API is in `limestone/il.h`; its selection
text is a canonical Schedrow handoff. Target timing can be supplied to the next
stage through its machine model.

## Infobank specifications

[`specs/`](specs/README.md) contains generated `.lburg` files for all 23
architectures in the Infobank manifest, with instruction-scoped semantic trees,
register classes, explicit immediate/alignment legality, costs, provenance, and
per-instruction coverage records. Regenerate with
`cmake --build build --target limeburg-regenerate-specs`; verify with
`build/limeburg-generate-specs --check metacode/infobank limeburg/specs`.
`infobank.hpp` / `Limestone::limeburg_infobank` supplies the owning C++ generation
boundary, retaining the normalized UMD alongside the rules. Explicit selection
trees describe generic source computations; inventory roots retain target
instruction identity and depend on their source/target adapters' effect and ABI
contracts. See the specification guide for naming and supported coverage.

## Declarative operand legality

Rules accept the same declarative operand predicates as Unisel:

```text
rule reg: ADD(reg binding base, CONST() binding imm) -> ADDI4
  where { constraints = [
    { kind = signed_bits; operand = imm; value = 8; },
    { kind = multiple_of; operand = imm; value = 4; }
  ]; };
```

See [Unisel's operand-legality contract](../unisel/README.md#declarative-operand-legality)
for predicate names and argument shapes. Conditions filter derivations before
cost selection and appear in rejection traces. UMD adaptation, direct BURS rules
and canonical printing retain them. Forest boundaries preserve a proven constant
as `known_constant` while keeping its use a register operand; a boundary cannot
be silently folded into an immediate. Textual `known_constant N` is valid only on
non-immediate external leaf values.

## Shared values and CFGs

Ordinary `select_graph` defaults to `GraphPolicy::TreeOnly`, rejecting shared
internal computations. `GraphPolicy::PreserveShared` splits a graph into a forest
at shared/observable boundaries and retains one materialized value. The top-level
pipeline uses that policy. It does not silently duplicate shared computations or
claim a globally optimal DAG cover.

The graph adapter preserves source blocks, dependencies, control flow, and live
outputs. Pattern matching cannot fuse across CFG/effect boundaries without a legal
adapter. No physical register or issue cycle is assigned by BURS.
