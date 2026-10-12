# AGENTS.md

# Limeburg

Limeburg is a **BURS-based instruction-selection framework**.

Its purpose is to provide a production-oriented C/C++ implementation of bottom-up rewrite-system instruction selection for compiler backends.

Limeburg consumes a machine description derived from the project's machine/instruction metadata and selects target instructions by matching and rewriting trees.

The conceptual model is:

```text
Source IR
   |
   v
Selection Tree / DAG
   |
   v
BURS Pattern Matching
   |
   v
State Computation
   |
   v
Cost / Rule Selection
   |
   v
Selected Machine Operations
   |
   v
Scheduler IR / Machine IR
```

Limeburg is deliberately distinct from `Unisel`.

```text
Limeburg
    = BURS / dynamic-programming instruction selection

Unisel
    = graph-based / SAT/SMT instruction selection
```

The two projects may share machine-description infrastructure, parser infrastructure, semantic representations, and downstream IRs, but their selection algorithms must remain independently implementable.

---

# 1. Core Design

Limeburg implements a **Bottom-Up Rewrite System (BURS)** instruction selector.

The essential operation is:

```text
source tree
    |
    v
bottom-up state computation
    |
    v
rule matching
    |
    v
minimum-cost rule selection
    |
    v
target instruction tree
```

A BURS selector should not require a SAT/SMT solver for its normal operation.

The normal selection algorithm should be polynomial in the size of the input tree for a fixed rule system.

---

# 2. Architectural Pipeline

The intended architecture is:

```text
                    metacode Infobank
                           |
                           v
                  Universal Machine
                   Description (.umd)
                           |
                           v
                  Limeburg Rule Set
                           |
                           v
Source IR ---> Selection Tree/DAG ---> BURS Matcher
                                      |
                                      v
                                State Tables
                                      |
                                      v
                                Rule Selection
                                      |
                                      v
                             Selected Operations
                                      |
                                      v
                                Scheduler IR
                                      |
                                      v
                                  Scheduler
```

The machine description is target-specific.

The BURS algorithm is target-independent.

---

# 3. BURS Terminology

Use the following terminology consistently.

## Tree

The input computation being selected.

Example:

```text
        add
       /   \
    load   const
      |
     addr
```

## Nonterminal

An abstract category representing a class of trees.

Examples:

```text
reg
gpr
fpr
addr
imm
mem
```

## Terminal

A source-tree operation that acts as an input symbol.

Examples:

```text
ADD
LOAD
CONST
MUL
CALL
STORE
```

## Rewrite rule

A rule transforming a source-tree pattern into a target nonterminal or instruction.

Conceptually:

```text
reg: ADD(reg, reg)
    -> ADD
```

or:

```text
reg: ADD(reg, CONST)
    -> ADDI
```

## Cost

The cost assigned to a rule.

Costs may represent:

```text
instruction count
code size
latency
target-specific cost
```

Do not assume that every cost is simply `1`.

---

# 4. Selection Tree

Limeburg's primary selection representation is a tree.

A source expression such as:

```text
a + b * c
```

may be represented as:

```text
      ADD
     /   \
    a    MUL
        /   \
       b     c
```

The BURS algorithm computes states bottom-up.

Each node receives a state describing the cheapest way to derive every relevant nonterminal.

Conceptually:

```text
state(node) = {
    cost(reg),
    cost(gpr),
    cost(fpr),
    cost(addr),
    ...
}
```

---

# 5. DAG Handling

BURS is fundamentally tree-oriented.

Limeburg must therefore make the treatment of DAGs explicit.

Possible policies include:

```text
tree-only
treeification
controlled duplication
DAG-preserving extensions
```

The default implementation should not silently duplicate shared computations.

If a source DAG contains:

```text
       x
      / \
     A   B
      \ /
       C
```

the selector must know whether:

```text
x
```

may be duplicated.

This must be represented as an explicit lowering/selection policy.

---

# 6. Tree Construction

The source IR must be lowered into a selection tree representation.

Do not make the BURS engine depend directly on a high-level compiler IR.

Use an adapter:

```text
Source IR
   |
   v
Selection Tree Builder
   |
   v
Limeburg Tree
```

This keeps the selector independent from any particular compiler IR.

---

# 7. Selection Tree API

The core tree representation should support:

```text
node ID
operator
children
parent
source value
source operation
type
attributes
metadata
```

Conceptually:

```cpp
class TreeNode {
public:
    Opcode opcode() const;
    std::span<TreeNode* const> children() const;
    TreeNode* child(size_t index) const;

    Type type() const;

    NodeID id() const;
};
```

The C API should use opaque handles.

---

# 8. Typed Selection

Selection trees may be typed.

Types may affect rule applicability:

```text
i8
i16
i32
i64
f32
f64
vector
pointer
```

Do not force all type information into opcode names.

Prefer explicit type information where the target description requires it.

---

# 9. Nonterminals

Nonterminals represent target-independent categories required by the rewrite system.

Examples:

```text
reg
gpr
fpr
vreg
addr
mem
imm
cond
```

The exact set is target-defined.

A nonterminal should have a stable identifier.

Do not use strings as the primary runtime representation.

Strings may be used during parsing/debugging.

---

# 10. Rewrite Rules

A rule should conceptually contain:

```text
rule ID
lhs nonterminal
operator/pattern
child nonterminals
result nonterminal
target instruction
cost
constraints
metadata
```

Example:

```text
reg:
    ADD(reg, reg)
    -> ADD
    cost 1
```

and:

```text
reg:
    ADD(reg, const8)
    -> ADDI
    cost 1
```

The second rule is preferable only when the constant satisfies the target's immediate constraints.

---

# 11. Rule Representation

Internally, represent rules as structured objects.

Conceptually:

```cpp
struct Rule {
    RuleID id;

    Nonterminal lhs;

    Operator opcode;

    std::span<const Nonterminal> operands;

    Nonterminal result;

    Cost cost;

    InstructionID instruction;

    RuleConstraints constraints;
};
```

Do not encode complete rules as opaque strings.

---

# 12. Rule Compiler

If the textual machine description describes rewrite rules declaratively, compile it into an internal BURS rule database.

The pipeline should be:

```text
UMD / rule description
        |
        v
Rule parser
        |
        v
Rule semantic validation
        |
        v
Rule normalization
        |
        v
BURS rule database
        |
        v
Runtime selector
```

The runtime selector should not repeatedly parse textual rules.

---

# 13. Rule Validation

Before a rule set is usable, validate:

```text
operator existence
operand count
nonterminal existence
result nonterminal
instruction existence
cost validity
type compatibility
constraint validity
```

Detect duplicate or contradictory rules where possible.

Malformed rule systems should fail during rule compilation rather than during selection.

---

# 14. Dynamic Programming

The core BURS algorithm computes the optimal state bottom-up.

For a node:

```text
n = op(c1, c2, ..., ck)
```

the state for `n` is computed from:

```text
state(c1)
state(c2)
...
state(ck)
```

For every applicable rule:

```text
N -> op(N1, N2, ...)
```

compute:

```text
cost(rule)
+
cost(N1, child1)
+
cost(N2, child2)
+
...
```

and retain the cheapest derivation.

Conceptually:

```text
cost[n][N] =
    min(
        rule.cost +
        Σ cost[child][required_nonterminal]
    )
```

---

# 15. State Representation

A node's BURS state should contain at least:

```text
nonterminal
best cost
best rule
child requirements
```

Conceptually:

```cpp
struct StateEntry {
    Nonterminal nonterminal;
    Cost cost;
    RuleID rule;
};
```

The implementation may use compact tables for performance.

---

# 16. State Computation

State computation must be deterministic.

Given:

```text
tree
rule set
cost model
```

the state computation should produce the same result regardless of pointer ordering or hash iteration.

Tie-breaking must be explicit.

For example:

```text
lower cost
then lower rule ID
```

or another documented deterministic policy.

Do not rely on container iteration order.

---

# 17. Rule Selection

After state computation, select the desired root nonterminal.

For example:

```text
root -> reg
```

The root state gives the optimal derivation.

Then recursively follow:

```text
selected rule
    |
    +-- required child nonterminal
    +-- required child nonterminal
    ...
```

until all leaves have been lowered.

---

# 18. Backtracking

The selected derivation must be recoverable from the state table.

Do not recompute arbitrary rule matches during backtracking unless there is a demonstrated performance reason.

Store enough information in each selected state to reconstruct:

```text
rule
child nonterminal choices
target instruction
source node
```

---

# 19. Costs

Costs should be abstract.

At minimum support:

```text
integer cost
```

The architecture should permit later extension to:

```text
multi-dimensional costs
lexicographic costs
target-specific cost objects
```

Do not bake machine latency into the BURS engine itself.

The rule or target description provides the cost.

---

# 20. Cost Model

Potential cost dimensions include:

```text
instruction count
code size
latency
throughput
register pressure
target-specific instruction cost
```

The first implementation may use scalar integer costs.

The API should avoid preventing richer cost models later.

---

# 21. Ties

Multiple rules may have equal cost.

The selector must have deterministic tie-breaking.

Prefer an explicit policy such as:

```text
1. lower total cost
2. lower rule priority
3. lower rule ID
```

Do not select based on:

```text
pointer address
unordered_map order
allocation order
```

---

# 22. Immediate Operands

Immediate constants are common BURS selection patterns.

Rules must support constraints such as:

```text
signed 8-bit
unsigned 12-bit
power of two
shift amount
scaled immediate
target-specific encoding range
```

Conceptually:

```text
ADDI(reg, imm8)
```

must not match:

```text
ADD(reg, arbitrary_constant)
```

unless the immediate satisfies the rule's constraint.

---

# 23. Addressing Modes

Address expressions should be representable as trees.

Example:

```text
base + index * scale + displacement
```

may be represented as:

```text
        ADD
       /   \
     ADD   disp
    /   \
  base  MUL
        / \
     index scale
```

The target may provide a rule such as:

```text
addr:
    ADD(base, MUL(index, scale))
    -> indexed-address
```

Limeburg should allow target descriptions to express such patterns declaratively.

---

# 24. Complex Instructions

A single target instruction may consume a large source subtree.

For example:

```text
LOAD_ADD
```

may match:

```text
ADD(
    LOAD(addr),
    reg
)
```

The selector should represent this as one selected target instruction rather than forcing an artificial sequence of:

```text
LOAD
ADD
```

when the target provides a combined operation.

---

# 25. Machine Instructions

The selected rule should identify the machine operation emitted downstream.

The selected instruction should retain:

```text
instruction ID
opcode
source node
operands
definitions
uses
implicit operands
machine metadata
```

Do not immediately encode the instruction into bytes.

---

# 26. Scheduler IR

Limeburg should normally emit Scheduler IR rather than final assembly.

The pipeline becomes:

```text
BURS selection
      |
      v
Scheduler IR
      |
      v
Scheduling
      |
      v
Register allocation
      |
      v
Machine encoding
```

This keeps instruction selection separate from scheduling.

---

# 27. Scheduler Metadata

Selected instructions should retain scheduling metadata when supplied by the machine description.

Potential information includes:

```text
latency
throughput
resource requirements
memory effects
side effects
instruction class
```

However, Limeburg must not assign issue cycles.

---

# 28. Register Constraints

BURS rules may specify register classes.

Examples:

```text
GPR
FPR
VR
predicate
special
```

Selection should preserve register-class requirements.

Physical register allocation is downstream.

---

# 29. Fixed Registers

Some instructions require fixed registers.

Examples:

```text
implicit accumulator
special status register
fixed call register
fixed return register
```

Such requirements should be represented in the selected instruction metadata.

Do not silently convert them into ordinary virtual registers.

---

# 30. Implicit Operands

Rules must be capable of representing:

```text
implicit use
implicit definition
clobber
flags
condition codes
special state
```

These are important for downstream scheduling and register allocation.

---

# 31. Memory Operations

Memory instructions must retain:

```text
read/write
address
size
alignment
volatile
atomic
ordering
alias information
```

Do not treat memory operations as ordinary arithmetic trees.

A selected load/store instruction must preserve its memory semantics.

---

# 32. Side Effects

Side-effecting instructions must be explicitly represented.

Examples:

```text
CALL
STORE
ATOMIC
BARRIER
TRAP
IO
```

A BURS rewrite must never remove an observable operation merely because another tree derivation has a lower numerical cost.

---

# 33. Control Flow

The selection framework must distinguish:

```text
expression selection
control-flow selection
```

Branches, calls, returns, and traps may carry control-flow semantics that ordinary expression rules cannot capture.

The selection tree adapter must make these boundaries explicit.

---

# 34. Calls

Call selection may require:

```text
argument locations
return locations
calling convention
clobbers
stack effects
special registers
```

Do not model calls as ordinary arithmetic operators.

The calling-convention layer may be separate, but Limeburg must preserve the information required by it.

---

# 35. BURS Limitations

Do not pretend that BURS solves every instruction-selection problem.

BURS is naturally suited to:

```text
tree-pattern instruction selection
dynamic-programming optimization
local/global expression selection
large rule sets
fast deterministic selection
```

More difficult cases include:

```text
shared DAGs
global instruction interactions
long-range data dependencies
register allocation interactions
complex scheduling interactions
multi-instruction resource constraints
```

Such cases may require preprocessing, treeification, post-selection transformations, or another selector.

Limeburg should expose explicit extension points rather than silently producing incorrect selections.

---

# 36. Parser Toolchain

Limeburg uses the same parser/toolchain conventions as the rest of the project ecosystem.

Use:

```text
Aurocks
GLR parser generation
scripts/third_party/dparser
```

Do not introduce a second parser generator.

If Limeburg has a grammar:

```text
grammar/limeburg.g
```

it is the authoritative grammar source.

Generated parser files are build artifacts.

Never manually patch generated GLR parser code.

---

# 37. Grammar Workflow

When changing Limeburg syntax:

```text
1. Edit grammar/limeburg.g
2. Run scripts/third_party/dparser
3. Regenerate the parser
4. Rebuild
5. Run parser tests
6. Run semantic tests
7. Run BURS-selection tests
```

Keep parser generation reproducible.

---

# 38. Parser / Semantic Separation

The parser should produce syntax.

Semantic processing should validate:

```text
operators
nonterminals
rules
instructions
types
constraints
costs
```

The parser must not perform BURS state computation.

Likewise, the BURS engine must not parse textual syntax.

---

# 39. C API

The C API should use opaque objects.

Conceptually:

```c
typedef struct limeburg_context limeburg_context;
typedef struct limeburg_tree limeburg_tree;
typedef struct limeburg_node limeburg_node;
typedef struct limeburg_rules limeburg_rules;
typedef struct limeburg_rule limeburg_rule;
typedef struct limeburg_state limeburg_state;
typedef struct limeburg_solution limeburg_solution;
typedef struct limeburg_scheduler_ir limeburg_scheduler_ir;
```

The public API should make it possible to:

```text
create context
load rules
construct selection tree
run selection
inspect result
emit Scheduler IR
destroy objects
```

---

# 40. C++ API

The C++ API should provide RAII and stronger type safety.

Preferred conceptual types:

```cpp
namespace limeburg {

class Context;
class Tree;
class Node;
class RuleSet;
class Rule;
class Selector;
class Selection;
class SchedulerIR;

}
```

Use RAII for owned objects.

Do not expose internal BURS tables as the normal public API.

---

# 41. C ABI Boundary

The C API is the ABI boundary.

Do not expose:

```text
C++ templates
STL containers
C++ exceptions
internal BURS tables
parser implementation structures
Aurocks structures
```

through the C ABI.

---

# 42. Error Handling

Errors should distinguish:

```text
parse error
semantic error
invalid rule
invalid tree
unknown operator
unknown nonterminal
invalid cost
no valid derivation
invalid target instruction
Scheduler IR emission error
```

A failed selection should provide enough information to diagnose why no rule derives the requested root nonterminal.

---

# 43. No-Derivation Diagnostics

When a tree cannot be selected, diagnostics should identify:

```text
node
operator
required nonterminal
available nonterminals
candidate rules
rejected rules
rejection reasons
```

For example:

```text
cannot derive `reg` at node `ADD`

available:
    addr
    imm

required:
    reg

candidate rules:
    ADD(reg, reg) -> reg
        rejected: left child cannot derive reg

    ADD(reg, imm) -> reg
        rejected: right child is not a legal immediate
```

This is substantially more useful than:

```text
instruction selection failed
```

---

# 44. Rule Inspection

Provide debugging support for:

```text
list rules
inspect rule
dump rule tree
dump nonterminals
dump costs
dump generated rule database
```

This is especially important because BURS behavior is determined by the interaction between many rewrite rules.

---

# 45. State Dumping

A debug mode should be able to emit:

```text
node:
    ADD

state:
    reg = 1 via rule 42
    addr = INF
    imm = 3 via rule 17
```

This makes dynamic-programming behavior inspectable.

---

# 46. Selection Trace

A selection trace should look conceptually like:

```text
Node #17 ADD

candidate:
    rule #42
    ADD(reg, reg) -> reg
    cost = 2

candidate:
    rule #43
    ADD(reg, imm) -> reg
    cost = 1

selected:
    rule #43
    cost = 1
```

The exact formatting is implementation-specific.

---

# 47. Rule Generation

If the project generates BURS tables from rules, the generated representation should contain:

```text
operator dispatch
state transitions
nonterminal identifiers
rule identifiers
costs
backtracking information
```

The runtime selector should avoid reparsing or interpreting the original rule syntax.

---

# 48. Generated BURS Tables

Generated BURS tables are build artifacts unless the repository explicitly chooses to version them.

If committed:

```text
source rule change
    |
    v
regenerate
    |
    v
test generated output
```

Do not manually edit generated tables.

---

# 49. Rule Compression

Large architectures may produce very large BURS rule sets.

Potential optimizations include:

```text
operator-indexed rules
nonterminal compression
state interning
transition-table compression
equivalent-rule elimination
cost-table compaction
```

These are implementation optimizations.

They must preserve exact selection semantics.

---

# 50. Rule Equivalence

If two rules produce identical:

```text
lhs
operator
child requirements
result
instruction
cost
constraints
```

they should normally be deduplicated.

Do not deduplicate rules merely because their instruction names look similar.

---

# 51. Rule Priorities

If priorities are supported, they must be explicit.

Priority must not silently override cost unless the documented selection policy says so.

For example:

```text
primary:
    minimum cost

secondary:
    rule priority

tertiary:
    rule ID
```

is a reasonable deterministic policy.

---

# 52. Multi-Objective Costs

The architecture should leave room for:

```text
Cost = {
    size,
    latency,
    pressure
}
```

Even if the initial engine only supports:

```text
uint64_t
```

Do not make all APIs fundamentally depend on arithmetic addition of scalar integers.

---

# 53. Target Machine Description

The target description should contain the information required to construct BURS rules.

Do not duplicate architecture information manually between:

```text
Infobank
UMD
Limeburg rules
```

when it can be derived.

The preferred pipeline is:

```text
Infobank
    |
    v
UMD
    |
    v
BURS rule generation
```

---

# 54. Infobank Provenance

Every generated instruction rule should be traceable to its machine-description source.

Useful provenance:

```text
architecture
family
model
version
instruction
source file
semantic source
UMD entity
rule ID
```

The Infobank schema contains architecture-level metadata such as architecture, family, model, version, instruction count, register classes, semantics, and compiler information. Preserve this lineage where practical.

---

# 55. Instruction Semantics

Semantic descriptions and BURS patterns are different things.

Semantics answer:

```text
What does the instruction do?
```

BURS patterns answer:

```text
What source computation can this instruction implement?
```

Do not automatically equate the two.

A semantic instruction description may need to be transformed into one or more legal selection patterns.

---

# 56. Pattern Legality

A BURS pattern must be legal with respect to:

```text
operand constraints
types
immediates
register classes
memory semantics
side effects
control flow
instruction availability
```

Never emit a machine instruction merely because its opcode appears to match a source operator.

---

# 57. Selection Result

A selection result should contain:

```text
selected rule
selected instruction
source node
target operands
child derivations
cost
provenance
```

The result must remain inspectable after selection.

---

# 58. Scheduler IR Emission

The selected derivation should be lowered into Scheduler IR.

For example:

```text
source:

    ADD
   /   \
 LOAD  CONST(7)
```

may select:

```text
ADDI
```

and emit:

```text
sir.addi %v2, %v1, 7
```

The Scheduler IR should preserve:

```text
%v1
    |
    v
ADDI
    |
    v
%v2
```

and any memory/control dependencies.

---

# 59. Physical Registers

Do not assign physical registers during ordinary BURS selection.

The selected operation may specify:

```text
GPR
FPR
VR
fixed register
```

but physical allocation remains downstream.

---

# 60. Testing

Testing must cover the entire BURS pipeline.

## Parser

```text
valid grammar
invalid grammar
ambiguity
malformed rules
```

## Rule validation

```text
valid rule
invalid operator
invalid nonterminal
invalid operand count
invalid target instruction
invalid cost
```

## State computation

Test:

```text
leaf rules
unary rules
binary rules
deep trees
multiple competing rules
ties
```

## Selection

Test:

```text
single instruction
multi-instruction tree
immediate matching
addressing modes
complex patterns
```

## Negative tests

Test:

```text
no derivation
invalid immediate
invalid register class
invalid type
unsupported operation
```

## Integration

Test:

```text
source IR
    -> selection tree
    -> BURS
    -> selected instructions
    -> Scheduler IR
```

---

# 61. Regression Tests

Every discovered incorrect selection must become a regression test.

The test should identify:

```text
source tree
rule set / UMD
expected instruction sequence
expected cost
```

Do not only test that selection succeeds.

Test that the correct derivation is selected.

---

# 62. Determinism

The same:

```text
tree
rule set
target description
cost model
configuration
```

must produce the same result.

Do not depend on:

```text
pointer order
hash-table iteration
allocation order
thread scheduling
```

for rule selection.

---

# 63. Performance

BURS is chosen partly because it can provide very fast instruction selection.

Avoid turning the runtime selector into a general-purpose constraint solver.

The critical path should be approximately:

```text
tree traversal
+
state computation
+
rule selection
+
backtracking
```

Optimize only after correctness is established.

Potential optimizations include:

```text
operator-indexed rule lookup
compact nonterminal IDs
flat state tables
arena allocation
memoized states
precompiled transitions
rule-table compression
```

---

# 64. Memory Management

Selection trees may be large.

Prefer arenas or region allocation where object lifetimes naturally coincide.

Avoid excessive individual heap allocations for:

```text
tree nodes
state entries
rule matches
diagnostic objects
```

Do not sacrifice API ownership clarity for internal allocation optimizations.

---

# 65. Thread Safety

Independent Limeburg contexts should be usable concurrently.

Avoid mutable global state for:

```text
rule databases
machine descriptions
selection state
diagnostics
parser state
```

Immutable rule databases should be shareable between selector instances where practical.

---

# 66. Parser Reentrancy

Parser state should be owned by the parsing context.

Do not use global parser state unless required by the generated parser and safely isolated.

Multiple UMD/rule files should be parseable independently.

---

# 67. Repository Organization

A reasonable structure is:

```text
limeburg/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
│
├── include/
│   └── limeburg/
│       ├── limeburg.h
│       ├── context.h
│       ├── tree.h
│       ├── node.h
│       ├── rule.h
│       ├── rules.h
│       ├── selector.h
│       ├── state.h
│       ├── selection.h
│       ├── scheduler-ir.h
│       └── error.h
│
├── include-cxx/
│   └── limeburg/
│       ├── context.hpp
│       ├── tree.hpp
│       ├── rule.hpp
│       ├── rules.hpp
│       ├── selector.hpp
│       ├── state.hpp
│       ├── selection.hpp
│       └── scheduler_ir.hpp
│
├── src/
│   ├── api/
│   ├── ir/
│   ├── tree/
│   ├── rules/
│   ├── burs/
│   ├── costs/
│   ├── umd/
│   ├── diagnostics/
│   └── scheduler/
│
├── grammar/
│   └── limeburg.g
│
├── scripts/
│   └── third_party/dparser
│
├── generated/
│   └── parser/
│
├── tests/
│   ├── parser/
│   ├── rules/
│   ├── state/
│   ├── selection/
│   ├── diagnostics/
│   └── integration/
│
├── metacode/
├── umd/
└── third_party/
```

Adapt this to the existing repository rather than blindly creating duplicate infrastructure.

---

# 68. Shared Toolchain

Limeburg should reuse the project's established infrastructure for:

```text
parsing
grammar generation
Aurocks
GLR parsing
machine-description loading
Infobank processing
UMD handling
testing
build configuration
```

Do not fork equivalent infrastructure inside Limeburg.

---

# 69. No Unnecessary Frameworks

Do not introduce:

```text
LLVM
MLIR
QBE
another parser generator
another BURS framework
another SAT/SMT framework
```

unless explicitly required.

Limeburg's purpose is to provide its own BURS implementation.

---

# 70. Relationship to Unisel

Do not merge Limeburg and Unisel into one selector.

They are complementary implementations.

Shared components may include:

```text
Infobank
UMD
instruction metadata
semantic representation
register descriptions
Scheduler IR
C/C++ utility libraries
parser infrastructure
```

Selection engines remain separate:

```text
Limeburg
    |
    +-- BURS

Unisel
    |
    +-- SAT/SMT
```

This allows the same target description to be used for both approaches.

---

# 71. Experimental Hybrid Selection

A future hybrid system may use:

```text
BURS
+
SAT/SMT
```

but Limeburg itself should remain a clean BURS implementation.

Possible future architecture:

```text
                 UMD
                  |
        +---------+---------+
        |                   |
     Limeburg             Unisel
       BURS               SAT/SMT
        |                   |
        +---------+---------+
                  |
             Scheduler IR
```

Do not add hybrid solving merely because a difficult selection case exists.

---

# 72. Development Workflow

When implementing a feature:

1. Inspect the existing IR and rule representation.
2. Determine whether the feature belongs to:

   * tree representation;
   * rule representation;
   * state computation;
   * cost model;
   * target description;
   * Scheduler IR.
3. Implement the smallest coherent abstraction.
4. Add positive tests.
5. Add negative tests.
6. Add a regression test if fixing a bug.
7. Regenerate parser/generated artifacts when necessary.
8. Run the complete relevant test suite.

---

# 73. BURS Correctness Invariants

The following must always hold:

```text
A. Every selected derivation corresponds to a valid rule.

B. Every selected rule's children satisfy its required nonterminals.

C. Every selected target instruction is legal for the target.

D. The selected derivation covers the required source tree.

E. Source side effects are preserved.

F. Memory semantics are preserved.

G. Control-flow semantics are preserved.

H. Immediate constraints are satisfied.

I. Register-class constraints are preserved.

J. The reported cost equals the cost of the selected derivation.

K. Backtracking reconstructs exactly the derivation represented
   by the selected root state.

L. Scheduler IR preserves the selected instructions and their
   semantic dependencies.

M. Parser-generated code is derived from the authoritative grammar
   and is never manually modified.
```

---

# 74. Definition of Done

A Limeburg feature is complete when:

```text
[ ] Core representation implemented
[ ] C API updated if necessary
[ ] C++ API updated if necessary
[ ] Rule representation updated if necessary
[ ] Parser updated if syntax changed
[ ] GLR parser regenerated
[ ] Rule validation implemented
[ ] BURS state computation implemented
[ ] Backtracking implemented
[ ] Cost behavior tested
[ ] Diagnostics implemented where appropriate
[ ] Scheduler IR emission handled
[ ] Positive tests added
[ ] Negative tests added
[ ] Regression tests added where applicable
[ ] Documentation updated
[ ] Build passes
[ ] Relevant test suite passes
```

The implementation must leave Limeburg in a reproducible, deterministic, buildable, and testable state.
