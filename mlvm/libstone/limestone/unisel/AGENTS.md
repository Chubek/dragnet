# AGENTS.md

## Project: Unisel

Unisel is an instruction-selection intermediate representation and instruction-selection framework.

Its purpose is to provide a reusable C/C++ implementation of **universal instruction selection** over machine-independent program representations, combining ideas from:

* Blindell et al.'s universal instruction-selection model;
* graph-based instruction selection;
* pattern matching over SSA/data-flow graphs;
* SAT/SMT-based global instruction selection;
* explicit machine-instruction alternatives;
* constraint-based code-placement and operand selection;
* cost-driven selection;
* a separate Scheduler IR suitable for downstream instruction scheduling.

Unisel is **not** a complete compiler backend. It is the instruction-selection layer between a machine-independent IR and a later machine/scheduling/register-allocation pipeline.

The SAT/SMT implementation framework is:

```text
third_party/satie/
```

Satie is the constraint-solving substrate used by Unisel. Do not introduce another SAT/SMT framework unless explicitly requested.

---

# 1. Core Architecture

The conceptual pipeline is:

```text
                 metacode Infobank
                        |
                        v
                Universal Machine
                  Description
                     (.umd)
                        |
                        v
              +-------------------+
              |      Unisel       |
              | Machine Patterns  |
              | + Semantics        |
              | + Constraints      |
              +---------+---------+
                        |
                        v
              Selection Graph / UF
                        |
                        v
                 Satie Encoding
                        |
                 SAT / SMT Solve
                        |
                        v
              Selected Instruction
                    Graph
                        |
                        v
                  Scheduler IR
                        |
                        v
              Instruction Scheduler
                        |
                        v
                 Machine IR
```

The important architectural boundary is:

```text
Instruction Selection != Instruction Scheduling
```

Instruction selection determines **which target instructions implement the computation**.

Scheduling determines **when those selected instructions execute and which machine resources they occupy**.

The Unisel selection result must therefore retain enough information for a later scheduler without prematurely committing to:

* physical registers;
* final instruction addresses;
* final encodings;
* concrete issue cycles;
* register allocation decisions;
* final bundling.

---

# 2. Design Principles

## 2.1 Global rather than purely local selection

Unisel should support instruction selection over a graph representing the complete relevant computation rather than assuming that every machine instruction corresponds to an isolated local tree pattern.

The internal model should be capable of representing:

* SSA values;
* basic blocks;
* CFG edges;
* data-flow edges;
* memory dependencies;
* control dependencies;
* instruction-pattern matches;
* alternative implementations;
* values defined by multiple candidate instructions;
* code motion;
* shared subexpressions;
* constants;
* branches;
* calls;
* returns;
* machine-specific operations.

The implementation should be capable of expressing Blindell-style universal instruction-selection problems while remaining useful for more conventional DAG/tree instruction selectors.

---

# 3. Universal Machine Description

Unisel consumes a normalized **Universal Machine Description**:

```text
*.umd
```

A `.umd` is the target-machine description used by the selection engine.

The `.umd` representation should be generated from the project's **metacode Infobank** rather than being manually duplicated whenever possible.

The intended relationship is:

```text
metacode Infobank
        |
        | normalization / lowering
        v
      .umd
        |
        v
     Unisel
```

The Infobank schema supplied to this project describes an ISA Description Bundle.

The top-level object requires:

```text
format
version
grammar
semantic_format
architectures
```

and identifies the format as:

```text
isa-description-bundle
```

with S-expression semantic data.

An architecture entry contains, among other things:

```text
file
arch
family
model
version
instruction_count
register_classes
semantics
compiler
```

The Infobank also permits architecture-level tooling metadata and per-instruction tooling metadata:

```text
tooling
op_tooling
```

These fields must not be discarded during `.umd` generation merely because they are not directly required by the initial selector.

The generator should preserve source provenance so that an instruction in a `.umd` can be traced back to:

```text
Infobank
    -> architecture
    -> instruction
    -> semantic description
    -> generated UMD entity
```

---

# 4. `.umd` Responsibilities

The Universal Machine Description is the machine-facing input to Unisel.

It should eventually describe at least:

## ISA identity

```text
architecture
family
model
version
```

## Registers

```text
register classes
register aliases
special registers
fixed registers
register constraints
```

## Instructions

Each instruction should be capable of describing:

```text
opcode
mnemonic
operands
operand classes
definitions
uses
implicit definitions
implicit uses
side effects
flags
memory effects
control-flow effects
semantic pattern
```

## Instruction patterns

Patterns describe computations that a target instruction can implement.

Conceptually:

```text
ADD:
    (add $lhs $rhs)

ADDI:
    (add $lhs (constant $imm))

LOAD_ADD:
    (load (add $base $offset))
```

The exact `.umd` syntax is determined by the Unisel grammar and implementation. Do not invent incompatible syntax in individual components.

## Selection alternatives

An operation may have multiple target implementations.

For example:

```text
add(x, y)
```

could match:

```text
ADD
ADDI
LEA
specialized fused operation
```

depending on operand structure, immediate availability, addressing modes, register constraints, and machine model.

## Costs

Costs must be represented as explicit attributes rather than hidden inside the solver.

Potential dimensions include:

```text
instruction_count
code_size
latency
throughput
register_pressure
spill_risk
resource_pressure
encoding_size
```

A selector may optimize a weighted or lexicographic combination of these.

Do not hard-code a universal notion of "best instruction."

---

# 5. Selection IR

Unisel should maintain a distinction between:

```text
source/program graph
target pattern graph
selection solution
Scheduler IR
```

Do not collapse all four into one structure.

A useful conceptual representation is:

```text
ProgramGraph
    |
    +-- Value
    +-- Operation
    +-- BasicBlock
    +-- CFGEdge
    +-- DataEdge
    +-- MemoryEdge
    |
    v
PatternMatcher
    |
    +-- PatternMatch
    +-- MatchCandidate
    |
    v
ConstraintModel
    |
    +-- Boolean selection variables
    +-- location variables
    +-- operand variables
    +-- ordering variables
    +-- cost variables
    |
    v
Satie
    |
    v
SelectionSolution
    |
    v
Scheduler IR
```

---

# 6. Universal Function Graph

The internal selection graph should support the central idea of a **Universal Function Graph (UF graph)**.

A UF graph combines:

```text
CFG structure
+
SSA/data-flow structure
```

into one representation over which instruction patterns can be matched.

The graph must be capable of representing values independently from their eventual target instructions.

For example:

```text
v1 = load p
v2 = add v1, c
v3 = mul v2, x
```

must initially represent the computation rather than prematurely deciding that:

```text
LOAD
ADD
MUL
```

are necessarily the final instructions.

The target machine may instead provide:

```text
LOAD_ADD
```

or:

```text
LOAD_ADD_MUL
```

or another architecture-specific implementation.

---

# 7. Pattern Matching

Patterns are graph transformations/coverings, not merely textual opcode aliases.

A pattern should be able to express:

```text
operation structure
operand relationships
constant requirements
register-class requirements
memory relationships
side effects
control-flow effects
implicit operands
```

Pattern matching should produce a set of candidate matches:

```text
MatchCandidate {
    pattern
    root
    covered_nodes
    input_values
    output_values
    constraints
    cost
}
```

Do not immediately mutate the program graph when a match is discovered.

Matching must be capable of producing competing candidates.

---

# 8. SAT/SMT Formulation

Satie is the solver substrate.

Do not couple the high-level selector directly to Satie's internal implementation.

Use an intermediate constraint model.

Conceptually:

```text
Unisel Selection Model
        |
        v
Satie Encoder
        |
        v
Satie variables / constraints
        |
        v
Satie solver
```

The selector should be able to express Boolean, integer, bit-vector, and other supported constraint types through Satie.

---

# 9. Selection Variables

The initial solver model should support variables conceptually equivalent to:

```text
select[m]
```

where:

```text
select[m] = true
```

means candidate match `m` is selected.

Other useful variables include:

```text
location[v]
def[v]
operand[m,i]
cover[m,n]
place[m,b]
order[m1,m2]
```

The exact Satie API must be isolated behind Unisel's constraint-model abstraction.

---

# 10. Coverage Constraints

Every source operation/value requiring target implementation must be covered by a valid selected pattern.

Conceptually:

```text
covered(n) <=> OR(select[m] for m covering n)
```

The model must prevent invalid overlapping selections.

For example, if two candidates both consume the same operation:

```text
M1 covers A,B
M2 covers B,C
```

the solver must not select both unless the model explicitly permits the overlap.

---

# 11. Definition Constraints

A selected instruction must correctly define the values it claims to define.

The model must distinguish:

```text
source value
target value
instruction operand
instruction definition
```

Do not assume a one-to-one mapping between source SSA values and target registers.

At this stage, use virtual/abstract values.

Physical register assignment belongs later.

---

# 12. Operand Constraints

Operand matching must support:

```text
register class
constant/immediate
addressing mode
memory operand
implicit operand
fixed register
tied operand
subregister relationship
operand equality
operand inequality
```

For example:

```text
ADDI r1, r2, imm
```

should be expressible as a pattern requiring:

```text
lhs : GPR
rhs : GPR
imm : Immediate
```

while a target-specific form may require:

```text
lhs == destination
```

or:

```text
destination == fixed register
```

---

# 13. Placement

Selection and placement are related but separate concepts.

A candidate instruction may be associated with:

```text
basic block
program point
control-flow region
value definition location
```

The solver must prevent illegal motion across:

```text
side effects
control-flow boundaries
memory barriers
volatile operations
calls
returns
observable operations
```

unless the target description explicitly permits the transformation.

---

# 14. Memory Semantics

Memory operations must never be treated as ordinary data-flow nodes.

The selection model must preserve:

```text
load
store
read/write
volatile
atomic
ordering
address
size
alignment
alias information
memory dependencies
```

A target instruction such as:

```text
LOAD_ADD
```

may consume a memory operation and an arithmetic operation simultaneously.

The selected Scheduler IR must retain the resulting memory dependency.

---

# 15. Control Flow

The selection model must represent:

```text
branch
conditional branch
indirect branch
call
return
trap
barrier
```

Control-flow instructions may have both:

```text
data dependencies
control dependencies
```

Do not encode branches merely as ordinary arithmetic instructions.

---

# 16. Cycles

The solver must prevent illegal cyclic instruction dependencies.

This is particularly important when global instruction selection and code motion are combined.

The selected graph must remain schedulable.

Do not rely on the later scheduler to repair an invalid cyclic selection.

---

# 17. Objective Model

Instruction selection should support multiple cost dimensions.

At minimum design the API so that the following can be represented:

```text
instruction count
code size
estimated latency
estimated throughput
register pressure
resource pressure
spill cost
target-specific cost
```

Do not bake one objective into the matcher.

A target or compiler pipeline should be able to configure the objective.

Possible optimization strategies include:

```text
lexicographic:
    minimize code size
    then latency
    then register pressure

weighted:
    4 * latency + 2 * size + pressure

Pareto / multi-solution:
    expose several feasible selections
```

The initial implementation may use a simpler objective, but the API must not prevent future objective extensions.

---

# 18. Selection vs Scheduling

This distinction is fundamental.

Selection answers:

```text
Which target instruction implements this computation?
```

Scheduling answers:

```text
When should this instruction execute?
```

Therefore:

```text
Unisel
    -> Scheduler IR
    -> Scheduler
```

rather than:

```text
Unisel
    -> physical machine instructions with fixed cycles
```

A selected instruction may contain metadata useful to scheduling:

```text
instruction class
latency
throughput
resource requirements
memory effects
side effects
```

but the selector must not assign final issue cycles.

---

# 19. Scheduler IR Emission

The selected solution should be materialized into Scheduler IR.

For example, a source graph:

```text
v1 = load [p]
v2 = add v1, x
v3 = store [q], v2
```

might produce:

```text
sir.load   %v1, [%p]
sir.add    %v2, %v1, %x
sir.store  [%q], %v2
```

If the target contains:

```text
LOAD_ADD
```

the solution could instead produce:

```text
sir.load_add %v2, [%p], %x
sir.store    [%q], %v2
```

The important point is that the solver's selected pattern becomes a **machine operation in Scheduler IR**, rather than being emitted directly as assembly.

---

# 20. Scheduler IR Requirements

Every emitted operation should retain enough information for scheduling.

Conceptually:

```text
SchedulerInstruction {
    id
    opcode
    instruction_class

    defs[]
    uses[]

    implicit_defs[]
    implicit_uses[]

    dependencies[]

    memory_effect
    control_effect
    side_effects

    register_constraints

    latency
    throughput
    resources

    selection_origin
}
```

`selection_origin` should make debugging possible:

```text
source operation(s)
pattern ID
match ID
UMD instruction ID
Infobank provenance
```

---

# 21. C API

The C API should be stable, explicit, opaque where appropriate, and usable without C++.

Prefer:

```c
typedef struct unisel_context unisel_context;
typedef struct unisel_module unisel_module;
typedef struct unisel_function unisel_function;
typedef struct unisel_pattern unisel_pattern;
typedef struct unisel_match unisel_match;
typedef struct unisel_model unisel_model;
typedef struct unisel_solution unisel_solution;
typedef struct unisel_scheduler_ir unisel_scheduler_ir;
```

over exposing implementation structures.

Use explicit ownership rules.

Every owning API must clearly document:

```text
who allocates
who owns
who frees
whether objects may outlive parents
thread safety
```

Avoid hidden global state.

---

# 22. C++ API

The C++ API should provide RAII and stronger type safety over the C ABI.

Prefer:

```cpp
unisel::Context
unisel::Module
unisel::Function
unisel::Pattern
unisel::Match
unisel::SelectionModel
unisel::Solution
unisel::SchedulerIR
```

Use:

```cpp
std::unique_ptr
std::shared_ptr
std::span
std::string_view
```

where appropriate.

Do not make the C++ API depend on compiler-specific extensions unless required.

The C API remains the ABI boundary.

---

# 23. Satie Boundary

Satie must be isolated behind an Unisel abstraction.

Do not expose Satie types throughout the entire codebase.

Prefer:

```text
Unisel
  |
  +-- constraint/
  |     +-- Model
  |     +-- Bool
  |     +-- Int
  |     +-- BitVector
  |     +-- Constraint
  |
  +-- solver/
        +-- SatieEncoder
        +-- SatieSolver
```

This allows the instruction-selection algorithms to remain independent of the concrete solver representation.

`third_party/satie` is an implementation dependency, not Unisel's public conceptual API.

---

# 24. Parser

The Unisel grammar is:

```text
grammar/unisel.g
```

It is generated into a GLR parser using:

```text
scripts/third_party/dparser
```

The grammar is authoritative for the textual Unisel language.

Do not manually edit generated parser sources.

The source of truth is:

```text
grammar/unisel.g
```

When changing syntax:

1. modify `grammar/unisel.g`;
2. run the appropriate `scripts/third_party/dparser` generation step;
3. rebuild;
4. run parser tests;
5. run semantic/selection tests.

The generated parser should be treated as build output.

---

# 25. GLR Parser Semantics

Because the parser is GLR, grammar ambiguity may be intentionally represented.

Do not "fix" an ambiguous grammar simply because it produces multiple parses.

Instead determine whether the ambiguity is:

```text
intentional
resolved semantically
resolved through precedence
actually erroneous
```

Semantic analysis belongs after parsing.

The parser should construct an AST or syntax representation; it should not perform SAT/SMT solving.

---

# 26. Grammar-to-IR Pipeline

The textual language should follow:

```text
source
  |
  v
GLR parser
  |
  v
syntax tree
  |
  v
semantic validation
  |
  v
Unisel IR / UMD model
  |
  v
pattern database
```

Keep parsing independent from:

```text
pattern matching
constraint generation
solver invocation
code emission
```

---

# 27. UMD Loading

UMD loading should be separate from solving.

Conceptually:

```cpp
auto umd = unisel::load_umd(...);

auto target = unisel::Target::from_umd(umd);

auto selector = unisel::Selector(target);
```

Do not make the parser itself construct a solver.

---

# 28. Infobank -> UMD

The metacode Infobank is the upstream source for machine knowledge.

The conversion pipeline should be:

```text
Infobank
   |
   v
schema validation
   |
   v
semantic normalization
   |
   v
instruction records
   |
   v
UMD generation
```

The supplied schema identifies:

```text
format = "isa-description-bundle"
semantic_format = "s-expression"
version = 2
```

and requires architecture records with instruction count, register classes, semantics, and compiler metadata.

The UMD generator must not silently invent information absent from the Infobank.

If information is unavailable:

```text
unknown
unspecified
target-dependent
```

should be represented explicitly according to the UMD schema.

Do not manufacture latency, throughput, resource, or semantic information.

---

# 29. Provenance

Machine descriptions should preserve provenance.

Where possible:

```text
UMD instruction
    -> Infobank architecture
    -> Infobank operation
    -> source file
    -> semantic source
```

This is important for:

* debugging;
* regenerating UMDs;
* validating instruction patterns;
* comparing architectures;
* diagnosing incorrect selections.

---

# 30. Error Handling

Errors must distinguish:

```text
parse error
schema error
UMD validation error
semantic error
pattern error
constraint-generation error
solver error
no-solution error
Scheduler IR emission error
```

Do not return a generic:

```text
UNISEL_ERROR
```

for all failures.

The C API should expose structured error information where practical.

---

# 31. No-Solution Handling

A SAT/SMT selector must distinguish:

```text
SAT
UNSAT
UNKNOWN
TIMEOUT
INTERRUPTED
INVALID_MODEL
```

`UNSAT` does not necessarily mean the input program is invalid.

It may mean:

```text
target description incomplete
pattern set incomplete
operand constraints too restrictive
memory semantics unavailable
control-flow constraints impossible
solver model incorrectly encoded
```

Diagnostics should help identify the cause.

---

# 32. Debugging and Explainability

Selection must be inspectable.

Provide facilities to dump:

```text
program graph
candidate matches
constraint model
solver variables
selected matches
rejected matches
objective values
final Scheduler IR
```

A useful diagnostic should answer:

```text
Why was this instruction selected?
```

For example:

```text
source:
    %4 = add %1, 7

selected:
    ADDI

reason:
    matched pattern ADDI.add-immediate
    immediate 7 is legal
    operand %1 satisfies GPR
    ADDI cost = 1
    ADD cost = 2
```

The exact explanation engine may be implemented later, but the IR must retain enough information to support it.

---

# 33. Testing Strategy

Tests must exist at multiple levels.

## Parser tests

```text
grammar/
tests/parser/
```

Test:

* valid syntax;
* invalid syntax;
* ambiguity;
* malformed patterns;
* malformed UMD definitions.

## UMD tests

Test:

```text
load
validation
normalization
instruction lookup
register classes
pattern construction
```

## Pattern tests

Test:

```text
exact match
partial match
constant match
multi-node match
memory match
control-flow match
overlapping candidates
```

## Solver tests

Test:

```text
SAT
UNSAT
multiple solutions
objective optimization
coverage
operand constraints
cyclic dependency prevention
```

## Integration tests

Test:

```text
source IR
    -> matching
    -> Satie
    -> solution
    -> Scheduler IR
```

## Regression tests

Every previously discovered incorrect selection should become a permanent regression test.

---

# 34. Determinism

Given identical:

```text
input IR
UMD
configuration
solver configuration
```

Unisel should preferably produce deterministic results.

Do not rely on:

```text
pointer addresses
unordered iteration
hash randomization
thread scheduling
```

for externally visible instruction ordering.

If Satie exposes solver randomness, provide an explicit seed/configuration.

---

# 35. Performance

Instruction selection is potentially combinatorial.

Do not prematurely optimize away the expressive constraint model.

First provide:

```text
correct candidate generation
correct constraints
correct solving
correct emission
```

Then optimize.

Potential optimizations include:

```text
candidate indexing
opcode indexing
root-operation indexing
constant-pattern indexing
register-class filtering
dominance filtering
basic-block filtering
memoized pattern matches
constraint simplification
symmetry breaking
incremental solving
solver timeouts
candidate pruning
```

Candidate generation should eliminate obviously impossible matches before they reach Satie.

---

# 36. Constraint Generation

Keep constraint generation modular.

Prefer components such as:

```text
CoverageConstraints
OperandConstraints
DefinitionConstraints
MemoryConstraints
ControlFlowConstraints
PlacementConstraints
AcyclicityConstraints
CostConstraints
RegisterConstraints
```

This makes it possible to enable or disable constraint families for debugging and experimentation.

---

# 37. Integrated Selection + Scheduling

The initial architecture should support a clean two-stage process:

```text
selection
    |
    v
Scheduler IR
    |
    v
scheduling
```

However, do not design the constraint model in a way that makes integrated solving impossible.

Future integrated optimization may jointly consider:

```text
instruction selection
+
instruction scheduling
+
resource assignment
+
register pressure
```

The separation should therefore be an API boundary, not an assumption that these problems can never be solved jointly.

---

# 38. Register Allocation Boundary

Unisel should generally operate on virtual/abstract values.

It may preserve constraints such as:

```text
GPR
FPR
vector register
special register
fixed register
tied operand
```

but should not normally assign physical registers.

Preferred pipeline:

```text
Unisel
  |
  v
Scheduler IR
  |
  v
Scheduling
  |
  v
Register Allocation
  |
  v
Machine IR
```

---

# 39. Encoding Boundary

Instruction encoding is not the primary responsibility of Unisel.

Unisel selects an instruction identity and operands.

Later components determine:

```text
encoding
relocations
instruction bytes
fixups
final addressing
```

An encoding description may be retained in the UMD for cost/legal-form reasoning, but encoding must not contaminate the core selection algorithm.

---

# 40. API Layering

Maintain this dependency direction:

```text
C API
  |
  v
C++ API
  |
  v
Core Unisel
  |
  +-- IR
  +-- patterns
  +-- matching
  +-- constraints
  +-- solver adapter
  +-- Scheduler IR
```

The core must not depend on the C API.

The C++ API may wrap the C API or share the same underlying implementation, but public abstractions must remain coherent.

---

# 41. Repository Organization

Prefer a structure along these lines:

```text
Unisel/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
│
├── include/
│   └── unisel/
│       ├── unisel.h
│       ├── context.h
│       ├── module.h
│       ├── target.h
│       ├── pattern.h
│       ├── matcher.h
│       ├── selector.h
│       ├── solution.h
│       ├── scheduler-ir.h
│       ├── umd.h
│       └── error.h
│
├── include-cxx/
│   └── unisel/
│       ├── context.hpp
│       ├── target.hpp
│       ├── pattern.hpp
│       ├── matcher.hpp
│       ├── selector.hpp
│       ├── solution.hpp
│       ├── scheduler_ir.hpp
│       └── umd.hpp
│
├── src/
│   ├── api/
│   ├── ir/
│   ├── patterns/
│   ├── matching/
│   ├── selection/
│   ├── constraints/
│   ├── solver/
│   ├── umd/
│   ├── scheduler/
│   └── diagnostics/
│
├── grammar/
│   └── unisel.g
│
├── scripts/
│   └── third_party/dparser
│
├── generated/
│   └── parser/
│
├── tests/
│   ├── parser/
│   ├── umd/
│   ├── patterns/
│   ├── matching/
│   ├── solver/
│   ├── scheduler/
│   └── integration/
│
├── metacode/
│   └── ...
│
├── umd/
│   └── ...
│
└── third_party/
    └── satie/
```

Adapt this to the existing repository instead of blindly creating duplicate directories.

---

# 42. Generated Files

Generated parser sources must not be manually edited.

The authoritative source is:

```text
grammar/unisel.g
```

The generation mechanism is:

```text
scripts/third_party/dparser
```

If generated files are committed, update them whenever the grammar changes.

If generated files are ignored by Git, ensure the normal build regenerates them.

---

# 43. Build Rules

The build must ensure:

```text
grammar/unisel.g
        |
        v
scripts/third_party/dparser
        |
        v
GLR parser
        |
        v
Unisel library
```

Satie must be available before compiling solver-dependent components.

Do not require users to install an unrelated external SAT/SMT framework.

---

# 44. Coding Style

Prefer straightforward C and modern portable C++.

Avoid unnecessary abstraction layers.

Do not introduce:

* LLVM;
* MLIR;
* QBE;
* another parser framework;
* another SAT/SMT framework;

to solve problems already covered by Unisel/Satie/Aurocks.

Use existing project infrastructure first.

---

# 45. Ownership

Use explicit ownership.

C:

```c
unisel_*_create()
unisel_*_destroy()
```

where appropriate.

C++:

```cpp
std::unique_ptr
```

for unique ownership.

Do not create hidden ownership relationships between:

```text
Context
Module
Target
Pattern
Match
Solution
SchedulerIR
```

Document lifetime dependencies.

---

# 46. Thread Safety

Do not use mutable global state for:

```text
target descriptions
patterns
solver models
selection contexts
diagnostics
```

Independent selection contexts should be capable of existing concurrently.

If Satie has thread-safety restrictions, isolate them in the solver adapter.

---

# 47. ABI Stability

The C API is the stable ABI boundary.

Avoid exposing:

```text
C++ STL types
templates
C++ exceptions
implementation structs
Satie internal types
```

through the C API.

Use opaque handles and explicit functions.

---

# 48. Public API Philosophy

A user should be able to perform:

```text
create context
load UMD
construct source graph
run selection
obtain solution
emit Scheduler IR
inspect diagnostics
destroy objects
```

without understanding Satie internals.

The public API should describe **instruction selection**, not the implementation technology used to solve it.

---

# 49. Do Not Leak Solver Concepts

Avoid APIs such as:

```cpp
SatieBool
SatieInteger
SatieConstraint
```

in the main Unisel public API.

Prefer:

```cpp
SelectionVariable
Constraint
BooleanExpr
IntegerExpr
```

or an equivalent Unisel-level abstraction.

Satie-specific objects belong under the internal solver adapter.

---

# 50. Experimental Features

Experimental algorithms may be placed behind explicit configuration.

Examples:

```text
UNISEL_ENABLE_GLOBAL_SELECTION
UNISEL_ENABLE_INTEGRATED_SCHEDULING
UNISEL_ENABLE_MULTI_OBJECTIVE
UNISEL_ENABLE_EXPLANATIONS
```

Do not make experimental solver behavior silently alter stable selection semantics.

---

# 51. Documentation Requirements

Document:

```text
architecture
IR semantics
UMD format
pattern language
C API
C++ API
solver model
Scheduler IR
examples
```

Every non-obvious solver constraint should have an explanation of the property it enforces.

For example:

```text
constraint: exactly-one-definition
reason: prevents two selected target instructions from claiming
        incompatible definitions for the same target value.
```

---

# 52. Implementation Order

When implementing a new Unisel repository from scratch, proceed in this order:

```text
1. Core data types
2. Error/diagnostic system
3. UMD model
4. UMD loader/parser
5. Pattern representation
6. Source/UF graph
7. Pattern matcher
8. Candidate database
9. Constraint model abstraction
10. Satie adapter
11. Coverage constraints
12. Operand constraints
13. Definition constraints
14. Memory/control constraints
15. Objective model
16. Solver
17. Selection solution
18. Scheduler IR
19. Solution -> Scheduler IR lowering
20. C API
21. C++ API
22. Integration tests
23. Diagnostics/explanations
24. Performance optimizations
```

Do not begin with solver optimization before the unsolved selection model can be inspected and tested.

---

# 53. Minimal End-to-End Goal

The first complete vertical slice should support:

```text
source SSA graph
       |
       v
pattern matching
       |
       v
candidate matches
       |
       v
Satie constraint model
       |
       v
SAT/SMT solution
       |
       v
selected instructions
       |
       v
Scheduler IR
```

For example:

```text
%a = load %p
%b = add %a, 7
```

with a target pattern:

```text
LOAD_ADD:
    (load (add $base $imm))
```

should be capable of producing:

```text
load_add %b, [%p], 7
```

when the target description permits that match.

---

# 54. Development Rule

When adding a feature, answer these questions:

```text
1. Is this ISA information?
2. Is this selection information?
3. Is this scheduling information?
4. Is this register-allocation information?
5. Is this encoding information?
6. Is this solver implementation detail?
```

Put the information in the lowest layer that semantically owns it.

Do not put scheduling information into ISA semantics merely because the selector currently needs it.

Do not put solver implementation details into the UMD.

Do not put physical-register decisions into Scheduler IR unless explicitly required.

---

# 55. Critical Invariants

The following invariants must always hold:

```text
A. Every selected target instruction corresponds to a valid target pattern.

B. Every required source operation/value is covered.

C. Selected patterns do not overlap illegally.

D. Every selected operand satisfies its target constraints.

E. Memory semantics are preserved.

F. Control-flow semantics are preserved.

G. Selected definitions and uses form a valid dependency graph.

H. The selected graph does not contain illegal dependency cycles.

I. Scheduler IR contains all dependencies required by later scheduling.

J. No physical register is required merely to represent a selection.

K. Solver implementation details do not leak into the public API.

L. UMD information can be traced back to its Infobank provenance.

M. Parser source is grammar/unisel.g; generated GLR code is not edited manually.
```

---

# 56. Agent Working Rules

When modifying Unisel:

1. Read the relevant existing implementation before introducing a new abstraction.
2. Preserve existing public APIs unless the task explicitly requires an API change.
3. Treat `grammar/unisel.g` as the parser source of truth.
4. Regenerate the GLR parser through `scripts/third_party/dparser` after grammar changes.
5. Treat `third_party/satie` as the solver substrate.
6. Keep Satie behind the solver abstraction.
7. Preserve Infobank provenance when generating or loading `.umd`.
8. Do not invent machine information that is absent from the Infobank/UMD.
9. Add regression tests for every solver or pattern-selection bug.
10. Keep selection, scheduling, register allocation, and encoding as separate conceptual stages.
11. Prefer inspectable intermediate representations over opaque solver state.
12. Keep C and C++ APIs aligned.
13. Do not introduce LLVM/MLIR/QBE or another solver/parser framework without an explicit requirement.
14. Do not rewrite generated parser sources manually.
15. Before declaring a selection feature complete, test both:

    * a positive SAT case;
    * a negative UNSAT case.

---

# 57. Definition of Done

A Unisel feature is complete only when:

```text
[ ] IR/data model implemented
[ ] public API updated if necessary
[ ] UMD representation updated if necessary
[ ] parser updated if syntax changed
[ ] GLR parser regenerated
[ ] candidate generation implemented
[ ] constraints implemented
[ ] Satie integration implemented
[ ] solution decoding implemented
[ ] Scheduler IR emission handled
[ ] diagnostics added
[ ] positive tests added
[ ] negative/UNSAT tests added
[ ] regression tests added where applicable
[ ] documentation updated
```

The final implementation must leave the repository in a buildable and testable state.
