# AGENTS.md — TraceML

## 1. Project Overview

TraceML is a metatracing-oriented programming language in the ML language family.

The language is designed around the **MetaKrivine machine**, an extension of the classical **Krivine abstract machine** with explicit support for metatracing. TraceML programs are compiled through a sequence of intermediate representations before ultimately being lowered to executable machine code.

The compilation pipeline is:

```text
TraceML
   │
   ▼
TraceLambda IR
   │
   ▼
Limestone MachineIR
   │
   ▼
Machine Code
```

Each stage has a distinct purpose:

1. **TraceML** — the source programming language.
2. **TraceLambda IR** — a lambda-calculus-based intermediate representation with metatracing semantics.
3. **Limestone MachineIR** — a lower-level machine-oriented intermediate representation.
4. **Machine code** — executable code for the target architecture.

When modifying the compiler, preserve the semantic boundaries between these stages. A transformation should be performed at the lowest stage that has sufficient information to implement it correctly, while avoiding unnecessary leakage of lower-level implementation details into higher-level representations.

---

## 2. Core Concepts

### 2.1 TraceML

TraceML is an ML-family language.

Unless explicitly documented otherwise, language features should be understood in terms of the language's functional and expression-oriented semantics rather than as imperative machine operations.

Compiler implementations should preserve the distinction between:

* source-level syntax,
* source-level semantics,
* intermediate representation semantics,
* machine-level implementation details.

Do not introduce machine-specific behavior into the TraceML frontend unless that behavior is an intentional part of the TraceML language specification.

---

### 2.2 Metatracing

Metatracing is a fundamental part of TraceML rather than an incidental optimization pass.

The compiler and runtime architecture should therefore distinguish between:

* ordinary program evaluation,
* tracing of program execution,
* representation of traced computations,
* transformations or compilation of traced computations.

Metatracing semantics must remain explicit when crossing compiler boundaries.

In particular, lowering a construct from TraceML to TraceLambda must not accidentally erase information required by the MetaKrivine execution model or by subsequent tracing transformations.

---

### 2.3 Krivine Machine

The **Krivine machine** is the conceptual foundation for the evaluation model.

The classical Krivine machine evaluates lambda calculus expressions using an environment and a stack of arguments/closures.

TraceML extends this model with metatracing facilities. The resulting machine is referred to as the **MetaKrivine machine**.

When implementing evaluation or lowering logic, use the abstract-machine semantics as the authoritative source for execution behavior rather than deriving semantics from incidental details of a particular backend.

---

### 2.4 MetaKrivine Machine

The MetaKrivine machine is an extension of the Krivine machine that incorporates metatracing semantics.

Conceptually:

```text
Krivine Machine
      +
Metatracing Facilities
      │
      ▼
MetaKrivine Machine
```

The MetaKrivine machine provides the semantic foundation for TraceML execution and for the TraceLambda representation.

Any implementation of MetaKrivine behavior should make the following distinctions explicit where applicable:

* the current computation,
* the evaluation environment,
* pending arguments or continuations,
* tracing state,
* metatracing operations,
* transitions between ordinary and traced computation.

Do not conflate the abstract semantics of the machine with its eventual machine-code implementation.

---

## 3. Compiler Architecture

TraceML is compiled through several representations.

### 3.1 Frontend

The frontend is responsible for translating source text into an appropriate representation of TraceML programs.

Conceptually:

```text
Source Text
    │
    ▼
Lexer / Parser
    │
    ▼
TraceML AST
```

The frontend should be responsible for source-language concerns such as:

* lexical analysis,
* parsing,
* syntax validation,
* source locations,
* syntactic desugaring where appropriate,
* construction of source-level AST nodes.

The frontend should not depend on target-machine details.

---

### 3.2 TraceML Semantic Processing

After parsing, source programs may require semantic processing before being lowered to TraceLambda.

This stage may include, where applicable:

* name resolution,
* scope analysis,
* binding resolution,
* validation of language invariants,
* type checking or inference,
* closure analysis,
* source-level desugaring.

Semantic transformations should preserve source-language meaning.

If a transformation changes observable semantics, it belongs to the language semantics rather than being treated as a purely mechanical compiler optimization.

---

## 4. TraceLambda IR

**TraceLambda** is the primary intermediate representation corresponding to the lambda-calculus semantics of TraceML.

It extends lambda calculus with explicit metatracing semantics.

Conceptually:

```text
TraceML
   │
   │ semantic lowering
   ▼
TraceLambda
```

TraceLambda should represent computations at a level where the semantics of functions, application, environments, and metatracing remain explicit.

The IR should not prematurely encode target-specific machine operations.

### 4.1 Responsibilities

TraceLambda is responsible for representing:

* lambda abstractions,
* function application,
* variables and bindings,
* closures or closure-forming constructs where required,
* evaluation structure,
* metatracing operations,
* other semantics required by the MetaKrivine model.

The precise set of constructs is defined by the TraceLambda IR specification and implementation.

### 4.2 Lowering to TraceLambda

When lowering TraceML to TraceLambda:

1. Preserve lexical binding semantics.
2. Preserve evaluation order.
3. Preserve closure behavior.
4. Preserve metatracing semantics.
5. Do not introduce target-specific assumptions.
6. Make implicit semantics explicit where required by the IR.
7. Preserve source locations when doing so is useful for diagnostics and debugging.

A TraceML construct should only be lowered into a TraceLambda construct when the resulting representation has equivalent semantics.

---

## 5. Limestone MachineIR

**Limestone MachineIR** is the lower-level intermediate representation used to bridge TraceLambda semantics and actual machine code.

Conceptually:

```text
TraceLambda
     │
     │ machine-oriented lowering
     ▼
Limestone MachineIR
```

MachineIR is expected to expose implementation details that are intentionally absent from TraceLambda.

These may include, depending on the implementation:

* explicit control flow,
* machine-level values,
* registers or virtual registers,
* stack operations,
* calling conventions,
* explicit memory operations,
* closure layout,
* runtime calls,
* low-level control transfers,
* target-specific or target-constrained operations.

The exact semantics of MachineIR must be defined by its own representation and verifier.

### 5.1 Lowering Rules

The TraceLambda → MachineIR lowering pass must preserve the semantics of TraceLambda.

In particular, it must not silently change:

* evaluation order,
* function application behavior,
* environment/closure semantics,
* lifetime requirements,
* tracing behavior,
* observable side effects.

MachineIR should contain enough information for subsequent code generation without requiring the backend to reconstruct high-level TraceLambda semantics.

---

## 6. Machine Code Generation

The final compiler stage translates Limestone MachineIR into executable machine code.

```text
Limestone MachineIR
        │
        ▼
   Code Generation
        │
        ▼
   Machine Code
```

Code generation is responsible for mapping the machine-oriented representation onto the target architecture and ABI.

Target-specific concerns should remain confined to the backend whenever possible.

Examples include:

* instruction selection,
* register allocation,
* stack-frame construction,
* calling conventions,
* instruction encoding,
* object-file generation,
* relocation handling,
* target-specific runtime conventions.

Higher-level compiler stages should not directly construct target-specific instructions unless the architecture explicitly requires such behavior.

---

## 7. Compiler Invariants

Compiler passes should maintain explicit invariants.

At every IR boundary, ask:

1. **What semantics does this IR represent?**
2. **What information is guaranteed to be preserved?**
3. **What information is intentionally discarded?**
4. **What invariants must hold for the next pass?**
5. **How is the representation verified?**

A transformation must not rely on undocumented properties of an earlier pass.

If a pass requires an invariant that is not obvious from the IR itself, document that invariant and, where practical, enforce it with a verifier or assertion.

---

## 8. Pass Design

Compiler passes should generally have one clear responsibility.

Prefer:

```text
AST
 │
 ├── name resolution
 │
 ├── semantic validation
 │
 └── lowering
        │
        ▼
   TraceLambda
        │
        ├── normalization
        ├── tracing transformation
        └── machine lowering
                │
                ▼
          MachineIR
```

over large passes that combine unrelated concerns.

A pass should document:

* its input representation,
* its output representation,
* required invariants,
* invariants it establishes,
* whether it preserves source locations,
* whether it can fail,
* whether it performs semantic transformations or only representation changes.

---

## 9. Correctness Over Convenience

Compiler correctness takes precedence over implementation convenience.

Do not:

* silently drop AST or IR nodes,
* ignore verifier failures,
* weaken type or semantic checks merely to make compilation succeed,
* replace a semantic transformation with a textual transformation,
* introduce backend-specific behavior into frontend code,
* assume evaluation order without documenting it,
* assume closure or environment behavior without verifying the machine semantics,
* remove tracing information unless the transformation explicitly permits it.

If an invariant cannot be maintained, fail explicitly rather than producing an apparently valid but semantically incorrect representation.

---

## 10. Error Handling

Errors should be reported at the highest level where the compiler has sufficient information to explain them accurately.

For example:

* lexical errors belong to lexing,
* syntax errors belong to parsing,
* binding errors belong to semantic analysis,
* invalid IR belongs to IR construction or verification,
* unsupported target operations belong to lowering/code generation.

Diagnostics should include source locations whenever the relevant information is available.

Avoid emitting low-level implementation errors for source-level problems when a more useful source-level diagnostic can be produced.

---

## 11. IR Verification

Every major IR should have explicit validity invariants.

Where practical, provide a verifier for:

* TraceML AST invariants,
* TraceLambda invariants,
* Limestone MachineIR invariants.

Verification should be performed:

* after constructing an IR,
* after major transformations,
* before entering a backend stage,
* in tests covering compiler transformations.

Compiler passes should not assume that malformed IR is impossible merely because earlier code is expected to be correct.

---

## 12. Testing

Tests should be organized around compiler semantics rather than implementation details alone.

At minimum, test:

### Parsing

* valid syntax,
* invalid syntax,
* nested expressions,
* binding constructs,
* source locations.

### Semantic Analysis

* valid bindings,
* invalid bindings,
* scope behavior,
* closure behavior,
* language invariants.

### TraceLambda

* correct lowering from TraceML,
* preservation of application semantics,
* preservation of binding semantics,
* preservation of metatracing semantics.

### MachineIR

* correct lowering from TraceLambda,
* valid machine-level invariants,
* correct control flow,
* correct closure/environment representation.

### Code Generation

* valid instruction generation,
* calling convention correctness,
* executable output,
* runtime behavior.

### End-to-End

Whenever possible, include tests of the form:

```text
TraceML source
      │
      ▼
  compiler
      │
      ▼
machine code
      │
      ▼
 expected behavior
```

End-to-end tests are particularly important for metatracing because a representation can appear locally correct while still violating the semantics of the complete evaluation pipeline.

---

## 13. Semantic Regression Tests

When fixing a compiler bug, add a regression test that demonstrates the original failure.

Prefer a test that captures observable semantics:

```text
source program
    →
compiled program
    →
observable result
```

rather than a test that only checks an incidental internal representation, unless the bug specifically concerns an IR invariant.

If the bug concerns a compiler invariant, test both:

1. the invariant itself, and
2. the resulting program behavior where applicable.

---

## 14. Debugging the Compiler

When debugging a compiler failure, determine the first stage at which the program becomes incorrect.

Use the pipeline:

```text
TraceML
   ↓
TraceLambda
   ↓
MachineIR
   ↓
Machine Code
```

A useful debugging strategy is:

1. Verify that the source parses as intended.
2. Verify semantic analysis.
3. Inspect the generated TraceLambda.
4. Verify TraceLambda invariants.
5. Inspect the generated MachineIR.
6. Verify MachineIR invariants.
7. Inspect generated machine code when necessary.
8. Compare runtime behavior with the expected MetaKrivine semantics.

Do not immediately debug the final machine code if the incorrect behavior can already be observed in an earlier IR.

---

## 15. Source Locations and Debug Information

Source-location information should be preserved across transformations whenever practical.

At minimum, compiler diagnostics should be able to associate failures with the original TraceML source.

When a transformation combines or eliminates nodes, choose a deterministic and meaningful source location rather than discarding location information without reason.

Debug information must not alter program semantics.

---

## 16. Performance

Performance optimizations must preserve the semantics of TraceML and the MetaKrivine/TraceLambda execution model.

Before optimizing a pass:

1. Establish a correctness test.
2. Measure the relevant behavior.
3. Identify the actual bottleneck.
4. Implement the smallest transformation that addresses it.
5. Verify semantic equivalence.
6. Benchmark before and after.

Do not add speculative optimizations merely because they appear locally beneficial.

For metatracing-related transformations, additionally verify that an optimization does not unintentionally alter the information available to tracing mechanisms.

---

## 17. Changes to the Compiler

When implementing a feature or fixing a bug, follow this general process:

```text
Understand semantics
        │
        ▼
Identify affected IR boundary
        │
        ▼
Update representation/specification if required
        │
        ▼
Implement transformation
        │
        ▼
Add verifier/tests
        │
        ▼
Run existing regression suite
        │
        ▼
Inspect generated IR when appropriate
```

Avoid implementing a feature solely at the lowest level if the feature has source-language semantics.

Likewise, avoid introducing source-level concepts into MachineIR when they can be completely represented by lower-level constructs.

---

## 18. Adding a New Language Feature

A new TraceML feature should be considered across the complete compiler pipeline.

For each feature, determine:

1. **Syntax**

   * How is it written?
   * What grammar changes are required?

2. **Semantics**

   * What does the construct mean?
   * What are its evaluation rules?
   * How does it interact with metatracing?

3. **TraceML Representation**

   * Is a new AST node required?
   * Can the construct be desugared?

4. **TraceLambda**

   * Does the feature require a new IR construct?
   * Can existing TraceLambda constructs represent it without losing semantics?

5. **MachineIR**

   * How is the feature represented at the machine level?
   * Does it require runtime support?

6. **Code Generation**

   * Does it require new target instructions or calling conventions?

7. **Runtime**

   * Does execution require runtime support?

8. **Testing**

   * What unit, IR, and end-to-end tests are required?

Do not add a new IR instruction merely because adding one is convenient. Prefer existing representations when they can express the feature faithfully.

---

## 19. Changing an Existing IR

Changes to an IR are potentially cross-cutting.

Before changing an IR node:

1. Find all constructors.
2. Find all pattern matches.
3. Find all visitors.
4. Find all printers/serializers.
5. Find all verifiers.
6. Find all optimization passes.
7. Find all lowering passes.
8. Find all code-generation paths.
9. Find all tests that construct or inspect the node.

After changing the IR, ensure all consumers agree on the new invariant.

An IR change should not leave obsolete assumptions in downstream passes.

---

## 20. Runtime and Compiler Boundaries

Compiler-generated runtime operations should have a clearly defined interface.

Do not duplicate runtime semantics independently in multiple compiler stages.

Where a TraceML or TraceLambda operation requires runtime support, document:

* the runtime operation,
* its arguments,
* its return value,
* ownership/lifetime requirements,
* calling convention,
* tracing behavior,
* error behavior.

The compiler and runtime must agree on these conventions.

---

## 21. Repository Conventions

When working in the repository:

* Prefer existing abstractions over introducing parallel implementations.
* Follow the conventions already established by neighboring code.
* Keep changes narrowly scoped.
* Do not reformat unrelated files.
* Do not modify generated files manually when they are produced by a build step.
* Do not commit build artifacts unless the repository explicitly requires them.
* Update tests when behavior changes.
* Update documentation when semantics or public interfaces change.

Before introducing a new abstraction, search the repository for an existing abstraction that already serves the same purpose.

---

## 22. Dependencies

Avoid adding dependencies for functionality that can reasonably be implemented using existing project infrastructure.

When a dependency is necessary:

1. Determine whether the project already has an equivalent facility.
2. Check compatibility with the project's build system and supported targets.
3. Keep the dependency narrowly scoped.
4. Document non-obvious reasons for the dependency.
5. Add tests covering its integration.

Do not introduce a dependency solely to avoid a small amount of straightforward implementation work.

---

## 23. Generated Code

Generated code should be treated as an output of the compiler/build system rather than as the primary source of truth.

When modifying generated output:

1. Identify the generator.
2. Modify the generator or source specification.
3. Regenerate the output.
4. Verify that the generated result is correct.

If generated artifacts are intentionally checked into the repository, follow the repository's existing regeneration workflow.

---

## 24. Documentation

Documentation should distinguish clearly between:

* language semantics,
* compiler implementation,
* IR definitions,
* machine semantics,
* implementation details,
* optimization behavior.

When documenting semantics, describe what the program means.

When documenting implementation, describe how the compiler realizes those semantics.

Do not describe an implementation detail as a language guarantee unless that behavior is intentionally part of the language specification.

---

## 25. Terminology

Use the following terminology consistently:

| Term                    | Meaning                                                                              |
| ----------------------- | ------------------------------------------------------------------------------------ |
| **TraceML**             | The source programming language.                                                     |
| **TraceML AST**         | The source-level abstract syntax representation.                                     |
| **TraceLambda**         | The lambda-calculus-based IR with metatracing semantics.                             |
| **TraceLambda IR**      | The formal intermediate representation represented by TraceLambda.                   |
| **Krivine machine**     | The classical abstract machine underlying the evaluation model.                      |
| **MetaKrivine machine** | The Krivine machine extended with metatracing facilities.                            |
| **Limestone MachineIR** | The machine-oriented intermediate representation used before code generation.        |
| **Machine code**        | The target architecture's executable instruction representation.                     |
| **Frontend**            | Source parsing and source-level semantic processing.                                 |
| **Lowering**            | Transformation from a higher-level representation into a lower-level representation. |
| **Backend**             | MachineIR processing and target-specific code generation.                            |
| **Metatracing**         | The tracing semantics provided by the TraceML/MetaKrivine execution model.           |

Use **IR** rather than **intermediate language** when referring to TraceLambda or MachineIR unless discussing a distinction that specifically requires the latter terminology.

---

## 26. Architectural Principle

The central architectural principle of TraceML is:

> **Preserve high-level semantics until the compiler stage that has sufficient information to lower them correctly, and make each lowering boundary explicit and verifiable.**

The intended relationship between the components is:

```text
                 ┌─────────────────────┐
                 │      TraceML         │
                 │   Source Language    │
                 └──────────┬──────────┘
                            │
                            │ parsing /
                            │ semantic analysis
                            ▼
                 ┌─────────────────────┐
                 │    TraceLambda      │
                 │ Lambda Calculus +   │
                 │ Metatracing Semantics│
                 └──────────┬──────────┘
                            │
                            │ machine lowering
                            ▼
                 ┌─────────────────────┐
                 │ Limestone MachineIR │
                 │ Machine-Oriented IR │
                 └──────────┬──────────┘
                            │
                            │ code generation
                            ▼
                 ┌─────────────────────┐
                 │    Machine Code     │
                 │   Target Platform   │
                 └─────────────────────┘
```

The **MetaKrivine machine** provides the conceptual execution model connecting TraceML semantics with the TraceLambda representation:

```text
                 TraceML Semantics
                        │
                        ▼
                MetaKrivine Model
                        │
                        ▼
                 TraceLambda IR
                        │
                        ▼
               Limestone MachineIR
                        │
                        ▼
                  Machine Code
```

Changes to any stage should be evaluated in terms of their effect on this overall semantic pipeline.

---

## 27. Agent Instructions

AI coding agents working on TraceML should:

1. Read this file before making repository-wide changes.
2. Inspect the existing implementation before assuming a component's design.
3. Treat existing tests and verifiers as part of the compiler's specification.
4. Preserve the semantics of the MetaKrivine execution model.
5. Keep compiler stages conceptually separated.
6. Avoid introducing target-specific behavior into higher-level compiler stages.
7. Add regression tests for bug fixes.
8. Update relevant documentation when changing public semantics or IR invariants.
9. Prefer small, reviewable changes over broad refactors.
10. Verify changes at the earliest relevant compiler stage before debugging downstream stages.
11. Do not invent semantics that are not established by the language or IR specification.
12. When the intended semantics are unclear, inspect existing code, tests, and documentation before implementing a new interpretation.
13. If the repository contains a formal specification, treat that specification as authoritative over informal assumptions.
14. If a compiler transformation appears to require a semantic change, make that change explicit rather than hiding it inside a lowering or optimization pass.
15. Never silently discard information required by later tracing, debugging, or code-generation stages.

---

## 28. Definition of Done

A compiler change is generally complete when:

* the implementation matches the intended semantics;
* relevant IR invariants are preserved;
* affected verifiers pass;
* existing tests pass;
* new behavior has appropriate regression coverage;
* generated IR is correct;
* end-to-end behavior is correct where applicable;
* diagnostics remain meaningful;
* documentation has been updated when necessary;
* no unrelated files or behavior have been changed.

For changes affecting metatracing, correctness must be evaluated not only by the final program result but also by whether the required tracing semantics remain intact throughout the compiler pipeline.

