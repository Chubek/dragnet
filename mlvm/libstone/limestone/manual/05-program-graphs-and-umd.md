# Chapter 5. Program Graphs and Universal Machine Descriptions

[Previous: Metacode](04-metacode-and-the-infobank.md) · [Contents](README.md) · [Next: Unisel](06-unisel-global-instruction-selection.md)

## 5.1 Two inputs to instruction selection

Instruction selection combines a source computation with a description of legal
target implementations. Limestone represents these inputs separately:

- `unisel::Program` contains nodes, outputs, explicit dependencies, basic blocks,
  and the entry identity.
- `unisel::MachineDescription` contains operators, instructions, patterns,
  registers, classes, aliases, machine metadata, and provenance.

The textual Universal Machine Description, or UMD, can contain one machine and
an optional program. It is useful for reproducible experiments, shared target
contracts, standalone selection, and complete pipeline input. Loading UMD does
not invoke a solver. The semantic loader validates and constructs owning objects
that the selection algorithms consume later.

The APIs are [unisel/unisel.hpp](../unisel/unisel.hpp) and
[unisel/umd.hpp](../unisel/umd.hpp). The authoritative text syntax is
[parsers/unisel.g](../parsers/unisel.g).

## 5.2 Node and value identity

A source node has a stable numeric ID, an operation name, input IDs, an optional
signed-64-bit constant, and a concrete type string. Additional properties describe
block membership, register class, provenance, effects, and control flow. A node
can produce a value, perform an observable statement, or represent an external
input.

For a simple arithmetic graph:

```cpp
limestone::unisel::Program program;
program.nodes = {
    {1, "const", {}, 20, "i64"},
    {2, "const", {}, 22, "i64"},
    {3, "add", {1, 2}, std::nullopt, "i64"}
};
program.outputs = {3};
```

This fragment assumes the public Unisel header is included. IDs are references,
not vector offsets. A node with ID `100` can precede one with ID `7` in the vector;
algorithms validate the identities and use stable ordering rules. Duplicate IDs,
missing inputs, and cyclic SSA dependencies are invalid.

`produces_value=false` distinguishes statements from values. Returns and branches
can be required without creating an allocatable result. An external input uses
`required=false`; it supplies a value boundary rather than requesting an
instruction that computes it.

## 5.3 Required operations and outputs

`required` determines which computations must be covered. It should not be used
as an informal hint that a computation may be deleted. External nodes represent
values supplied by another boundary, such as arguments or previously materialized
regions. Required internal nodes request target implementation.

Program outputs retain observable value boundaries. A pattern that consumes an
internal computation cannot hide its result when another consumer or output still
needs that value. This is central to correct DAG covering and to BURS forest
construction.

For example, if `%2 = add(%a,%b)` feeds both `%3` and `%4`, a fused candidate
covering `%2` inside `%3` is legal only if it preserves the needed `%2` boundary.
Otherwise `%4` would read a value that no selected instruction defines. Candidate
outputs and boundary-definition constraints make this obligation explicit.

## 5.4 A complete UMD machine and program

```text
machine scalar {
  regclass G = [$r0, $r1, $r2];
  operator const(0);
  operator add(2);
  instruction CONST { latency = 0; }
  instruction ADD { latency = 1; }
  instruction ADDI { latency = 1; }
  pattern constant: const():i64 -> CONST cost 2;
  pattern sum: add(?lhs:i64, ?rhs:i64):i64 -> ADD cost 2;
  pattern small_sum: add(?lhs:i64, const():i64[-8..7]):i64 -> ADDI cost 1;
  default_register_class = G;
}
program main {
  node %10 = const(100):i64;
  node %20 = const(7):i64;
  node %1 = add(%10, %20):i64;
  output %1;
}
```

Operators declare source arity. Instructions declare target identities and
machine-facing metadata. Patterns associate typed source trees with target
instructions and costs. The graph's `const(100)` notation attaches a constant to
a nullary source operation; it does not change the declared `const(0)` arity.

The cheaper immediate pattern covers the right constant inside `ADDI`, leaving
the left value as a boundary input. If the constant does not satisfy the range,
the general addition remains an alternative. Exact choices depend on complete
coverage and shared-value legality, not just a pattern's local cost.

## 5.5 Types, boundaries, and repeated bindings

A structured pattern distinguishes covered operators from boundary operands.
`?lhs:i64` means a boundary value of type `i64`. `const():i64` means an internal
constant operation covered by the pattern. Named bindings can be repeated to
require the same source identity in more than one position:

```text
pattern double: add(?x:i64, ?x:i64):i64 -> DOUBLE;
```

This is different from matching two equal integer constants stored in different
source nodes. Repetition constrains identity. Declarative `same_value` and
`different_value` predicates expose the same relation explicitly.

Flat `Pattern::operands` entries name concrete source types. An empty entry or
`v` represents an untyped boundary. `pattern_tree` normalizes flat and structured
forms to the same matching contract; canonical UMD output and BURS adaptation use
that normalization.

Register-class constraints refine a typed boundary or covered result. They state
which storage category is acceptable, while physical register assignment remains
downstream. Source type and register class are distinct properties: an `i64` value
may have several possible target storage classes.

## 5.6 Declarative predicates

Bind the relevant covered operand before referring to it in a constraint:

```text
pattern scaled: add(?base:i64, const():i64 binding imm):i64 -> ADDI4
  where { constraints = [
    { kind = signed_bits; operand = imm; value = 8; },
    { kind = multiple_of; operand = imm; value = 4; }
  ]; };
```

The `imm` binding names the constant node consumed by the pattern. Each condition
must be proven before the candidate is admitted. Neither a cheap cost nor a later
allocator can make an illegal immediate legal.

Constraints are deliberately closed and declarative. Arbitrary textual functions
inside `where` are not executed. Unrecognized predicates or malformed fields are
errors, and unknown constant contents do not satisfy numeric predicates. This
keeps matching deterministic across both instruction selectors.

## 5.7 Effects and preparation

Source nodes retain `side_effect`, `call`, `terminator`, `may_trap`, optional memory
access, control classification, and direct block targets. A pattern that covers
an effectful node must explicitly support side effects. Emission preserves source
effects rather than reconstructing them from target mnemonic spelling.

The supported memory model includes read/write, volatility, atomicity, ordering,
address space, alias sets, byte size, and alignment. Unknown alias information
is handled conservatively by the dependency model. Explicit disjoint alias
information allows the downstream scheduler to distinguish independent accesses.

Call `prepare(program)` before independent matching/solving or graph optimization
when you need the same prepared effect ordering used by the pipeline:

```cpp
auto prepared = limestone::unisel::prepare(program);
if (!prepared) {
  // prepared.error() identifies a structural/effect failure.
}
```

Preparation establishes observable effect order before fusion or rewriting can
erase distinctions. Semantic dependencies remain pinned through the typed Tunah
adapter. A scheduler-only dependency is a separate artificial scheduling
restriction; the distinction survives handoff.

## 5.8 CFGs and dominance

`Program::blocks` contains stable block IDs, names, successors, and explicit
live-outs. Nodes identify their source block. Entry is the first declared block,
and declaration order defines layout. Cross-block values must be available on
every path to their use, normally through dominance.

A conceptual diamond can be declared in the C++ model as:

```cpp
program.blocks = {
    {10, "entry", {30, 20}, {}},
    {20, "fallthrough", {40}, {}},
    {30, "taken", {40}, {}},
    {40, "exit", {}, {}}
};
program.entry = 10;
```

This fragment supplies block structure only. The host must also create the nodes,
assign their block IDs, and add matching legal terminators and target lists.
For a conditional branch, target `0` is taken and target `1` is fallthrough;
the latter agrees with the next layout block. Successor metadata alone does not
implement a branch instruction.

The SSA dataflow graph remains acyclic even when the CFG has a loop. Phi lowering,
loop-carried value representation, calling-convention moves, and exceptional
control semantics require explicit adapters. Selection cannot silently fuse
computations across blocks or use scheduling to repair invalid dominance.

## 5.9 Includes and source identity

File-based loading supports relative top-level includes:

```text
include "target.umd";
include "program.umd";
```

Included declarations are expanded in order and validated as a complete document.
The default graph limits are 16 MiB total source text, 128 document occurrences,
and 32 include levels. Repeated includes count as repeated declarations and may
cause duplicate-definition errors. Canonical path identity, including symlinks,
is used to detect file cycles.

`load_umd_file` opts into filesystem resolution. The text loader expands includes
only with an explicit `syntax::IncludeOptions::resolver`. A resolver returns
owning source name/text, so virtual documents can be used without retaining
temporary input storage. When supplied to the file API, it also resolves the
root document. Chapter 20 describes the shared source-loading machinery.

An `origin "..."` clause overrides pattern provenance explicitly. Original
included file locations otherwise survive normalization, printing, and Scheduler
IR emission. Retain those identities when constructing a virtual resolver.

## 5.10 Loading, printing, and handing off

```cpp
auto document = limestone::unisel::load_umd_file(
    "tests/fixtures/arithmetic-includes.umd");
if (document && document.value().program) {
  auto target = limestone::make_target(document.value().machine);
  // Run the pipeline with the owned graph and target after checking target.
}
```

This fragment additionally assumes the pipeline header is included. `print_umd`
canonically prints the machine description; it is not a general serializer for
an arbitrary mutated source graph. Unknown machine metadata remains available in
the owning normalized model. Unknown semantic graph properties and unsupported
string operands are rejected because their behavior cannot be inferred.

Before a handoff, verify IDs, arities, source types, required coverage, outputs,
effects, block layout, dominance, and control targets. A valid source graph plus
a well-formed pattern set can still have no legal cover. Chapter 6 explains how
to inspect that selection model and distinguish missing candidates from
incompatible global choices.

[Previous: Metacode](04-metacode-and-the-infobank.md) · [Contents](README.md) · [Next: Unisel](06-unisel-global-instruction-selection.md)
