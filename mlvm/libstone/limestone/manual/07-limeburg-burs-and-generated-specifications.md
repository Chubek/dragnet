# Chapter 7. Limeburg BURS and Generated Specifications

[Previous: Unisel](06-unisel-global-instruction-selection.md) · [Contents](README.md) · [Next: Scheduling](08-schedrow-instruction-scheduling.md)

## 7.1 Why a second instruction selector exists

Limeburg implements bottom-up rewrite-system instruction selection, commonly
called BURS. It computes minimum-cost derivations over typed source trees through
dynamic programming. This gives a deterministic tree selector independent of
Unisel's global candidate/constraint solving.

Both selectors can consume normalized UMD knowledge and emit Scheduler IR.
They share types, legality predicates, and downstream contracts; they retain
different algorithms. Choosing BURS does not invoke Satie. Choosing global Unisel
does not build BURS state tables.

The core API is [limeburg/limeburg.hpp](../limeburg/limeburg.hpp), with text loading
in [text.hpp](../limeburg/text.hpp), UMD/graph adaptation in
[target.hpp](../limeburg/target.hpp), and Infobank generation in
[infobank.hpp](../limeburg/infobank.hpp).

## 7.2 Terminals, nonterminals, and rules

A terminal is a source-tree operation such as `ADD` or `CONST`. A nonterminal is
a derivation category such as `reg`, `addr`, or `value`. A rule states that a
particular source pattern can derive a category while emitting a target
instruction at a stated cost.

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

`CONST()` is an explicit nullary application. A bare nonterminal such as `reg`
is a boundary requiring a child derivation. The textual loader distinguishes
these forms and validates declared arities, stable IDs, types, ranges, and costs.
Rule IDs are explicit observable identities, not array positions.

## 7.3 Bottom-up dynamic programming

For each reachable node and nonterminal, the selector retains the least-cost
legal derivation. Conceptually:

```text
cost(node, category) = minimum over applicable rules of
    rule_cost + sum(required child derivation costs)
```

For the example, both constants can derive `reg` at cost two. The general root
addition costs `2 + 2 + 2 = 6`. The immediate rule consumes the right constant
inside its pattern, leaving only the left register derivation, so its total is
`1 + 2 = 3`. The selected instruction sequence materializes `100` and emits
`ADDI` with immediate `7`.

The right constant's own `reg` state still exists, but is unnecessary in the
chosen immediate derivation. A state table contains all reachable best category
derivations; a selection contains only those followed from the requested root.

Costs use checked additive integer arithmetic. Equal totals are resolved by rule
priority and stable rule ID. Priority is secondary to cost; it cannot make an
expensive form win solely because of preference.

## 7.4 Structured rules and legality

Rules may contain nested operator patterns, concrete types, immediate ranges,
named bindings, register-class restrictions, effect capability, and shared
declarative constraints. Repeated bindings enforce source identity just as they
do in Unisel.

```text
rule reg: ADD(reg binding base, CONST() binding imm) -> ADDI4
  where { constraints = [
    { kind = signed_bits; operand = imm; value = 8; },
    { kind = multiple_of; operand = imm; value = 4; }
  ]; };
```

The pattern must first match structurally, then satisfy its operand conditions,
then obtain the required child categories. Only after those obligations succeed
does its cost compete for the state.

An effectful node requires an explicitly effect-capable rule. Memory, calls,
traps, and control boundaries cannot disappear merely because a pure derivation
is cheaper. Rich memory/address contracts may require a source adapter that
preserves those effects in emitted Scheduler IR.

`external_only` rules materialize graph boundaries without covering internal
computations. They are useful for preexisting values, and cannot be used as a
zero-cost escape hatch for a required computation.

## 7.5 Analysis before selection

Given host-supplied nodes, root, and rules:

```cpp
auto states = limestone::limeburg::analyze(nodes, root, rules, true);
if (!states) return report(states.error());
auto listing = limestone::limeburg::print_analysis(states.value(), rules);

auto choice = limestone::limeburg::select(nodes, root, rules, "reg");
if (!choice) return report(choice.error());
auto region = limestone::limeburg::emit_scheduler(nodes, rules, choice.value());
```

This fragment assumes a host `report` function. `analyze(...,true)` records rule
attempts in deterministic postorder, priority, and ID order. A state retains rule,
cost, child requirements, boundary children, and covered nodes. A `RuleAttempt`
records whether matching succeeded, its cost when available, whether it improved
the state at that point, and a rejection reason.

`improves_state` is a historical update flag. A later rule may replace that state,
and a final root derivation may not use it. It does not mean the attempt belongs
to the final selected program.

Analysis remains useful when the requested root category has no derivation.
Malformed trees still fail structural validation, but well-formed unselectable
trees retain states and rejection explanations.

## 7.6 Explaining missing derivations

Run:

```sh
build/limestone-cli --analyze-burs tests/fixtures/selection.limeburg
build/limestone-cli --select-burs tests/fixtures/selection.limeburg
```

When a rule cannot apply, inspect its type, immediate range, register class,
repeated binding, effect capability, declarative predicate, and child category.
The deepest missing child derivation is often more informative than the root's
failure.

For example, a root `ADD(reg,imm)` may match structurally while its right child
has only `reg` states. Adding a cheaper root rule does not help; the rule system
needs a legal `imm` derivation or an inline constant pattern. A wrong source type
or class likewise needs a justified adapter or pattern correction.

Owning C analysis handles expose states/attempts in `limestone/il.h`; Python's
`BURSAnalysis` provides copied lists. Both can outlive their source document.
This is useful for IDE diagnostics and rule-set development tools.

## 7.7 Trees, DAGs, and shared-value forests

Ordinary BURS dynamic programming is tree-oriented. `select_graph` therefore
defaults to `GraphPolicy::TreeOnly`, rejecting shared internal computations.
Silent duplication could change effects or increase cost in ways the tree
objective does not model.

`GraphPolicy::PreserveShared` splits a source graph into a forest at shared or
observable boundaries. A computation is materialized once and referred to by
other trees. The top-level pipeline uses this policy by default. It provides
sharing-preserving selection, rather than claiming a globally optimal DAG cover.

Forest boundaries preserve a known constant as `known_constant` while retaining
its register use. A predicate can use the proven value, but a rule cannot fold
that boundary into an immediate as though it covered the original constant node.
Textual `known_constant N` is restricted to non-immediate external leaves.

CFG boundaries and observable effects are also preserved. Fusion does not cross
blocks or forest cuts. Outputs, dependencies, block targets, and provenance survive
the adapter into Scheduler IR.

## 7.8 Text documents, includes, and canonical printing

`load_rules` constructs a `RuleDocument` with rules and optional input trees.
`load_rules_file` expands top-level or ruleset-local includes. A ruleset-local
include supplies nonterminals, terminals, and rules; a top-level include can also
supply trees. Relative paths follow the including file.

The default source graph budgets are 16 MiB, 128 document occurrences, and
32 include levels. File cycles use canonical identities. Text loading requires
an explicit resolver to expand includes. `origin "..."` preserves a supplied
source identity through canonical `print_rules` and selection emission.

These semantics are shared by the CLI, C file loader, and Python
`BURSDocument.from_file`. Canonical output is a reproducibility artifact: reload it
and compare normalized rules/trees rather than relying on incidental whitespace.

## 7.9 Infobank-derived specifications

The repository contains one generated `.lburg` file for each manifest architecture
under [limeburg/specs](../limeburg/specs/README.md). The current corpus contains
3,395 inventory instructions, 1,935 instruction rules, and 1,460 recorded coverage
boundaries. The coverage report explains the disposition of each instruction.

There are two distinct generation modes:

1. An explicit `tooling.instruction_selection.selection_tree` becomes a declared
   generic source computation, preserving types, classes, costs, and predicates.
2. An inventory-derived rule has an instruction-qualified `ISA_<instruction>`
   root. Its source adapter establishes ISA context, availability, and semantics.

Inventory roots prevent short semantic hints from being mistaken for unrestricted
generic equivalences. Nested semantic applications become `SEM_<operator>_<arity>`
terminals. Symbol and string literals use distinct `TAG_symbol_*` and
`TAG_string_*` leaves. Non-alphanumeric bytes are reversibly escaped as `_hh`,
including an underscore as `_5f`; names cannot collide by dropping punctuation.

`value` represents a single register/SSA result; `stmt` represents an operation
without one. Stack-machine statements retain observable stack state through their
source adapter. `INPUT()` can derive only a legal external value with matching
class. `CONST()` carries a signed-64-bit immediate. Split immediate fields retain
logical width and required low-bit alignment through predicates.

Inventory rule IDs are one-based source instruction positions, with gaps for
unsupported instructions. External-value rules start at 65,536. Instruction-count
cost defaults to one; explicit selection cost overrides it. External values have
zero cost. These costs do not replace scheduling latency or throughput.

## 7.10 Generation API and freshness

```sh
cmake --build build --target limeburg-regenerate-specs
build/limeburg-generate-specs --check metacode/infobank limeburg/specs
ctest --test-dir build -R limeburg-specs --output-on-failure
```

The generator uses Metacode, semantic parsing, normalized UMD, and the existing
UMD-to-BURS adapter. Outputs pass the actual GLR loader and canonical round trip
before publication. Manifest identities, counts, and classes are validated.
`--check` performs no writes.

The C++ API `from_infobank(architecture)` returns an owning `InfobankSpec` with
the normalized machine, rule document, and `InstructionCoverage` records.
`print_infobank_spec` produces the generated text. Link
`Limestone::limeburg_infobank` to use this boundary.

## 7.11 Coverage as target-development information

Coverage boundaries include absent immediate signedness, multiple immediates
without per-operand contracts, contradictory semantic/dataflow reads, memory
address operands needing an adapter, multiple results, and virtual-ISA type/result
identities that need typed lowering. They are inspectable work items, rather than
silently approximated rules.

A rule count alone is not a native backend completion measure. Selection still
needs source effects, downstream timing, storage, encoding, and ABI contracts.
Use the runnable RISC-V example to understand qualified inventory input:

```sh
build/limestone-cli --select-burs \
  limeburg/specs/examples/riscv64-addi.limeburg
build/limestone-cli --analyze-burs \
  limeburg/specs/examples/riscv64-addi.limeburg
```

When extending coverage, edit the authoritative metadata or generation adapter,
regenerate, and add positive/negative legality checks. Keep rule provenance so a
selection decision remains traceable to its original instruction contract.

[Previous: Unisel](06-unisel-global-instruction-selection.md) · [Contents](README.md) · [Next: Scheduling](08-schedrow-instruction-scheduling.md)
