# Blackstone — Agent Implementation Guide

**Document type:** Normative project specification and implementation guide
**Project:** Blackstone
**Parent project:** `libstone`
**Primary concern:** Declarative database of compiler and interpreter optimization kernels
**Kernel language:** Optilisp (`.olisp`)
**Intermediate representation:** NodeIR
**IR stages:** NodeHIR, NodeMIR, NodeLIR

---

## 1. Mission and Project Identity

Blackstone is a declarative database of compiler and interpreter optimization algorithms. It belongs to the `libstone` project family and provides a structured, machine-readable catalog of individual optimization techniques.

Blackstone is not itself a compiler, interpreter, optimizer execution engine, or intermediate representation implementation. Its primary deliverable is a collection of documented, declarative optimization kernels, together with the metadata, schemas, validation rules, and supporting tooling needed to discover and reason about those kernels.

A Blackstone kernel describes one optimization, its prerequisites, its target IR stage, the information it requires, its correctness constraints, and the transformation it intends to perform.

The database must support both conventional compiler optimizations and advanced optimization techniques, including:

* Constant folding and constant propagation.
* Algebraic simplification and canonicalization.
* Copy propagation and value numbering.
* Dead-code and dead-store elimination.
* Control-flow simplification and unreachable-block elimination.
* Common-subexpression elimination.
* Loop-invariant code motion and loop transformations.
* Strength reduction and induction-variable optimization.
* Register-pressure and instruction-cost-oriented transformations.
* Data-flow, control-flow, and program-dependence-based optimization.
* Polyhedral analysis and loop-nest transformations.
* Alias analysis-dependent transformations.
* SSA-based optimizations.
* Target-specific instruction-level optimizations.
* Interpreter bytecode and dispatch optimizations.
* Partial evaluation and specialization.
* Interprocedural and whole-program optimizations, where the required program information is available.

This list is illustrative rather than exhaustive. The database must be extensible without requiring every optimization to fit into a single predefined taxonomy.

### 1.1. Core design principles

All implementation work MUST preserve the following principles.

1. **One kernel, one optimization.** Each kernel has one primary transformation objective.
2. **Declarative descriptions.** Kernels describe transformations and their contracts rather than implementing them as arbitrary imperative programs.
3. **Explicit applicability.** A kernel must declare the conditions under which it is valid.
4. **Explicit IR stage.** A kernel must identify its intended NodeIR stage.
5. **Explicit information requirements.** A kernel must declare which analyses, facts, and annotations it requires.
6. **Correctness first.** A transformation must preserve the program's required observable behavior under its stated assumptions.
7. **Machine-readable metadata.** Kernel metadata must be parseable, validated, indexed, and queryable.
8. **First-class documentation.** Documentation is part of the kernel specification, not an optional comment.
9. **Composability without hidden assumptions.** Kernel dependencies and conflicts must be represented explicitly.
10. **Separation of concerns.** The kernel database, NodeIR implementation, optimization planner, transformation executor, and target-specific backends must remain distinct.
11. **Reproducibility.** Validation and tests must produce deterministic results where their inputs and environment are deterministic.
12. **Extensibility.** New optimization paradigms, analyses, monoids, and IR attributes must be introducible without redesigning the entire database.

### 1.2. What Blackstone is not

Do not turn Blackstone into a monolithic optimizer.

Blackstone should not independently reimplement NodeIR, a compiler front end, a register allocator, a code generator, or a complete analysis framework simply because an optimization may depend on one of these components.

Instead, Blackstone records the optimization's requirements and describes how the optimization relates to the corresponding facilities.

For example, a dead-store-elimination kernel may require memory-dependence information. It does not follow that Blackstone must implement an alias analysis engine inside the kernel definition.

Likewise, a polyhedral loop-transformation kernel may recommend an external polyhedral library without requiring Blackstone to embed that library.

---

## 2. Relationship to the `libstone` Ecosystem

Blackstone is a member of the `libstone` project family.

Its neighboring projects have distinct responsibilities.

| Project     | Responsibility                                                                           |
| ----------- | ---------------------------------------------------------------------------------------- |
| Blackstone  | Optimization-kernel database and optimization descriptions                               |
| Yellowstone | Object-file formats, linkers, binary utilities, and related facilities                   |
| Sandstone   | Target architectures, assemblers, and architecture-related descriptions                  |
| Limestone   | Instruction selection, instruction scheduling, and related low-level compiler facilities |
| Firestone   | Intermediate representations, IR modules, and associated infrastructure                  |

The exact boundaries of these projects must follow their existing implementations and specifications. Do not assume that a neighboring project's API, schema, or file layout exists merely because its responsibility is described here.

### 2.1. Integration rules

When implementing Blackstone:

* Reuse established `libstone` conventions where they are already implemented.
* Avoid duplicating functionality that belongs to another library.
* Define explicit integration interfaces rather than depending on undocumented internals.
* Keep Blackstone's optimization metadata independent of any single compiler backend.
* Represent backend-specific requirements as target constraints or capability requirements.
* Do not make all kernels depend on every `libstone` component.

For example, a high-level constant-folding kernel should not require Yellowstone or Sandstone merely because other kernels use object files or architecture descriptions.

Conversely, a low-level instruction-combination kernel may legitimately require architecture information supplied by Sandstone.

---

## 3. Terminology

The following terms have specific meanings in this specification.

### 3.1. Database

The **database** is the complete collection of kernel definitions, associated documentation, schemas, manifests, and supporting indexes.

It is a logical database. Its storage may consist of ordinary files, generated indexes, or a dedicated database implementation if the repository explicitly requires one.

Do not assume that a particular database engine is required.

### 3.2. Kernel

A **kernel** is a declarative specification of exactly one optimization.

A kernel includes:

* A stable identity.
* A descriptive name.
* A target IR stage.
* A transformation category.
* Applicability constraints.
* Required analyses and metadata.
* Required and forbidden prior transformations, when relevant.
* The intended transformation.
* Semantic preconditions.
* Correctness obligations.
* Expected effects and limitations.
* Documentation.
* Validation and test references, where supported.

A kernel may describe a local rewrite, a graph-wide transformation, a control-flow transformation, a loop transformation, or a transformation that requires whole-program information.

The term *kernel* does not imply GPU execution, a hardware kernel, or an imperative function.

### 3.3. Optilisp

**Optilisp** is Blackstone's declarative S-expression language for expressing kernel definitions, metadata, transformation descriptions, documentation, and related specifications.

Its syntax and semantics must be defined by the actual language implementation and grammar.

The examples in this document that use proposed forms such as `define-kernel`, `requires-analysis`, or `rewrite` are illustrative specification examples. They MUST NOT be treated as evidence that these forms already exist in the repository.

Before implementing syntax, inspect the existing Optilisp grammar, parser, evaluator or compiler, standard library, and examples. Extend established constructs where possible.

### 3.4. NodeIR

**NodeIR** is the graph-oriented intermediate representation against which Blackstone kernels are specified.

NodeIR represents program structure through nodes and their relationships. It may carry expressions, operations, types, control-flow information, data dependencies, memory dependencies, source locations, and analysis annotations, depending on the selected stage and implementation.

NodeIR is not synonymous with a particular compiler's IR.

### 3.5. NodeHIR, NodeMIR, and NodeLIR

The three named stages represent different levels of abstraction.

* **NodeHIR:** High-level representation, preserving high-level operations and structures where possible.
* **NodeMIR:** Mid-level representation, exposing more explicit control flow, data dependencies, and lower-level operational details.
* **NodeLIR:** Low-level representation, suitable for transformations close to target instructions, machine operations, or other low-level execution forms.

The precise guarantees of each stage must be documented by the NodeIR implementation.

Do not assume that every NodeHIR program contains source-language types, that every NodeMIR program is in SSA form, or that every NodeLIR program has already undergone register allocation.

These properties must be expressed as explicit capabilities or invariants.

### 3.6. Optimization paradigm

An **optimization paradigm** describes the conceptual framework or algorithmic family used by a kernel.

Examples include:

* Algebraic rewriting.
* Data-flow analysis.
* SSA-based optimization.
* Graph rewriting.
* Control-flow optimization.
* Dependence analysis.
* Polyhedral optimization.
* Abstract interpretation.
* Profile-guided optimization.
* Interprocedural optimization.
* Cost-model-guided optimization.
* Target-specific instruction optimization.

A paradigm classifies a kernel. It does not automatically prove that the kernel is applicable.

### 3.7. Analysis

An **analysis** computes facts about a program without necessarily changing the program.

Examples include constant-value analysis, dominator analysis, alias analysis, liveness analysis, dependence analysis, and loop-nest analysis.

A kernel may require the results of an analysis as an input capability.

### 3.8. Transformation

A **transformation** changes the representation of a program.

Examples include replacing an expression, deleting an unreachable block, moving an instruction, rewriting a loop nest, or eliminating a redundant memory operation.

A transformation must satisfy its declared correctness conditions.

### 3.9. Monoid

A **monoid** is an algebraic structure consisting of a carrier set, an associative binary operation, and an identity element.

For a monoid \((M,\oplus,e)\):

* \(M\) is the set of values.
* \(\oplus\) is an associative operation.
* \(e\) is the identity element.

The defining laws are:

$$
(a\oplus b)\oplus c = a\oplus(b\oplus c)
$$

and

$$
e\oplus a = a = a\oplus e
$$

Blackstone uses monoidal abstractions to describe composition and accumulation where the semantics are appropriate.

A monoid must not be used merely to make a description look algebraic. The chosen operation must satisfy its laws under the specified semantics.

---

## 4. NodeIR Stage Model

Every kernel MUST declare its intended NodeIR stage.

The canonical stage identifiers are:

* `hir`
* `mir`
* `lir`

Use the repository's established identifiers if they differ. Do not introduce aliases without a documented normalization rule.

### 4.1. Stage selection

Choose the stage at which the transformation's assumptions are naturally expressed and its intended effects are valid.

Examples:

| Optimization                            | Typical stage    | Reason                                                      |
| --------------------------------------- | ---------------- | ----------------------------------------------------------- |
| Source-level constant folding           | HIR              | High-level typed expressions may be available               |
| Algebraic simplification                | HIR or MIR       | Depends on types and operator semantics                     |
| Sparse conditional constant propagation | MIR              | Often benefits from explicit control flow and SSA           |
| Dead-code elimination                   | HIR, MIR, or LIR | Depends on observability and representation                 |
| Loop-invariant code motion              | MIR              | Requires suitable loop and dependence information           |
| Polyhedral loop interchange             | HIR or MIR       | Requires an analyzable loop nest and dependence constraints |
| Register allocation preparation         | LIR              | Requires low-level liveness and machine constraints         |
| Target-specific instruction combining   | LIR              | Depends on target operations and instruction semantics      |

These are typical choices, not universal rules.

For example, dead-code elimination is not inherently an LIR-only transformation. A high-level kernel can eliminate an unused pure expression, while a low-level kernel can eliminate an unneeded machine operation if its effects are proven irrelevant.

### 4.2. Stage contracts

Each stage should have a documented contract describing:

* Supported node kinds.
* Supported operand relationships.
* Type-information guarantees.
* Control-flow representation.
* Memory-effect representation.
* SSA or non-SSA properties.
* Permitted analysis annotations.
* Relevant invariants.
* Any restrictions on transformation results.

A kernel must not silently depend on an invariant that its target stage does not guarantee.

### 4.3. Example: HIR-only optimization

The following is illustrative Optilisp-style notation:

```lisp
(define-kernel
  :id "blackstone.hir.constant-folding.integer-add"
  :name "Integer Addition Constant Folding"
  :stage hir

  (requires
    (capability typed-expressions)
    (capability integer-constant-operands))

  (forbids
    (assumption signed-overflow-is-undefined))

  (transform
    (rewrite
      (add (integer a) (integer b))
      (integer-result
        (evaluate-integer-add a b)))))
```

The implementation must define what `evaluate-integer-add` means for each supported integer type.

The rewrite is not valid merely because both operands are constants. The evaluator must preserve the IR's overflow, trapping, wrapping, and exceptional-behavior semantics.

### 4.4. Example: LIR-only optimization

```lisp
(define-kernel
  :id "blackstone.lir.target.instruction-combine"
  :name "Target Instruction Combination"
  :stage lir

  (requires
    (capability machine-operations)
    (capability target-description)
    (capability instruction-cost-model))

  (transform
    (target-rewrite
      (sequence
        (machine-op :kind "ADD")
        (machine-op :kind "COMPARE"))
      (target-pattern
        :name "combined-arithmetic-condition-pattern"))))
```

This example describes the intended shape of a target-specific kernel. The actual instruction patterns and result form must be defined by the target and NodeIR schemas.

A kernel must not invent a target instruction or assume that an instruction exists on all architectures.

---

## 5. Kernel Metadata and Applicability Contracts

Kernel metadata is a central part of Blackstone's functionality.

Documentation explains a kernel to humans. Metadata allows tooling to determine whether a kernel is suitable for a particular program, what information must be computed first, and what other transformations may be required.

### 5.1. Required metadata categories

Every kernel MUST provide, directly or through a schema-defined default, the following information:

1. Stable identifier.
2. Human-readable name.
3. Target IR stage.
4. Optimization category.
5. Primary optimization objective.
6. Applicability conditions.
7. Required NodeIR capabilities.
8. Required analyses.
9. Relevant assumptions about prior transformations.
10. Semantic preconditions.
11. Transformation description.
12. Correctness obligations.
13. Expected effects and limitations.
14. First-class documentation.

The concrete serialization of these fields must follow the repository's Optilisp conventions.

If a field is required by the schema, an empty value must not be used to evade validation.

### 5.2. Required and forbidden prior optimizations

A kernel may require certain transformations to have been performed already.

For example, an optimization might require the program to be in SSA form or require loops to have been normalized.

A kernel may also require that a transformation has not yet occurred.

For example, a loop transformation may require the original loop structure to remain intact.

Represent these requirements as explicit metadata.

Illustrative notation:

```lisp
(prerequisites
  (optimization-applied
    "blackstone.mir.ssa-construction")

  (optimization-applied
    "blackstone.mir.loop-simplification"))
```

Illustrative forbidden-state declaration:

```lisp
(forbidden-prior-transformations
  "blackstone.lir.register-allocation")
```

The second example is appropriate only if the kernel actually requires an unallocated representation. Register allocation does not automatically invalidate every low-level optimization.

### 5.3. Distinguish transformation history from current properties

An important implementation distinction is the difference between a transformation having occurred and a program currently satisfying a property.

For example:

* "SSA construction has been applied" is historical information.
* "The program is in SSA form" is a property of the current IR.
* "The program has not been transformed since SSA construction" is a stronger historical condition.
* "The current SSA form has valid dominance relationships" is an invariant that must be checked or guaranteed.

Prefer checking the current property when that is sufficient.

Use transformation history only when the history itself matters.

A kernel should not reject a valid program merely because it reached the required state through a different sequence of transformations.

### 5.4. Positive and negative applicability conditions

Applicability contracts must support both positive and negative requirements.

Positive examples:

* Integer types are known.
* Dominator information is available.
* The control-flow graph is reducible.
* Memory-dependence information is available.
* The target supports a particular operation.
* Loop bounds satisfy a declared mathematical constraint.

Negative examples:

* An operation may have observable side effects.
* Alias information is insufficient to prove independence.
* The target lacks the required instruction.
* A loop contains an unsupported exceptional exit.
* Floating-point reassociation is not permitted under the active semantics.
* The representation violates an invariant required by the transformation.

A kernel must not interpret missing information as proof that a condition holds.

For example, unknown aliasing does not mean that two memory references are independent.

### 5.5. Example: kernel with complete applicability metadata

The following is illustrative:

```lisp
(define-kernel
  :id "blackstone.mir.dead-store-elimination"
  :name "Dead Store Elimination"
  :stage mir

  (category memory-optimization)

  (objective
    "Remove a store whose written value cannot be observed.")

  (requires
    (capability memory-operations)
    (analysis memory-dependence)
    (analysis observable-memory-effects))

  (preconditions
    (proven-store-is-overwritten-before-observation)
    (proven-no-intervening-read-of-overwritten-value))

  (forbids
    (unknown-memory-dependence)
    (unmodeled-observable-store-effects))

  (transform
    (eliminate
      (store :id overwritten-store)))

  (correctness
    (preserve-observable-memory-behavior))

  (documentation
    "Remove a store only when the earlier stored value cannot
     affect any permitted observable behavior."))
```

A production schema should additionally define the scope of the memory model, exceptional behavior, volatile and atomic operations, concurrency assumptions, and treatment of external observers.

Do not implement dead-store elimination as a textual pattern that deletes an earlier store whenever another store targets the same apparent address.

---

## 6. One Kernel, One Optimization

Each kernel MUST have one primary optimization objective.

This is a structural requirement, not merely a naming convention.

### 6.1. Acceptable kernel boundaries

The following should ordinarily be separate kernels:

* Integer constant folding.
* Floating-point constant folding.
* Constant propagation.
* Copy propagation.
* Common-subexpression elimination.
* Dead-code elimination.
* Dead-store elimination.
* Loop-invariant code motion.
* Loop interchange.
* Loop fusion.
* Strength reduction.
* Instruction combining.

These transformations have different preconditions, correctness rules, analysis dependencies, and expected effects.

Combining them into one kernel makes applicability harder to determine and testing less precise.

### 6.2. What counts as one optimization?

A kernel may perform multiple coordinated edits if those edits constitute one logical optimization.

For example, a single loop-interchange kernel may update loop nesting, iteration bounds, and associated control-flow edges as one transformation.

Likewise, an SSA-based dead-code-elimination kernel may delete several unused instructions in a single pass.

The requirement is one coherent optimization objective, not one changed node.

### 6.3. Avoid omnibus kernels

Do not create kernels such as:

```lisp
(define-kernel
  :id "blackstone.optimize-everything"
  :name "General Optimization"

  (transform
    (constant-fold)
    (dead-code-eliminate)
    (inline-functions)
    (unroll-loops)
    (vectorize-loops)))
```

This is not a suitable kernel specification.

Each operation must have its own identity, applicability conditions, correctness contract, documentation, and tests.

### 6.4. Kernel decomposition example

A constant-folding family could be organized as:

```text
constant-folding/
    integer-add.olisp
    integer-sub.olisp
    integer-mul.olisp
    integer-div.olisp
    integer-comparison.olisp
    boolean-logic.olisp
    floating-point-arithmetic.olisp
    cast-constants.olisp
```

The precise number of kernels should follow meaningful differences in semantics and applicability, not an arbitrary requirement to create one file for every operator.

For example, several integer arithmetic operations may share a reusable definition mechanism if the resulting kernels remain independently identifiable and their semantics remain explicit.

---

## 7. Optilisp Language Requirements

Optilisp is a declarative S-expression language.

Its role is to express optimization descriptions in a form that is both understandable to compiler developers and processable by tools.

### 7.1. Declarative semantics

An Optilisp kernel should specify:

* What optimization is being described.
* When the optimization is applicable.
* What facts it consumes.
* What transformation is intended.
* What properties must remain true.
* What result properties are expected.

A kernel should not encode its entire implementation as arbitrary imperative code.

The transformation executor may use imperative algorithms internally, but the kernel database must preserve a declarative description of the algorithm's intent and constraints.

### 7.2. S-expression conventions

Follow the existing Optilisp syntax.

As a general design guideline:

* Use symbolic forms for structured declarations.
* Use keywords or explicit field names for metadata where supported.
* Use stable identifiers for references.
* Use nested expressions for structured conditions and transformations.
* Keep definitions syntactically unambiguous.
* Avoid relying on positional arguments when the schema needs extensible metadata.
* Keep reusable definitions separate from individual kernel declarations.
* Make parser errors identify the location and context of the invalid form.

Illustrative structure:

```lisp
(define-kernel
  :id "blackstone.hir.algebraic.identity-add-zero"
  :name "Additive Identity Simplification"
  :stage hir

  (category algebraic-simplification)

  (requires
    (capability typed-expressions))

  (preconditions
    (operand-has-compatible-integer-type))

  (transform
    (rewrite
      (add x (integer 0))
      x))

  (documentation
    "Replace integer addition by zero with the original value,
     provided the IR semantics establish the required identity."))
```

This is a proposed notation, not a claim about the current parser.

### 7.3. Do not silently extend the language

Before adding new forms:

1. Inspect the current grammar and parser.
2. Identify existing constructs with equivalent meaning.
3. Determine whether the feature belongs in core syntax or a library.
4. Specify its semantics.
5. Update the grammar and parser together.
6. Add positive and negative parser tests.
7. Update language documentation and examples.
8. Verify that existing kernel files remain valid.

Do not add syntax that the parser ignores or that has no defined meaning.

### 7.4. Syntax and semantics must be separate

The parser determines whether a source file is structurally valid.

Semantic validation determines whether the parsed definition is meaningful.

For example, a parser may successfully read:

```lisp
(define-kernel
  :id "blackstone.hir.example"
  :name "Example"
  :stage hir

  (requires
    (analysis non-existent-analysis)))
```

The parser need not know whether `non-existent-analysis` exists. A semantic validation pass should detect an unresolved analysis identifier if the database declares that identifier invalid.

Similarly, a syntactically valid kernel can still be semantically incorrect if its transformation violates the NodeIR contract.

---

## 8. Monoids and Algebraic Descriptions

Optilisp uses algebraic abstractions, including monoids, to express composition and accumulation where these concepts are appropriate.

The project must provide or reference a standard library of monoidal definitions.

### 8.1. Monoid requirements

Every standard-library monoid must define:

* A stable identifier.
* A carrier type or value domain.
* A binary operation.
* An identity element.
* The applicable equality or equivalence relation.
* The conditions under which the monoid laws hold.
* Any restrictions imposed by the representation.

Where appropriate, include an explicit statement of associativity and identity laws.

Do not infer that an operation is associative simply because it is named `combine`, `merge`, `append`, or `compose`.

### 8.2. Example: integer addition

A conventional integer-addition monoid can be represented as:

```lisp
(define-monoid
  :id "blackstone.monoid.integer-addition"
  :carrier integer
  :operation integer-add
  :identity 0

  (laws
    (associative integer-add)
    (left-identity integer-add 0)
    (right-identity integer-add 0)))
```

The actual syntax and integer domain must be established by the Optilisp implementation.

If arithmetic uses fixed-width machine integers, the specification must state whether the operation is modular, trapping, undefined on overflow, or governed by another rule.

An algebraic law may hold over mathematical integers but fail under another machine-arithmetic model.

### 8.3. Example: Boolean conjunction

```lisp
(define-monoid
  :id "blackstone.monoid.boolean-and"
  :carrier boolean
  :operation boolean-and
  :identity true

  (laws
    (associative boolean-and)
    (left-identity boolean-and true)
    (right-identity boolean-and true)))
```

This monoid is suitable for combining Boolean conditions when the intended semantics match the declared Boolean algebra.

### 8.4. Example: sequence composition

```lisp
(define-monoid
  :id "blackstone.monoid.sequence-composition"
  :carrier node-sequence
  :operation sequence-concatenate
  :identity empty-sequence

  (laws
    (associative sequence-concatenate)
    (left-identity sequence-concatenate empty-sequence)
    (right-identity sequence-concatenate empty-sequence)))
```

This example illustrates a mathematical structure, not an instruction to treat every sequence of compiler operations as safely reorderable.

Sequence concatenation is associative, but program execution order may still be semantically significant.

### 8.5. Example: transformation composition

Transformation composition requires particular care.

Suppose \(T_1\) and \(T_2\) are transformations. Their composition is valid only when the output state accepted by one transformation is compatible with the input state expected by the next.

A useful conceptual representation is:

```lisp
(define-composition
  :id "blackstone.transform.compose"

  (compose
    (transformation first)
    (transformation second))

  (requires
    (output-contract first)
    (satisfies-input-contract
      first
      second)))
```

This is illustrative notation.

A transformation-composition facility must account for preconditions, postconditions, stage compatibility, invalidated analyses, and possible changes to the program's semantic assumptions.

Do not treat every transformation as an element of one universal monoid without defining a suitable carrier and composition operation.

### 8.6. Monoids are not a substitute for transformation semantics

A monoid describes algebraic composition. It does not, by itself, prove that a compiler optimization preserves behavior.

For example, algebraic reassociation may be valid for mathematical integer addition but invalid for floating-point addition under strict IEEE 754 semantics.

The kernel must state the conditions under which the algebraic transformation is permitted.

### 8.7. Standard-library organization

A useful monoid library may include:

```text
optilisp/
    stdlib/
        monoids/
            integer-addition.olisp
            integer-multiplication.olisp
            boolean-and.olisp
            boolean-or.olisp
            set-union.olisp
            set-intersection.olisp
            sequence-composition.olisp
            map-merge.olisp
            diagnostic-accumulation.olisp
```

These filenames are proposed examples. Adapt them to the actual repository.

Each definition must be validated independently and tested against its declared laws where those laws are executable.

---

## 9. Documentation Is Part of the Kernel

Optilisp documentation is first-class.

A kernel's documentation should be available to both human readers and tooling, rather than existing only as an unstructured comment that disappears during parsing.

The exact documentation syntax must follow the Optilisp language implementation.

### 9.1. Required documentation topics

Document every kernel's:

1. Purpose.
2. Motivation.
3. Intended IR stage.
4. Applicable program patterns.
5. Preconditions.
6. Required analyses and annotations.
7. Transformation behavior.
8. Semantic assumptions.
9. Correctness considerations.
10. Known limitations.
11. Expected effects.
12. Testing strategy.
13. Relevant references.
14. Recommended third-party libraries or tools, where applicable.

A kernel may explicitly state that no external library is recommended.

Do not invent a library recommendation simply to fill a documentation field.

### 9.2. Documentation must be precise

Avoid descriptions such as:

> This optimization makes the program faster by simplifying it.

This statement is too vague to be useful.

Prefer:

> This kernel replaces a redundant integer addition by zero with its nonconstant operand when the operand and result have compatible integer semantics. It does not authorize floating-point reassociation or transformations that change observable overflow behavior.

The latter identifies a transformation and its semantic boundaries.

### 9.3. Document limitations, not just intended use

For example, a loop-invariant-code-motion kernel might document:

* It requires a valid loop representation.
* It requires the candidate expression to be invariant with respect to the loop.
* It requires memory and side-effect information when the expression may access memory.
* It must not move a potentially trapping operation to a path on which it previously did not execute unless the semantics permit doing so.
* It may increase register pressure or code size.
* It does not automatically imply that moving the expression improves performance.

This level of precision helps both implementers and optimization planners.

### 9.4. Third-party library recommendations

A kernel may document useful external implementations, papers, libraries, or analysis tools.

Recommendations should explain the role of the dependency.

For example:

```lisp
(documentation
  (purpose
    "Identify affine loop nests suitable for dependence-aware
     loop transformations.")

  (recommended-tools
    (tool
      :name "isl"
      :role "Integer-set and dependence-related analysis"
      :optional true))

  (limitations
    "The recommendation does not imply that Blackstone depends
     on the library or that every loop nest is representable." ))
```

This is illustrative syntax. Confirm actual capabilities, licensing, APIs, and compatibility before documenting a third-party tool as an implementation recommendation.

Separate three different concepts:

* **Recommended tool:** A possible implementation aid.
* **Required dependency:** A dependency needed by the implementation.
* **Required capability:** An abstract property that the execution environment must supply.

Do not conflate them.

---

## 10. Optimization Categories and Paradigms

Blackstone should use a structured, extensible taxonomy.

Categories describe the primary objective of an optimization. Paradigms describe the methods or theoretical frameworks it uses. These are related but distinct concepts.

For example, common-subexpression elimination is an optimization category, while value numbering is one possible approach.

### 10.1. Suggested optimization categories

```text
algebraic-simplification
constant-evaluation
constant-propagation
copy-propagation
redundancy-elimination
dead-code-elimination
dead-store-elimination
control-flow-simplification
data-flow-optimization
ssa-optimization
memory-optimization
loop-optimization
polyhedral-optimization
interprocedural-optimization
specialization
vectorization
parallelization
register-pressure-optimization
instruction-combination
code-size-optimization
profile-guided-optimization
interpreter-optimization
```

Treat this list as an initial vocabulary, not a closed enumeration.

### 10.2. Suggested paradigms

```text
algebraic-rewriting
graph-rewriting
data-flow-analysis
abstract-interpretation
ssa-based
control-flow-graph
dependence-analysis
polyhedral
symbolic-execution
cost-model-guided
profile-guided
interprocedural
target-specific
rule-based
equality-saturation
```

A kernel may use multiple paradigms if the metadata supports that distinction.

### 10.3. Example

A sparse conditional constant-propagation kernel might be classified as:

```lisp
(category constant-propagation)

(paradigms
  ssa-based
  data-flow-analysis
  control-flow-analysis)
```

A polyhedral loop-interchange kernel might use:

```lisp
(category polyhedral-optimization)

(paradigms
  polyhedral
  dependence-analysis
  cost-model-guided)
```

These classifications must not be treated as proof that the required analyses or cost model are available. Those requirements belong in the applicability contract.

---

## 11. Analyses, Capabilities, and Annotations

Kernels frequently require facts that are not directly encoded in the basic NodeIR graph.

Blackstone must distinguish between:

* IR structure.
* Static properties of the IR.
* Results computed by analyses.
* Target capabilities.
* Profile information.
* Execution-environment guarantees.

### 11.1. Common capability identifiers

An extensible vocabulary may include:

```text
typed-expressions
explicit-control-flow
ssa-form
dominance-information
post-dominance-information
loop-information
loop-normalization
alias-information
memory-dependence
data-dependence
liveness-information
reaching-definitions
constant-value-information
range-information
exception-information
side-effect-information
call-graph
interprocedural-summaries
profile-information
target-description
instruction-cost-model
register-constraints
floating-point-semantics
atomic-memory-semantics
```

The repository must define canonical identifiers and their meanings.

Avoid creating several synonymous capability names for the same property.

### 11.2. Analyses can become stale

A transformation may invalidate one or more analysis results.

For example:

* Rewriting control flow may invalidate dominator information.
* Changing memory operations may invalidate memory-dependence information.
* Modifying definitions may invalidate liveness information.
* Altering loops may invalidate loop-nest information.

A kernel must declare the analyses it consumes and, where supported, the analyses it preserves or invalidates.

Illustrative notation:

```lisp
(analysis-contract
  (requires
    dominator-information
    loop-information)

  (invalidates
    dominator-information
    loop-information
    liveness-information))
```

This is particularly important for optimization pipelines that execute multiple kernels consecutively.

### 11.3. Missing facts must remain unknown

An analysis result may be:

* Proven true.
* Proven false.
* Unknown.
* Not computed.
* Invalidated.

The implementation must not silently collapse these states into a Boolean result.

For example, if a dependence analysis cannot prove that two accesses are independent, a loop-transformation kernel must not assume independence.

---

## 12. Correctness and Semantic Preservation

Correctness is a primary property of every kernel.

A transformation that produces a simpler graph but changes observable program behavior is not a valid optimization unless the declared execution model explicitly permits the change.

### 12.1. Semantic equivalence

At an abstract level, a semantics-preserving optimization should ensure that the original and transformed programs are equivalent under the declared language and execution model.

The precise equivalence relation may be:

* Exact equivalence of observable behavior.
* Refinement under a defined nondeterministic semantics.
* Equivalence under explicitly stated assumptions.
* Another rigorously specified relation required by the IR.

Do not assume that syntactic similarity proves semantic equivalence.

### 12.2. Sources of semantic hazards

Kernel authors must consider the relevant hazards, including:

* Integer overflow and underflow.
* Division by zero.
* Signed versus unsigned arithmetic.
* Floating-point rounding and exceptions.
* NaNs, infinities, and signed zero.
* Undefined, unspecified, or implementation-defined behavior.
* Pointer aliasing.
* Volatile memory accesses.
* Atomic operations and memory ordering.
* Exceptions and traps.
* Function calls and external side effects.
* Undefined values and poison-like IR semantics.
* Control-flow reachability.
* Concurrency and data races.
* Target-specific instruction behavior.

Not every kernel requires discussion of every item. Document those that can affect its validity.

### 12.3. Example: integer identity

Consider:

```text
x = y + 0
```

Replacing this with:

```text
x = y
```

may be valid for a suitable integer-addition operation.

However, the kernel must still respect:

* The types of `y` and `x`.
* The semantics of the addition operation.
* Any relevant instrumentation or observable-operation rules.
* The actual semantics of the NodeIR operator.

Do not generalize the rule to arbitrary overloaded operators or floating-point expressions without examining their semantics.

### 12.4. Example: floating-point reassociation

Consider:

```text
x = (a + b) + c
```

A reassociation kernel might propose:

```text
x = a + (b + c)
```

This transformation is not generally equivalent under strict floating-point semantics because rounding may occur at different points.

A valid kernel must require a suitable floating-point contract or explicit permission for reassociation.

Illustrative metadata:

```lisp
(preconditions
  (floating-point-reassociation-permitted)
  (operands-have-compatible-floating-point-types))
```

The execution environment must provide evidence that the preconditions hold.

### 12.5. Correctness must not depend on documentation alone

A sentence in a docstring does not enforce a precondition.

Where possible, important constraints must be represented in machine-readable metadata, checked by a validator, or enforced by the transformation executor.

Documentation should explain the contract. Executable validation should enforce it.

---

## 13. Example Kernel Catalog

This section provides illustrative examples for common optimization families. They demonstrate the expected degree of specificity rather than establishing a definitive Optilisp grammar.

Before copying these examples into the repository, reconcile their syntax with the actual language implementation.

### 13.1. Constant folding

Objective: Evaluate a supported constant expression at compile time.

Example input:

```text
x = 6 * 7
```

Expected conceptual result:

```text
x = 42
```

Illustrative kernel:

```lisp
(define-kernel
  :id "blackstone.hir.constant-folding.integer-multiply"
  :name "Integer Multiplication Constant Folding"
  :stage hir

  (category constant-evaluation)

  (requires
    (capability typed-expressions)
    (capability integer-constant-operands))

  (preconditions
    (operation-has-defined-compile-time-semantics)
    (result-representable-under-integer-semantics))

  (transform
    (rewrite
      (multiply (integer a) (integer b))
      (evaluate-integer-multiply a b)))

  (documentation
    "Evaluate integer multiplication when both operands are
     constants and the result can be represented according to
     the declared integer semantics."))
```

Test cases should include:

* Ordinary positive operands.
* Negative operands, where supported.
* Zero.
* One.
* Minimum and maximum representable values.
* Overflow boundaries.
* Unsupported or malformed constant values.
* Different integer widths.
* Signed and unsigned arithmetic.

The kernel must not produce a host-language result that differs from the target IR's arithmetic semantics.

### 13.2. Constant propagation

Objective: Replace a use of a value with a known constant when the relevant data-flow facts prove that the substitution is valid.

Example:

```text
a = 12
b = a
c = b + 5
```

Conceptual result:

```text
a = 12
b = 12
c = 17
```

Constant propagation and constant folding should remain distinct kernels. One propagates known values; the other evaluates expressions whose operands are known constants.

The example illustrates their possible composition, not a requirement that a single kernel perform both.

### 13.3. Copy propagation

Objective: Replace a use of a copied value with its source when the copy remains valid and the relevant definitions have not been invalidated.

Example:

```text
b = a
c = b
```

Conceptual result:

```text
b = a
c = a
```

This example assumes a representation where the assignments are eligible for this simplification. The implementation must account for SSA properties, aliasing, mutable storage, and other relevant IR semantics.

Copy propagation must not be implemented as unrestricted textual substitution.

### 13.4. Dead-code elimination

Objective: Remove computations whose results cannot affect observable behavior and whose execution has no observable effect.

Example:

```text
a = 3
b = 4
c = a + b
return c
```

If the value of `c` is replaced with a constant by another kernel, the assignments may become dead. A dead-code-elimination kernel can remove eligible instructions once their results are unused.

Do not delete an instruction solely because its result is unused. It may still trap, perform a volatile access, call an effectful function, or have another observable effect.

### 13.5. Common-subexpression elimination

Objective: Eliminate redundant evaluations when a previously computed expression remains valid at the later use.

Example:

```text
a = x + y
b = x + y
```

Conceptual result:

```text
a = x + y
b = a
```

Applicability depends on operator semantics, available-value information, intervening definitions, memory effects, and any required dominance relationships.

Expressions involving memory must not be treated as redundant without suitable memory-dependence reasoning.

### 13.6. Algebraic simplification

Objective: Apply a specific algebraic identity under explicit type and semantic constraints.

Example:

```text
x = y * 1
```

Potential result:

```text
x = y
```

The kernel must identify the arithmetic domain in which the identity is valid. For example, the transformation must not assume that every language-level multiplication operator behaves like ordinary mathematical integer multiplication.

### 13.7. Unreachable-block elimination

Objective: Remove a basic block that cannot be reached under the current control-flow graph and its declared semantics.

Example:

```text
entry:
    jump exit

unreachable:
    call unknown_function
    jump exit

exit:
    return
```

A control-flow simplification kernel may remove the unreachable block if reachability has been established and no external mechanism makes the block observable.

The kernel must maintain valid control-flow edges, block references, and any required IR invariants.

### 13.8. Loop-invariant code motion

Objective: Move an eligible loop-invariant computation to a location where it executes fewer times without changing observable behavior.

Example:

```text
for i in range(n):
    t = a * b
    use(t, i)
```

Potential conceptual result:

```text
t = a * b
for i in range(n):
    use(t, i)
```

The transformation requires proof that:

* The computation's operands remain invariant.
* The computation's dependencies remain valid.
* Moving the computation does not introduce new observable behavior.
* Memory dependencies are handled correctly.
* Traps, exceptions, and conditional execution remain semantically valid.
* The selected insertion point dominates the uses as required by the IR.

The transformation may also increase register pressure or change code size. These effects should be documented.

### 13.9. Loop interchange

Objective: Exchange the nesting order of compatible loops when dependence constraints permit the transformation.

Example:

```text
for i in range(N):
    for j in range(M):
        A[i, j] = f(i, j)
```

Conceptual interchange:

```text
for j in range(M):
    for i in range(N):
        A[i, j] = f(i, j)
```

This is only an illustration of loop structure. The transformation is not automatically valid for arbitrary loop bodies, memory accesses, iteration domains, or side effects.

A production kernel must require appropriate loop-nest information and dependence constraints, and it must preserve the original iteration semantics.

### 13.10. Strength reduction

Objective: Replace an expensive operation with a cheaper equivalent recurrence or operation sequence.

Example:

```text
for i in range(n):
    offset = i * 4
    use(offset)
```

A potential transformation maintains an incrementing offset instead of recomputing the multiplication.

The kernel must ensure that the recurrence preserves the original arithmetic semantics and that any additional state is correctly initialized and updated.

Do not assume that a replacement is faster on every target. Cost-model requirements should be explicit when performance depends on target characteristics.

### 13.11. Instruction combining

Objective: Replace a supported sequence of low-level operations with a target operation or sequence that has equivalent behavior.

Example:

```text
tmp = ADD a, b
result = COMPARE tmp, 0
```

A target-specific kernel may identify a more direct operation if the target provides an appropriate instruction and its flags, exceptions, and result semantics match.

The kernel must not assume that all architectures expose the same instruction set or condition-code behavior.

### 13.12. Polyhedral loop transformation

Objective: Transform a supported affine loop nest using mathematical representations of iteration domains and dependences.

A kernel might require:

```text
- Affine loop bounds.
- Supported affine memory-access functions.
- A valid dependence model.
- A legal transformed iteration schedule.
- Explicit assumptions about side effects.
```

The transformation may propose loop interchange, tiling, skewing, fusion, or another specific operation. Each should have its own kernel identity when it represents a distinct optimization objective.

A kernel that merely declares itself "polyhedral" must not be considered applicable until its concrete constraints are satisfied.

---

## 14. Kernel Dependencies, Conflicts, and Composition

Optimization kernels do not operate in isolation.

One kernel may establish a property needed by another. Another may invalidate analyses or structural invariants on which a subsequent kernel depends.

Blackstone must represent these relationships explicitly when the schema and execution infrastructure support them.

### 14.1. Dependency types

Distinguish between:

* **Stage dependency:** A kernel requires a particular IR stage.
* **Capability dependency:** A kernel requires a current IR property.
* **Analysis dependency:** A kernel requires analysis results.
* **Transformation dependency:** A kernel requires another transformation to have been performed.
* **Ordering dependency:** Two kernels must execute in a particular order.
* **Conflict:** Two kernels cannot be safely applied under the same conditions or in the same execution plan.
* **Invalidation:** A transformation invalidates an analysis or property.
* **Optional recommendation:** One kernel may make another more profitable, without being a correctness prerequisite.

Do not represent all of these relationships as generic dependencies.

### 14.2. Example: prerequisite

```lisp
(prerequisites
  (optimization-applied
    "blackstone.mir.ssa-construction"))
```

This declaration is appropriate when the kernel genuinely requires the result of SSA construction.

If it merely requires an SSA-form program, a property-based requirement is usually more flexible.

### 14.3. Example: analysis invalidation

```lisp
(analysis-contract
  (requires
    loop-information
    memory-dependence)

  (invalidates
    loop-information
    memory-dependence))
```

This illustrative example says that the kernel consumes the named analyses and that the current results must not be assumed valid after transformation.

Whether both analyses actually become invalid depends on the transformation. Declare only the invalidations that are justified by the implementation.

### 14.4. Cycles and dependency validation

A dependency graph may contain cycles that cannot be satisfied by ordinary one-way planning.

The validator should identify such cycles and report them clearly.

Do not silently ignore a cycle or repeatedly execute kernels in the hope that the dependency will eventually be satisfied.

Some optimization pipelines intentionally iterate to a fixed point. Such behavior must be specified separately, including termination or iteration-limit rules.

### 14.5. Fixed-point iteration

If a set of kernels is designed to iterate, define:

* The set of kernels in the iteration group.
* The order or scheduling policy.
* The convergence condition.
* The maximum iteration count, if applicable.
* How invalidated analyses are recomputed.
* How repeated transformations are detected.
* What happens if convergence is not reached.

A single kernel must still describe one optimization. Fixed-point scheduling belongs to the execution plan or orchestration layer.

---

## 15. Repository Organization

Follow the existing repository layout where it is established. If the project does not yet have a stable layout, use the following as a proposed organization.

```text
blackstone/
├── AGENTS.md
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── kernel-schema.md
│   ├── nodeir-stages.md
│   ├── optilisp.md
│   ├── monoids.md
│   ├── applicability.md
│   ├── correctness.md
│   └── contributing.md
├── schemas/
│   ├── kernel.schema.json
│   ├── capability.schema.json
│   ├── analysis.schema.json
│   └── monoid.schema.json
├── kernels/
│   ├── hir/
│   │   ├── constant-folding/
│   │   ├── algebraic-simplification/
│   │   └── control-flow/
│   ├── mir/
│   │   ├── constant-propagation/
│   │   ├── dead-code-elimination/
│   │   ├── memory/
│   │   └── loops/
│   └── lir/
│       ├── instruction-combining/
│       ├── register-pressure/
│       └── target-specific/
├── optilisp/
│   ├── grammar/
│   ├── stdlib/
│   │   └── monoids/
│   └── examples/
├── tools/
│   ├── validate-kernels
│   ├── inspect-kernel
│   └── query-kernels
└── tests/
    ├── parser/
    ├── schema/
    ├── semantics/
    ├── metadata/
    └── fixtures/
```

This tree is a design proposal, not a mandatory set of files to create regardless of repository state.

### 15.1. Organization rules

* Organize kernels by stable stage and optimization family where practical.
* Keep schemas separate from individual kernel definitions.
* Keep reusable Optilisp facilities separate from kernel records.
* Keep documentation discoverable from the repository root.
* Keep test fixtures separate from production kernels.
* Avoid duplicating the same kernel definition in multiple locations.
* Do not introduce generated files into source directories without an established convention.
* Ensure the canonical location of a kernel is unambiguous.

If the repository already uses a different structure, preserve it unless there is a documented reason to migrate.

---

## 16. Kernel Identity and Naming

Every kernel must have a stable identifier that is distinct from its display name and filename.

A proposed identifier convention is:

```text
blackstone.<stage>.<family>.<operation>
```

Examples:

```text
blackstone.hir.constant-folding.integer-add
blackstone.hir.algebraic-simplification.multiply-one
blackstone.mir.constant-propagation.ssa
blackstone.mir.dead-code-elimination.unused-pure-operation
blackstone.mir.loops.loop-invariant-code-motion
blackstone.mir.polyhedral.loop-interchange
blackstone.lir.instruction-combining.add-compare
```

Use the repository's established identifier convention if one exists.

### 16.1. Identity rules

* IDs must be unique within the Blackstone database.
* IDs must remain stable when documentation is edited.
* Display names may change without changing the ID, unless a migration is explicitly required.
* Renaming a file must not silently change a kernel's identity.
* References to other kernels must use canonical identifiers.
* Deprecated identifiers must have an explicit migration policy.

Do not generate identifiers from file paths at runtime unless the repository explicitly defines that behavior.

### 16.2. Versioning

If kernels evolve incompatibly, version the schema or the relevant definition according to the project's versioning policy.

An incompatible change may include:

* Changing the meaning of a transformation.
* Relaxing a correctness precondition.
* Changing the interpretation of an existing metadata field.
* Changing a kernel's semantic contract.
* Removing an identifier referenced by other definitions.

Documentation corrections and compatible metadata additions do not necessarily require a new kernel identity.

---

## 17. Validation Requirements

A kernel database is only useful if its entries can be checked consistently.

Implement validation in distinct layers rather than relying on one large, opaque validation function.

### 17.1. Layer 1: Syntax validation

Check that:

* Parentheses are balanced.
* S-expressions are well formed.
* Strings and escapes are valid.
* The source conforms to the actual Optilisp grammar.
* No unexpected trailing forms exist where the grammar forbids them.
* Source locations are retained for diagnostics.

Example diagnostic:

```text
kernels/hir/constant-folding/integer-add.olisp:18:7:
error: expected a valid kernel transformation form
```

### 17.2. Layer 2: Structural validation

Check that:

* Required fields are present.
* Field types are correct.
* The declared stage is recognized.
* Metadata collections have the expected structure.
* Each kernel has one primary optimization objective.
* Required documentation fields are present.
* Duplicate singleton fields are rejected or handled according to the schema.
* Unknown fields are rejected unless explicitly permitted by an extension mechanism.

### 17.3. Layer 3: Reference validation

Check that:

* Kernel IDs are unique.
* Referenced kernels exist.
* Required capabilities are defined.
* Required analyses are defined.
* Referenced monoids exist.
* Referenced target capabilities are known or explicitly declared external.
* Dependency relationships are valid.
* Deprecated references follow the migration policy.

Example diagnostic:

```text
error: unresolved analysis identifier "memory-dependence"
       referenced by kernel "blackstone.mir.dead-store-elimination"
```

### 17.4. Layer 4: Semantic contract validation

Check what can be checked statically, including:

* Stage compatibility.
* Contradictory preconditions.
* Invalid or contradictory capability declarations.
* Impossible dependency requirements.
* Incorrect monoid-law declarations where verification is available.
* Missing correctness contracts.
* Incompatible source and result forms.
* Unresolved transformation references.
* Invalidated analyses incorrectly declared as preserved.
* Unsupported assumptions about arithmetic or memory semantics.

Not every semantic property can be decided automatically. When a property cannot be verified, the tool should report the limitation rather than claim it has proved correctness.

### 17.5. Layer 5: Transformation validation

Where a transformation executor or semantic model exists, verify that:

* Input patterns are valid for the selected NodeIR stage.
* The transformation result satisfies required IR invariants.
* Declared postconditions hold.
* Required analyses are available.
* The transformation does not violate its explicit semantic assumptions.
* Unsupported inputs fail safely and produce useful diagnostics.

### 17.6. Layer 6: Database validation

At the database level, verify:

* The database can be loaded deterministically.
* All required indexes can be built.
* IDs remain unique across directories and imported packages.
* Cross-kernel references resolve.
* All production entries pass the configured validation policy.
* Invalid entries are not silently treated as valid kernels.

The database must be able to distinguish between an absent optional capability and a malformed declaration.

---

## 18. Testing Requirements

Tests must verify more than whether an Optilisp file parses.

Every implementation change should be accompanied by tests appropriate to the changed behavior.

### 18.1. Parser tests

Include:

* Valid kernel definitions.
* Invalid S-expression structure.
* Missing required fields.
* Duplicate IDs or fields.
* Unknown forms.
* Invalid strings.
* Invalid stage names.
* Invalid metadata types.
* Unexpected end of input.
* Large definitions and nested expressions.
* Multiple kernel definitions in a supported compilation unit.

### 18.2. Metadata tests

Include:

* Successful resolution of valid references.
* Detection of duplicate identifiers.
* Detection of unknown capabilities.
* Detection of unknown analyses.
* Detection of invalid dependency relationships.
* Correct handling of optional fields.
* Correct handling of deprecated identifiers.
* Rejection of malformed stage declarations.

### 18.3. Transformation tests

Where execution support exists, test each kernel using:

1. A positive case where the transformation applies.
2. A negative case where a precondition fails.
3. An edge case involving relevant semantics.
4. A case where the transformation must not occur.
5. A case that verifies the resulting IR remains valid.
6. A case that verifies the semantic contract.

For constant folding, for example:

```text
Positive:
    6 * 7  ->  42

Negative:
    multiply(unknown_value, 7)
    must not be folded as a constant expression

Boundary:
    integer operation near the representable limit

Semantic restriction:
    overflow behavior must match the IR's integer semantics
```

These are conceptual test descriptions. The actual test syntax must follow the available NodeIR and execution infrastructure.

### 18.4. Differential testing

If the project has an interpreter, reference evaluator, or another trusted execution path, compare original and transformed programs on supported inputs.

A useful workflow is:

1. Construct a valid input program.
2. Evaluate or execute the original program.
3. Apply the kernel.
4. Verify the transformed IR.
5. Evaluate or execute the transformed program.
6. Compare observable behavior under the declared semantic model.

Differential testing is valuable but is not a complete proof of correctness.

### 18.5. Property-based testing

Where practical, generate many valid IR fragments and test general properties.

Examples:

* Constant folding agrees with the specified integer evaluator.
* A rewrite does not alter observable behavior for generated valid inputs.
* A transformation preserves required CFG invariants.
* A monoid operation satisfies its laws for values in the supported domain.
* Repeated application of an idempotent rewrite reaches the expected normal form.

Do not assume that every rewrite is idempotent or terminating. Those properties must be established for the individual transformation.

### 18.6. Regression tests

Every discovered correctness bug should produce a regression test when feasible.

Keep the test focused on the failure rather than encoding unrelated implementation details.

---

## 19. Database Querying and Discovery

The database should support useful discovery queries independently of transformation execution.

Potential queries include:

* Find all kernels for NodeMIR.
* Find kernels requiring SSA form.
* Find kernels that require alias analysis.
* Find kernels that invalidate loop information.
* Find kernels belonging to a particular optimization family.
* Find kernels applicable to a particular target capability.
* Find kernels with unresolved dependencies.
* Find kernels with incomplete documentation.
* Find kernels recommending a particular external tool.
* Find kernels that require profile information.
* Find kernels that may increase code size.
* Find kernels that have not yet been validated by the available test suite.

### 19.1. Example query intent

A query for memory-related MIR optimizations might conceptually specify:

```text
stage = mir
category in [memory-optimization, dead-store-elimination]
requires analysis alias-information
requires capability memory-operations
validation_status = valid
```

This is a query example, not a required query language.

### 19.2. Searchable metadata

Fields intended for machine filtering must remain structured.

Do not encode all metadata in one documentation string and then require consumers to scrape natural language to determine applicability.

Free-form documentation remains important, but it complements rather than replaces structured metadata.

---

## 20. Performance and Cost Models

An optimization is not necessarily profitable simply because it preserves semantics or reduces the number of IR nodes.

Blackstone must distinguish between:

* **Correctness:** Whether the transformation is valid.
* **Applicability:** Whether its preconditions hold.
* **Profitability:** Whether applying it is expected to provide a desired benefit.
* **Cost:** The computational or resource expense of applying the transformation.

### 20.1. Profitability considerations

Depending on the kernel, relevant costs may include:

* Execution latency.
* Throughput.
* Code size.
* Compile time.
* Register pressure.
* Memory traffic.
* Instruction count.
* Branch behavior.
* Cache behavior.
* Vectorization opportunities.
* Compilation memory consumption.

A kernel may declare an optional cost-model requirement if profitability cannot be determined without target or profile information.

### 20.2. Example: strength reduction

Replacing a multiplication with repeated additions might reduce the apparent cost of an individual operation but increase dependencies, instruction count, or register pressure.

The kernel must not claim universal profitability without evidence.

A suitable description is:

> This kernel identifies a semantically valid strength-reduction opportunity. Profitability depends on the target cost model, recurrence structure, and surrounding use pattern.

Correctness and profitability must be represented separately.

### 20.3. Cost-model metadata

A future-compatible schema may distinguish:

```text
correctness requirements:
    arithmetic semantics
    dependency constraints

profitability requirements:
    target cost model
    operation latency
    throughput estimates
    code-size constraints

expected effects:
    fewer expensive operations
    possible additional state
    possible register-pressure increase
```

These are distinct classes of information and should not be conflated.

---

## 21. Third-Party Libraries and External Tools

Blackstone's declarative nature allows it to document external tools that may help implement, analyze, validate, or execute an optimization.

The database must not assume that every recommendation is an installed dependency.

### 21.1. Dependency categories

Use explicit categories such as:

* Required runtime dependency.
* Required build dependency.
* Optional integration.
* Recommended analysis tool.
* Reference implementation.
* Research reference.
* Alternative implementation.

The precise schema should follow repository conventions.

### 21.2. Recommendation quality

A recommendation should identify:

* The library or tool.
* Its intended role.
* Whether it is optional.
* The functionality it contributes.
* Relevant compatibility or licensing considerations when known.
* The reason it is useful for the kernel.

Do not claim that an external library supports a feature without checking its documentation or implementation.

Do not introduce a new mandatory dependency merely because it would simplify one optimization.

### 21.3. Dependency isolation

Where practical, external tool integrations should remain separate from the canonical kernel metadata.

The core database must remain understandable and loadable even when optional integrations are unavailable.

If a kernel cannot be executed without a required component, the execution infrastructure must report that limitation explicitly.

---

## 22. Implementation Workflow for Coding Agents

All agents working on Blackstone MUST follow a repository-first implementation workflow.

### Step 1: Inspect the repository

Before editing files, inspect:

* Existing `AGENTS.md` instructions.
* Directory structure.
* Current Optilisp grammar.
* Parser and any evaluator or compiler.
* Existing kernel definitions.
* NodeIR interfaces and stage definitions.
* Existing schemas and metadata conventions.
* Test infrastructure.
* Build system and validation commands.
* Existing `libstone` integration boundaries.

Do not assume that this specification describes code that has already been implemented.

### Step 2: Identify the actual task

Determine whether the task concerns:

* A new kernel.
* An existing kernel.
* Optilisp syntax.
* Metadata or schema design.
* Monoid library functionality.
* NodeIR integration.
* Validation.
* Testing.
* Querying and indexing.
* Documentation.
* Build or packaging integration.

Avoid changing unrelated subsystems.

### Step 3: Define the kernel contract first

For a new kernel, determine:

1. Its single optimization objective.
2. Its target IR stage.
3. Its required input patterns.
4. Its semantic assumptions.
5. Its required capabilities.
6. Its required analyses.
7. Its prior-transformation requirements.
8. Its forbidden conditions.
9. Its intended transformation.
10. Its correctness obligations.
11. Its expected effects and limitations.
12. Its tests.

Do not begin by writing a rewrite rule and adding the contract later.

### Step 4: Inspect existing patterns

Find the closest existing kernel and reuse its conventions where appropriate.

Avoid copying irrelevant metadata from a neighboring kernel merely because the files look similar.

For example, a constant-folding kernel should not inherit alias-analysis requirements from a dead-store-elimination kernel unless the implementation genuinely needs them.

### Step 5: Implement the smallest coherent change

Prefer a focused patch that establishes the required functionality without unrelated refactoring.

If a task requires a new language form, update all relevant parts:

* Grammar.
* Parser.
* Semantic representation.
* Validation.
* Documentation.
* Tests.
* Existing examples affected by the change.

### Step 6: Validate

Run the available:

* Parser tests.
* Schema checks.
* Reference checks.
* Kernel validation.
* Relevant transformation tests.
* Regression tests.
* Build and formatting checks.

Do not claim that a test passed if it was not executed.

If a tool is unavailable, report the limitation precisely.

### Step 7: Review semantic boundaries

Before finishing, ask:

* Does the transformation preserve the declared program semantics?
* Are all assumptions explicit?
* Can unknown analysis results cause an unsafe rewrite?
* Are side effects and exceptions handled correctly?
* Are stage invariants preserved?
* Are invalidated analyses reported?
* Does the kernel still describe exactly one optimization?
* Are documentation and tests consistent with the implementation?

### Step 8: Report the result

Summarize:

* What changed.
* Which files changed.
* Which new kernel or language features were added.
* Which validation and test commands ran.
* Which tests passed or failed.
* Any unresolved assumptions or limitations.

Keep the report factual and distinguish implemented behavior from proposed future work.

---

## 23. Coding Rules and Engineering Quality

### 23.1. General rules

* Follow the repository's established language and style conventions.
* Prefer explicit data structures over unstructured metadata blobs.
* Use stable identifiers and centralized definitions for shared concepts.
* Keep parser, schema, semantic validation, and execution responsibilities separate.
* Provide clear diagnostics with source locations when available.
* Reject malformed definitions deterministically.
* Avoid silent fallbacks that change a kernel's meaning.
* Preserve backward compatibility where required.
* Add tests for new behavior and bug fixes.
* Document non-obvious semantic decisions.

### 23.2. Error handling

Errors should identify the relevant kernel and the reason the definition is invalid.

Prefer:

```text
error: kernel "blackstone.mir.loop-interchange"
       requires loop-dependence information,
       but no valid result is available
```

over:

```text
optimization failed
```

Where appropriate, distinguish:

* Parse errors.
* Schema errors.
* Unresolved references.
* Unsatisfied prerequisites.
* Unsupported IR patterns.
* Failed correctness checks.
* Missing optional tools.
* Internal implementation failures.

A kernel that is inapplicable should not automatically be treated as malformed. Inapplicability is often a normal outcome of optimization planning.

### 23.3. Determinism

Where practical:

* Use deterministic kernel discovery and indexing.
* Keep diagnostics stable enough for regression testing.
* Avoid iteration order that depends on unspecified hash-table behavior when order affects output.
* Ensure that generated metadata does not vary unnecessarily across runs.
* Define ordering semantics where order matters.

### 23.4. Extensibility

New optimization categories, capabilities, analyses, and monoids should have a documented extension mechanism.

Do not require unrelated kernels to change when a new category is added unless the schema explicitly makes the change necessary.

Unknown identifiers should be handled according to a deliberate compatibility policy. Do not silently reinterpret an unknown capability as a known one.

---

## 24. Common Implementation Mistakes

The following mistakes are prohibited unless a documented design decision explicitly establishes otherwise.

### Mistake 1: Treating Blackstone as the optimizer executor

Blackstone's primary purpose is the kernel database. An executor may exist as a supporting component, but it must not be confused with the database's defining responsibility.

### Mistake 2: Writing one large optimization kernel

A kernel that performs constant propagation, dead-code elimination, loop unrolling, and vectorization violates the one-kernel-one-optimization principle.

Split it into separate kernel definitions.

### Mistake 3: Omitting the IR stage

A kernel without a known stage has an incomplete contract.

Do not infer the stage from its filename alone.

### Mistake 4: Treating metadata as documentation

A prose statement that a kernel requires SSA form does not replace a machine-readable prerequisite when the schema supports one.

### Mistake 5: Assuming types are always available

NodeHIR does not necessarily guarantee complete type information. Require it explicitly when necessary.

### Mistake 6: Assuming analysis results remain valid

Transformations can invalidate analysis information. Declare invalidations and recompute results when needed.

### Mistake 7: Assuming algebraic identities are universal

Integer arithmetic, floating-point arithmetic, pointer arithmetic, and language-level overloaded operators have different semantics.

Declare the applicable domain.

### Mistake 8: Assuming a valid rewrite is profitable

Correctness does not establish profitability. Use an appropriate cost model when required.

### Mistake 9: Treating monoids as arbitrary containers

A structure is not a monoid merely because it has a merge operation. The required algebraic laws must hold under the specified equivalence relation.

### Mistake 10: Requiring every optional library

A documented recommendation is not automatically a build or runtime dependency.

### Mistake 11: Treating examples as the grammar

Illustrative Optilisp examples in this document are not authoritative syntax definitions. The actual grammar and implementation determine valid source syntax.

### Mistake 12: Claiming proof from tests alone

Passing a finite test suite does not establish general semantic correctness. Describe tests as evidence, and claim formal proof only when an actual proof or verification procedure supports the claim.

### Mistake 13: Hiding unsupported assumptions

If a kernel only works for a restricted subset of NodeIR, declare the restriction and test it.

### Mistake 14: Creating undocumented identifiers

New capability names, analysis names, categories, monoids, and kernel IDs must follow the repository's naming and registration rules.

---

## 25. Completion Criteria

A kernel or infrastructure change is complete only when all applicable criteria are satisfied.

### Kernel-definition checklist

* [ ] The kernel has a stable, unique ID.
* [ ] The kernel has one primary optimization objective.
* [ ] The target NodeIR stage is explicit.
* [ ] Required capabilities are declared.
* [ ] Required analyses are declared.
* [ ] Relevant prior-transformation requirements are declared.
* [ ] Forbidden states or assumptions are declared where relevant.
* [ ] Semantic preconditions are explicit.
* [ ] The intended transformation is described.
* [ ] Correctness obligations are documented.
* [ ] Expected effects and limitations are documented.
* [ ] First-class documentation is present.
* [ ] External recommendations are accurate and properly classified.
* [ ] The kernel passes applicable validation.
* [ ] Tests cover positive and negative cases.
* [ ] Semantic edge cases are covered where relevant.

### Infrastructure checklist

* [ ] Syntax is defined by the actual Optilisp implementation.
* [ ] Parsing and semantic validation are separate.
* [ ] Kernel identifiers are checked for uniqueness.
* [ ] Cross-references are validated.
* [ ] Unknown or missing information is handled explicitly.
* [ ] Stage and analysis contracts are enforced where possible.
* [ ] Invalidated analyses are tracked.
* [ ] Diagnostics identify the relevant failure.
* [ ] Regression tests cover discovered bugs.
* [ ] Documentation matches implemented behavior.
* [ ] Existing repository conventions are preserved.
* [ ] No unrelated subsystem has been changed unnecessarily.

A checklist item may be marked not applicable only when its irrelevance is justified by the specific task.

---

## 26. Guiding Principle

Blackstone should make optimization algorithms **describable, discoverable, inspectable, composable, and verifiable**.

A good kernel is not merely a rewrite rule that appears to work. It is a self-contained declarative specification of one optimization, with enough structured information for tooling to determine where it belongs, what it requires, when it is safe to apply, what it intends to change, and what evidence supports its correctness.

When implementing Blackstone, prioritize explicit contracts over hidden assumptions, reusable declarative abstractions over duplicated definitions, precise semantics over vague claims of optimization, and independently testable kernels over monolithic transformation logic.

The database should ultimately make it possible to answer, programmatically and reliably:

* What optimization does this kernel perform?
* Which NodeIR stage does it target?
* What facts must be known before it can run?
* What must already be true about the program?
* What conditions make it inapplicable?
* Which analyses does it consume or invalidate?
* What transformation does it describe?
* Under what semantics is the transformation correct?
* What performance or code-size effects might it have?
* Which other kernels does it depend on or conflict with?
* How has its implementation been validated?

If those questions cannot be answered from a kernel's structured metadata, its transformation description, and its first-class documentation, the kernel specification is not yet sufficiently complete.

