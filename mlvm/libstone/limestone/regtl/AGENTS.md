# AGENTS.md — RegTL

## 1. Overview

**RegTL** (Register Transfer Language) is the register-allocation intermediate language of the **Limestone compiler framework**.

RegTL is an **IL specifically designed for register allocation and register-assignment problems**. It provides a machine-oriented representation in which register requirements, value movement, lifetimes, constraints, and allocation decisions can be represented independently of any single register-allocation algorithm.

RegTL is not intended to replace Limestone's general-purpose intermediate representation. Instead, it forms a late-stage compiler representation between machine-level code generation and physical-register assignment.

The primary purpose of RegTL is to provide a common representation on which multiple register-allocation methods can operate.

Conceptually:

```text
Limestone IR
     │
     ▼
Machine / Target Lowering
     │
     ▼
    RegTL
     │
     ├── Graph Coloring
     ├── Linear Scan
     ├── PBQP / Constraint-Based Allocation
     ├── Greedy Allocation
     ├── Priority-Based Allocation
     └── Other Allocation Methods
     │
     ▼
Physical Register Assignment
     │
     ▼
Machine Code Generation
```

---

# 2. Purpose of RegTL

RegTL exists to separate **register allocation** from the representation and implementation of individual allocation algorithms.

An allocation algorithm should not need to understand the complete semantics of Limestone's higher-level IR.

Conversely, the compiler pipeline should not need to construct a separate representation for every register-allocation algorithm.

RegTL provides the common contract between these two layers.

The central design principle is:

> **RegTL describes the register-transfer and allocation problem; allocation algorithms determine how that problem is solved.**

---

# 3. Position in the Limestone Pipeline

RegTL is a late-stage representation.

A simplified Limestone compilation pipeline is:

```text
Source / Frontend
       │
       ▼
High-Level IR
       │
       ▼
Middle-End IR
       │
       ▼
Lowering / Instruction Selection
       │
       ▼
Machine-Level Representation
       │
       ▼
      RegTL
       │
       ▼
Register Allocation
       │
       ▼
Allocated Machine Representation
       │
       ▼
Final Lowering / Emission
       │
       ▼
Object / Machine Code
```

RegTL should be introduced only after enough target-specific information is available to formulate the register-allocation problem.

However, RegTL should avoid embedding unnecessary assumptions about the particular allocation algorithm that will consume it.

---

# 4. RegTL as an IL

RegTL is an **intermediate language**, not merely a data structure used internally by one allocator.

Its representation should therefore have:

* A defined semantic model
* A defined instruction/operation model
* Explicit operands
* Explicit value identity
* Explicit register requirements
* Explicit constraints
* Explicit control-flow relationships where required
* Defined lifetime semantics
* Defined treatment of calls and ABI boundaries
* Defined treatment of fixed registers
* Defined treatment of register classes
* Defined treatment of spills
* A stable representation suitable for multiple allocation algorithms

RegTL should be serializable or inspectable where doing so materially improves debugging, testing, or compiler development.

---

# 5. Core Abstraction

The fundamental problem represented by RegTL is the assignment of compiler values to physical storage locations subject to machine constraints.

A RegTL program should make it possible to determine:

```text
Value
  │
  ├── Where is it defined?
  ├── Where is it used?
  ├── What is its lifetime?
  ├── What register class does it require?
  ├── Which registers are permitted?
  ├── Which registers are forbidden?
  ├── Does it require a fixed register?
  ├── Can it be spilled?
  ├── What values interfere with it?
  └── What moves or transfers are required?
```

The allocator consumes this information and produces an assignment.

---

# 6. Virtual Registers

RegTL should represent compiler values requiring register allocation as **virtual registers** or equivalent virtual storage entities.

A virtual register represents a value before physical register assignment.

Virtual registers should have stable identities within a RegTL unit.

A virtual register may have associated:

* Register class
* Width
* Subregister requirements
* Alignment requirements
* Lifetime
* Spillability
* Fixed-register requirements
* Preferred registers
* Allowed registers
* Forbidden registers
* Operand constraints
* Calling-convention constraints

Physical registers must remain distinct from virtual registers.

The allocator is responsible for establishing the mapping:

```text
Virtual Register → Physical Register
```

or, where necessary:

```text
Virtual Register → Spill Location
```

---

# 7. Physical Registers

Physical registers represent target-machine storage locations.

RegTL may reference physical registers when required by machine semantics or ABI constraints.

Examples include:

* Fixed instruction operands
* Return-value registers
* Argument registers
* Stack/frame pointers
* Condition-code registers
* Special-purpose registers
* Architectural state registers
* Registers required by particular instructions

A physical register reference is a constraint on allocation rather than an allocation result in the general case.

The allocator must preserve the semantics of all fixed-register requirements.

---

# 8. Register Classes

RegTL must support target-specific **register classes**.

A register class describes a set of physical registers that can satisfy a particular virtual-register requirement.

Examples may include:

```text
General Purpose
Floating Point
Vector
Predicate
Condition Code
Address
Special
```

The exact classes are target-dependent.

RegTL should not hard-code a universal set of register classes.

Instead, target backends should provide the register-class model required by their architecture.

---

# 9. Register Constraints

RegTL must provide a way to express constraints imposed by machine instructions and calling conventions.

Constraints may include:

* Required register
* Allowed register set
* Register-class requirement
* Register-pair requirement
* Register tuple requirement
* Fixed input register
* Fixed output register
* Tied operands
* Early-clobber outputs
* Read/write operands
* Forbidden registers
* Register-bank restrictions
* Subregister restrictions

Constraints must be represented explicitly rather than inferred from allocator implementation details.

---

# 10. Register Transfers

As implied by its name, RegTL represents **register transfers** as an important part of its machine-level model.

Transfers may occur between:

```text
Virtual Register
Physical Register
Spill Location
Memory
```

A transfer may be required because of:

* Register assignment
* Calling conventions
* Operand constraints
* Register-class conversion
* Spilling
* Reloading
* Register coalescing
* Parallel moves
* Instruction-specific requirements

The representation must distinguish semantically meaningful transfers from transformations introduced purely as allocator implementation details.

---

# 11. Register Allocation Methods

RegTL is explicitly designed to support **multiple register-allocation methods**.

The RegTL representation must therefore not encode assumptions specific to a single allocator.

Potential consumers include:

### Graph Coloring

The allocator constructs an interference representation and assigns physical registers while respecting register-class and machine constraints.

### Linear Scan

The allocator uses value intervals/lifetimes to assign registers efficiently, particularly where compilation speed is important.

### Greedy Allocation

The allocator assigns registers according to priorities, costs, and local constraints, potentially performing iterative reassignment and spilling.

### Priority-Based Allocation

Values may be prioritized according to properties such as lifetime, use frequency, spill cost, or other target-specific metrics.

### Constraint-Based Allocation

The allocator may formulate register assignment as a constraint-solving problem.

This can include approaches such as:

* PBQP
* Integer/Boolean constraint formulations
* Other combinatorial allocation techniques

RegTL should allow these methods to consume the same underlying allocation problem without requiring separate machine-level IRs.

---

# 12. Liveness and Lifetime

Register allocation fundamentally depends on value lifetime.

RegTL must provide sufficient information for an allocator to determine when a value:

* Becomes live
* Remains live
* Is used
* Is redefined
* Dies
* Crosses a basic block boundary
* Crosses a call
* Requires preservation

The exact liveness representation may differ between allocation algorithms.

RegTL should therefore provide semantic lifetime information while allowing an allocator to derive its preferred representation, such as:

```text
Live Intervals
Live Ranges
Interference Graphs
Use Lists
Dataflow Sets
Constraint Graphs
```

The canonical RegTL representation should not unnecessarily force one of these representations on all allocators.

---

# 13. Basic Blocks and Control Flow

RegTL should retain the control-flow information necessary for correct allocation.

This includes, where applicable:

* Basic blocks
* Block successors
* Block predecessors
* Terminators
* Branches
* Conditional branches
* Calls
* Returns
* Exceptional control flow
* Unreachable blocks

Register allocation must account for values whose lifetimes cross control-flow edges.

An allocator must not assume that RegTL is necessarily a single linear sequence.

---

# 14. Calls and ABI Boundaries

Calls are important register-allocation boundaries.

RegTL must represent information required to handle:

* Caller-saved registers
* Callee-saved registers
* Argument registers
* Return-value registers
* Call-clobbered registers
* Fixed-register arguments
* Fixed-register returns
* Variadic-call requirements
* Stack-passed arguments
* Register-passed arguments

The target ABI should provide the authoritative calling-convention information.

RegTL should represent the resulting allocation constraints without duplicating the complete ABI specification unnecessarily.

---

# 15. Spilling

When register pressure exceeds available physical registers, an allocator may spill values.

RegTL must support the representation of spill and reload operations or the information required to introduce them.

A spill may move a value:

```text
Register → Stack / Spill Location
```

A reload performs the reverse:

```text
Stack / Spill Location → Register
```

Spill decisions may be allocator-specific.

The RegTL representation should allow an allocator to introduce and optimize spills without corrupting the semantics of the original program.

Spill locations must be distinguishable from ordinary memory operands where required.

---

# 16. Register Coalescing

RegTL should support register-coalescing opportunities.

For example:

```text
v1 ← v2
```

may permit:

```text
v1 ≡ v2
```

when the semantic and lifetime constraints allow the transfer to disappear after allocation.

Coalescing should be treated as an optimization opportunity rather than an unconditional transformation.

The allocator must preserve all constraints involving the participating values.

---

# 17. Parallel Moves

Register allocation frequently introduces simultaneous register transfers.

RegTL should have a well-defined representation for **parallel moves** where required.

For example:

```text
r1 ← r2
r2 ← r1
```

must not be interpreted as two sequential operations when the intended semantics are simultaneous exchange.

The implementation may lower parallel moves into temporary-register or stack-assisted sequences during final lowering.

---

# 18. Register Pressure

RegTL should expose sufficient information for allocation strategies to reason about register pressure.

Possible sources include:

* Number of simultaneously live values
* Register-class pressure
* Block-local pressure
* Loop pressure
* Call-crossing pressure
* Spill costs
* Use frequency
* Hot/cold region information

The core RegTL representation should remain neutral about how an allocator computes its priority or cost model.

---

# 19. Allocation Cost Models

Different allocation algorithms may require different cost models.

A RegTL consumer may consider:

* Number of uses
* Weighted use frequency
* Loop depth
* Execution frequency
* Spill/reload cost
* Register-move cost
* Register-class pressure
* Instruction constraints
* Rematerialization cost
* Code-size impact
* Calling-convention cost

These policies belong to the allocator rather than the fundamental RegTL semantics.

RegTL should expose the facts necessary to construct these policies without mandating one universal cost function.

---

# 20. Rematerialization

Some values are cheaper to recompute than to spill and reload.

RegTL should support the identification of values or operations that can be **rematerialized**, where the target backend can establish that doing so is valid.

Examples may include:

* Constants
* Addresses
* Simple arithmetic expressions
* Target-specific immediate constructions

Rematerialization must preserve semantics and must respect target instruction constraints.

---

# 21. Subregisters and Register Aliasing

Targets may contain registers that overlap other registers.

Examples include:

```text
Full Register
    ├── Low Subregister
    └── High Subregister
```

RegTL must support target register aliasing where required.

An allocator must account for interference between overlapping physical registers.

A virtual register occupying a full register may therefore conflict with another value occupying one of its subregisters.

This information should be supplied by the target register model rather than duplicated inside individual allocation algorithms.

---

# 22. Register Allocation and Instruction Selection

RegTL sits after sufficient machine lowering has occurred to expose the constraints relevant to register allocation.

Instruction selection and register allocation may nevertheless interact.

The architecture should allow the compiler to represent alternatives when allocation decisions can affect instruction selection.

Examples include:

* Two-address instructions
* Register-specific instructions
* Immediate-vs-register alternatives
* Register-class-specific operations
* Instructions with tied operands
* Instructions requiring register pairs

RegTL must represent the constraints needed to resolve these interactions correctly.

---

# 23. Interaction with Limestone

RegTL is a subsystem of Limestone and should integrate with the surrounding compiler through explicit interfaces.

The expected relationship is:

```text
Limestone Middle End
        │
        ▼
Target Lowering
        │
        ▼
      RegTL
        │
        ▼
RegTL Allocator
        │
        ▼
Allocated Machine Representation
        │
        ▼
Target Backend
```

The RegTL subsystem should not depend directly on unrelated high-level compiler representations.

Dependencies should flow through well-defined interfaces.

---

# 24. Allocator Interface

An allocator consuming RegTL should conceptually implement:

```text
RegTL Program
      │
      ▼
Analyze Allocation Problem
      │
      ▼
Select Allocation Strategy
      │
      ▼
Assign Physical Registers
      │
      ▼
Insert / Resolve Transfers
      │
      ▼
Insert Spills / Reloads
      │
      ▼
Resolve Constraints
      │
      ▼
Allocated RegTL / Machine Representation
```

The allocator should not mutate semantic information in a way that makes the original allocation problem unrecoverable unless explicitly required by the API contract.

---

# 25. Multiple Allocators

RegTL should support multiple allocation implementations coexisting within Limestone.

For example:

```text
regtl/
├── core/
├── analysis/
├── model/
├── constraints/
├── alloc/
│   ├── graph/
│   ├── linear_scan/
│   ├── greedy/
│   ├── pbqp/
│   └── ...
└── lowering/
```

The exact directory organization may differ, but the architectural separation should remain.

Allocation algorithms should depend on RegTL's common model rather than on one another.

---

# 26. Verification

Register allocation is correctness-critical.

RegTL should provide verification facilities capable of detecting:

* Invalid physical-register assignments
* Register-class violations
* Register aliasing violations
* Invalid fixed-register assignments
* Broken tied operands
* Incorrect call-clobber handling
* Invalid spill locations
* Missing reloads
* Incorrect parallel-move lowering
* Lifetime violations
* Interference violations
* Invalid subregister assignments

Verification should be usable both during development and in compiler testing.

---

# 27. Debugging and Diagnostics

RegTL should provide facilities for inspecting the allocation problem before and after allocation.

Useful diagnostic information includes:

* Virtual registers
* Physical registers
* Register classes
* Value lifetimes
* Interference
* Constraints
* Spill decisions
* Coalescing decisions
* Register assignments
* Move insertion
* Rematerialization
* Allocation failures

Where practical, an allocator should be able to emit a representation suitable for human inspection.

---

# 28. Allocation Failures

An allocation failure should be explicit.

The allocator must distinguish between:

```text
Successful Allocation
Partial Allocation
Retry Required
Spill Required
Constraint Conflict
Unsupported Constraint
No Valid Allocation
Internal Error
```

An allocator must never silently assign an invalid register merely to complete compilation.

When allocation fails, diagnostics should identify the relevant:

* Value
* Register class
* Instruction
* Constraint
* Physical register set
* Interference
* Calling-convention requirement

where available.

---

# 29. Testing

Every RegTL change should include appropriate tests.

Testing should cover at least:

### Representation

* Construction
* Parsing/serialization if supported
* Value identity
* Register classes
* Constraints
* Control flow

### Allocation

* Simple allocation
* Register pressure
* Spilling
* Coalescing
* Calls
* Fixed registers
* Tied operands
* Register aliases
* Subregisters

### Algorithms

Each allocation algorithm should have its own correctness and regression tests while also being tested against shared RegTL cases.

### Verification

Invalid RegTL programs should be rejected by the verifier where possible.

---

# 30. Performance

Register allocation can be one of the most expensive parts of late-stage compilation.

Performance-sensitive code should avoid unnecessary:

* IR copying
* Allocation-state duplication
* Lifetime recomputation
* Constraint reconstruction
* Register-class lookup
* Graph rebuilding

However, performance optimizations must not compromise allocation correctness.

The representation should support efficient access to:

* Uses
* Definitions
* Basic blocks
* Constraints
* Register classes
* Lifetimes
* Interference information

---

# 31. Extensibility

RegTL should be designed so that new allocation techniques can be added without changing the fundamental IL.

Potential future allocation methods include:

* Graph coloring variants
* Linear scan variants
* Greedy allocation
* PBQP
* Constraint programming
* ILP-based allocation
* Hybrid allocators
* Profile-guided allocation
* Region-based allocation
* Target-specialized allocation

A new allocator should primarily implement an allocation strategy over the existing RegTL model.

It should not require a new compiler IR unless the existing RegTL abstraction genuinely cannot express the required allocation problem.

---

# 32. Design Principles

RegTL follows these principles:

1. **RegTL is an IL, not an allocator.**
2. **The IL describes the allocation problem; allocators solve it.**
3. **Multiple allocation algorithms must be able to consume the same RegTL representation.**
4. **Target-specific constraints must be explicit.**
5. **Virtual and physical registers must remain conceptually distinct.**
6. **Register classes are target-defined.**
7. **Lifetime and control-flow information must be sufficient for correct allocation.**
8. **ABI constraints are part of the allocation problem.**
9. **Spilling, coalescing, rematerialization, and parallel moves are first-class concerns.**
10. **Register aliasing and subregister relationships must be modeled explicitly where required.**
11. **Allocation failures must never result in silently invalid machine code.**
12. **The verifier is part of the correctness boundary.**
13. **Allocator-specific heuristics belong in allocator implementations, not in the fundamental RegTL semantics.**
14. **RegTL should remain useful as new allocation algorithms are introduced.**
15. **The representation should be sufficiently target-aware for correct allocation without becoming unnecessarily tied to one target architecture.**

---

# 33. Summary

**RegTL is Limestone's Register Transfer Language and register-allocation intermediate language.**

Its purpose is to provide a common, machine-aware representation of register-transfer operations, virtual registers, physical-register constraints, lifetimes, register classes, ABI requirements, and allocation-related transformations.

The architectural boundary is:

```text
                 Limestone
                     │
                     ▼
             Machine Lowering
                     │
                     ▼
                   RegTL
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
   Graph Color   Linear Scan    Greedy
        │            │            │
        └────────────┼────────────┘
                     │
              Other Allocators
                     │
                     ▼
             Physical Registers
                     │
                     ▼
              Machine Backend
```

The central goal is to make **register allocation a replaceable backend strategy over a stable intermediate language**, rather than coupling Limestone's machine representation to one particular register-allocation algorithm.
