# AGENTS.md — Bin2Bin

## 1. Purpose

**Bin2Bin** is the binary translation and binary analysis subsystem of the **Limestone late-stage compiler backend framework**.

Bin2Bin consumes architecture and instruction information from the **Metacode Infobank** and provides a common infrastructure for:

* Static Binary Translation (SBT)
* Dynamic Binary Translation (DBT)
* Just-In-Time (JIT) Binary Translation
* Same-ISA binary rewriting and optimization
* Cross-architecture binary translation
* Binary lifting and semantic normalization
* Binary decompilation to high-level assembly
* LLM-assisted binary analysis
* Runtime code translation for virtual machines and emulators

Bin2Bin must not assume that binary translation always means changing architectures. Source and target architectures may be identical when the purpose is optimization, rewriting, instrumentation, specialization, canonicalization, or other binary transformations.

---

# 2. Metacode Infobank

The **Metacode Infobank is the authoritative source of ISA information used by Bin2Bin**.

Bin2Bin should consume architecture-specific information from the Infobank rather than duplicating ISA definitions inside the Bin2Bin implementation.

The Infobank uses the `isa-description-bundle` format. Its architecture descriptions provide architecture identity, family, model, ISA version, instruction counts, register classes, semantics, compiler information, and tooling metadata.
The semantic representation of the Infobank is S-expression based.

The existing architecture and instruction tooling metadata is extended specifically for Bin2Bin.

---

# 3. Bin2Bin Infobank Contract

Every architecture description consumed by Bin2Bin should expose a:

```text
tooling.bin2bin
```

object.

Every translatable instruction should expose:

```text
op.tooling.binary_translation
```

metadata.

These fields form the **Bin2Bin translation contract** between Metacode and the Bin2Bin backend.

The Bin2Bin metadata must describe how an architecture or instruction participates in decoding, lifting, semantic translation, control-flow analysis, optimization, target lowering, and binary emission.

Bin2Bin-specific metadata is versioned independently from the general tooling metadata.

The repository therefore contains:

```text
bin2bin.schema.json
```

as the schema for the Bin2Bin-specific extension.

The current Bin2Bin metadata schema is version **1**.

---

# 4. Architecture-Level Bin2Bin Metadata

`tooling.bin2bin` provides information that applies to the complete architecture rather than to an individual instruction.

The metadata covers, where applicable:

* Execution domain
* Semantic representation
* Instruction encoding model
* Instruction encoding widths
* Endianness
* Word size
* Address size
* Instruction-boundary rules
* Control-flow model
* Branch-target sources
* Relocation model
* Indirect-target behavior
* Translation-unit model
* Unsupported/unknown instruction policy
* Translation-cache identity requirements

Architecture metadata must describe the properties required to correctly decode and translate binaries for that architecture.

Architecture-level metadata must not be used to duplicate instruction-specific semantics that already belong in `op.tooling.binary_translation`.

---

# 5. Instruction-Level Bin2Bin Metadata

Each instruction supported by Bin2Bin should expose:

```text
op.tooling.binary_translation
```

This metadata describes the properties required to translate that particular instruction.

It may describe:

* Semantic source
* Encoding source
* Operand interpretation
* Explicit dataflow
* Implicit dataflow
* Register reads
* Register writes
* Memory reads
* Memory writes
* Control-flow behavior
* Block termination
* Branch-target classification
* Memory effects
* Trap behavior
* Atomicity
* Serialization requirements
* Privilege requirements
* Implicit architectural state
* Flag reads
* Flag writes
* Target-lowering requirements
* Semantic-equivalence requirements

The Bin2Bin implementation should use this metadata instead of reconstructing these properties independently from ad-hoc instruction tables whenever the Infobank provides them.

---

# 6. Native and Virtual Instruction Sets

Bin2Bin must distinguish between different execution and encoding domains.

Not every architecture description in the Infobank represents a conventional native CPU instruction set.

For example, architecture descriptions may represent:

* Native machine ISAs
* Virtual ISAs
* Bytecode formats
* Intermediate representations
* Textual machine representations

The Bin2Bin metadata must therefore identify the execution/representation domain rather than assuming that every architecture is directly executable machine code.

This distinction is important for:

* Decoder selection
* Binary loading
* Instruction boundary determination
* Address calculation
* Control-flow analysis
* Target lowering
* Binary emission
* Runtime translation

Bin2Bin must not incorrectly apply native-machine assumptions to virtual or intermediate instruction sets.

---

# 7. Translation Pipeline

The canonical Bin2Bin translation pipeline is:

```text
             Source Binary
                  │
                  ▼
        Binary Format / Loader
                  │
                  ▼
              Decoder
                  │
                  ▼
          Instruction Stream
                  │
                  ▼
        Semantic Lifting
                  │
                  ▼
      Normalized Translation IR
                  │
                  ▼
       Pattern Transformation
                  │
                  ▼
       Equality Saturation
                  │
                  ▼
        Optimization / Rewrite
                  │
                  ▼
          Target Lowering
                  │
                  ▼
             Encoder
                  │
                  ▼
             Target Binary
```

The Infobank supplies architecture-specific information required at each stage.

The translation pipeline should remain architecture-independent wherever practical.

---

# 8. Static Binary Translation

Static Binary Translation (SBT) translates binary code before execution.

The general flow is:

```text
Input Binary
    │
    ▼
Decode
    │
    ▼
Analyze
    │
    ▼
Lift
    │
    ▼
Transform / Optimize
    │
    ▼
Lower
    │
    ▼
Encode
    │
    ▼
Output Binary
```

Static translation is primarily exposed through the Bin2Bin CLI.

The C and C++ APIs may also expose static translation functionality when embedding applications require programmatic access.

Typical uses include:

* Cross-architecture migration
* Offline binary optimization
* Binary rewriting
* Compatibility translation
* Instrumentation
* Binary specialization
* Code-size optimization
* Security-oriented rewriting

---

# 9. Dynamic Binary Translation

Dynamic Binary Translation (DBT) translates code during execution.

The runtime should be able to:

1. Observe or receive guest code.
2. Decode the code using the source architecture description.
3. Lift it into the Bin2Bin representation.
4. Transform and optimize it.
5. Lower it to the target architecture.
6. Emit executable target code.
7. Cache the translated region.
8. Execute the translated code.

Conceptually:

```text
Guest Execution
      │
      ▼
Guest Code
      │
      ▼
Decode / Lift
      │
      ▼
Translate / Optimize
      │
      ▼
Cache
      │
      ▼
Execute Host Code
```

DBT is primarily an API-driven facility.

It is intended for integration with:

* Virtual machines
* Emulators
* Compatibility runtimes
* Dynamic instrumentation systems
* Sandboxing systems
* Runtime binary analysis systems

---

# 10. JIT Binary Translation

Bin2Bin supports Just-In-Time binary translation as an extension of dynamic translation.

The runtime may collect execution information and identify code regions that are sufficiently hot to justify additional optimization.

A typical JIT workflow is:

```text
Guest Execution
      │
      ▼
Runtime Profiling
      │
      ▼
Hot Region Detection
      │
      ▼
Trace / Region Formation
      │
      ▼
Translation
      │
      ▼
Aggressive Optimization
      │
      ▼
Native Code Generation
      │
      ▼
JIT Cache
      │
      ▼
Repeated Execution
```

JIT translation is always an API-oriented facility.

The JIT subsystem should be able to share:

* Decoder infrastructure
* Semantic lifting
* Translation IR
* Pattern rules
* Equality saturation
* Optimization passes
* Target lowering
* Encoding
* Translation cache infrastructure

with static and dynamic translation.

---

# 11. Translation Cache

Bin2Bin uses:

```text
third_party/lmdbxx
```

for persistent translation-result caching unless caching has explicitly been disabled.

The cache must be treated as a correctness-sensitive component.

A cached translation must not be reused when any relevant input to translation has changed.

Cache identity should therefore incorporate, as applicable:

* Source architecture
* Source ISA version
* Target architecture
* Target ISA version
* Relevant architecture-description version
* Infobank version
* Bin2Bin version
* Bin2Bin translation-schema version
* Translation-rule version
* Optimization configuration
* Translation configuration
* Plugin versions
* Source binary identity
* Relevant runtime configuration

JIT and DBT caches may additionally include execution-environment information where necessary.

The Bin2Bin architecture metadata provides the information required to construct a sufficiently strong translation-cache identity.

---

# 12. Translation Rules

Bin2Bin uses a combination of semantic transformation rules and pattern-based rewriting.

Translation and optimization rules may be represented using **S-expressions**.

The S-expression parser provided by:

```text
third_party/metatk
```

is used for parsing these representations.

Rules should preferably operate on semantic representations rather than directly manipulating architecture-specific instruction encodings.

This allows the same transformation infrastructure to be reused across multiple architectures.

---

# 13. Equality Saturation

Bin2Bin uses:

```text
third_party/equinox-ng
```

for equality-saturation-based transformation.

Equality saturation allows Bin2Bin to represent multiple semantically equivalent forms simultaneously and select an appropriate representation after transformation.

Potential applications include:

* Instruction canonicalization
* Algebraic simplification
* Peephole optimization
* Instruction combining
* Redundant-operation elimination
* Cross-architecture idiom recognition
* Target-specific lowering
* Same-ISA optimization
* Code-size optimization
* Performance-oriented instruction selection

Translation rules must preserve the semantic contract defined by the source and target architectures.

---

# 14. Same-ISA Translation

Bin2Bin must support:

```text
Source ISA == Target ISA
```

as a normal translation case.

Same-ISA translation may be used for:

* Binary optimization
* Instruction canonicalization
* Binary rewriting
* Instrumentation
* Specialization
* Code-size reduction
* Performance optimization
* Security transformations
* Removal of redundant instructions

The core Bin2Bin abstraction should therefore operate on:

```text
Source Architecture → Target Architecture
```

rather than on the assumption:

```text
Source Architecture → Different Target Architecture
```

---

# 15. Binary Decompilation

Bin2Bin also provides binary decompilation and analysis.

The native decompiler produces **high-level assembly**, not conventional source languages such as C or C++.

Higher-level reconstruction may be implemented through plugins.

Potential plugin targets include:

* C
* C++
* Rust
* Domain-specific languages
* Custom intermediate representations

LLMs may be used through the OpenAI web API to assist with binary analysis and decompilation.

LLM-generated interpretations must remain distinguishable from deterministic facts obtained from:

* Binary decoding
* Instruction semantics
* Control-flow analysis
* Dataflow analysis
* Architecture metadata
* Runtime observations

LLM output must not silently override authoritative machine-derived information.

---

# 16. API and CLI Responsibilities

## CLI

The CLI is intended primarily for offline operations:

* Static translation
* Binary inspection
* Binary decompilation
* Offline optimization
* Binary rewriting
* Translation diagnostics

## C API

The C API provides an embedding interface for applications that need Bin2Bin functionality programmatically.

## C++ API

The C++ API provides the higher-level embedding interface and is particularly relevant to:

* Virtual machines
* Emulators
* JIT runtimes
* Dynamic binary translators
* Runtime instrumentation
* Binary analysis applications

Dynamic translation and JIT functionality are API-driven facilities.

---

# 17. Virtual Machines and Emulators

A major intended use of Bin2Bin is as the translation engine inside virtual machines and emulators.

A typical integration is:

```text
┌─────────────────────┐
│ Guest Architecture  │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│     Guest Code      │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│       Bin2Bin       │
│                     │
│ Decode              │
│ Lift                │
│ Transform           │
│ Optimize            │
│ Lower               │
│ Encode              │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│   Host Architecture │
└──────────┬──────────┘
           │
           ▼
     Native Execution
```

The runtime should not need to implement a separate ISA semantic database when the required information is available through the Infobank.

---

# 18. Architecture Extension

Adding a new architecture to Bin2Bin should primarily involve adding or extending its description in the Metacode Infobank.

An architecture entry should provide:

1. Architecture identity.
2. Family, model, and ISA version.
3. Register classes.
4. Instruction semantics.
5. Compiler information where available.
6. Existing tooling metadata.
7. `tooling.bin2bin`.
8. `op.tooling.binary_translation` for supported instructions.

Architecture-specific information should not be copied into Bin2Bin unless there is a concrete implementation reason to do so.

The Infobank remains the source of truth.

---

# 19. Per-Instruction Translation Requirements

When adding an instruction to an ISA description, maintainers must consider its effect on binary translation.

At minimum, the instruction should have sufficient metadata for Bin2Bin to determine:

* What architectural state it reads.
* What architectural state it writes.
* Whether it reads or writes memory.
* Whether it changes control flow.
* Whether it terminates a translation block.
* How its branch targets are determined.
* Whether it can trap.
* Whether it has atomic semantics.
* Whether it has serialization requirements.
* Whether it has privilege requirements.
* Whether it has implicit state effects.
* How it should be lowered to another architecture.

Instructions for which translation is not currently possible should be explicitly represented as unsupported or requiring a fallback path rather than being silently treated as ordinary instructions.

---

# 20. Unsupported and Ambiguous Instructions

Bin2Bin must fail safely when an instruction cannot be translated with sufficient semantic confidence.

The implementation should distinguish between:

```text
Supported
Unsupported
Requires Fallback
Architecture-Specific
Privileged
Environment-Dependent
Semantically Ambiguous
```

An instruction must not be translated merely because it can be syntactically decoded.

Correct semantics take precedence over producing output code.

---

# 21. Correctness

Binary translation is semantics-sensitive.

The primary correctness requirement is:

> The translated program must preserve the observable behavior of the source program within the defined source and target execution models.

Where practical, Bin2Bin should support:

* Instruction-level semantic validation
* Differential execution
* Translation-rule tests
* Cross-architecture regression tests
* Same-ISA regression tests
* Round-trip tests
* Translation-cache validation
* Deterministic translation
* Unsupported-instruction diagnostics
* Control-flow validation
* Dataflow validation

Equality-saturation transformations must be based on valid semantic equivalences.

---

# 22. Determinism and Reproducibility

Static translation should be reproducible given identical:

* Input binary
* Source architecture
* Target architecture
* Infobank revision
* Bin2Bin revision
* Translation rules
* Optimization configuration
* Plugin versions

Where deterministic operation is requested, runtime-dependent heuristics must not affect the generated output.

Dynamic and JIT translation may intentionally use runtime information and therefore have different reproducibility requirements.

---

# 23. Infobank Versioning

The Infobank schema is versioned.

The current top-level ISA bundle identifies:

```text
format = isa-description-bundle
version = 2
semantic_format = s-expression
```

The schema requires architecture descriptions containing the core architecture and semantic information used by consumers.

Bin2Bin-specific metadata has its own schema/version boundary.

Changes to:

* ISA semantics
* Instruction encodings
* Register definitions
* Tooling metadata
* Bin2Bin translation metadata
* Translation-rule metadata

may invalidate existing translation-cache entries.

Cache invalidation must therefore be considered part of Infobank evolution.

---

# 24. Repository Periphery

The Bin2Bin integration is not limited to individual `.isa` files.

The following files form part of the Bin2Bin Infobank contract:

```text
schema.json
bin2bin.schema.json
manifest.json
ISA_TOOLING_AUGMENTATION.md
BIN2BIN_TRANSLATION.md
```

These files must remain synchronized with the architecture descriptions.

When adding a new ISA:

1. Add the architecture description.
2. Add its Bin2Bin architecture metadata.
3. Add instruction-level binary-translation metadata.
4. Update the manifest.
5. Validate the description against the relevant schemas.
6. Update Bin2Bin documentation where the translation model differs from existing architectures.
7. Verify cache identity requirements.
8. Run Bin2Bin translation and semantic validation tests.

---

# 25. Third-Party Dependencies

Bin2Bin currently relies on:

| Dependency               | Role                                                                 |
| ------------------------ | -------------------------------------------------------------------- |
| Metacode Infobank        | ISA definitions, semantics, registers, compiler and tooling metadata |
| `third_party/equinox-ng` | Equality saturation and semantic transformation                      |
| `third_party/metatk`     | S-expression parsing and related language tooling                    |
| `third_party/lmdbxx`     | Translation-result caching                                           |
| OpenAI web API           | LLM-assisted binary analysis and decompilation                       |

Third-party dependencies should be isolated behind stable Bin2Bin abstractions wherever practical.

---

# 26. Development Rules

When modifying Bin2Bin:

### Do

* Prefer Infobank metadata over duplicated ISA knowledge.
* Keep translation semantics architecture-aware.
* Preserve source/target semantic correctness.
* Treat same-ISA translation as a supported use case.
* Keep static, dynamic, and JIT translation on shared infrastructure.
* Version cache inputs appropriately.
* Add tests for every new translation rule.
* Update the corresponding Infobank metadata when adding architecture support.
* Keep virtual/bytecode architectures distinct from native machine architectures.
* Keep deterministic translation separate from LLM-assisted analysis.

### Do not

* Assume every translation changes architectures.
* Assume every ISA is a native CPU ISA.
* Infer instruction semantics solely from opcode names.
* Cache translations without considering architecture and rule versions.
* Treat an LLM's interpretation as authoritative machine semantics.
* Add architecture-specific data to Bin2Bin when it belongs in the Infobank.
* Silently translate unsupported or semantically ambiguous instructions.

---

# 27. High-Level System Architecture

```text
                         Metacode Infobank
                                │
             ┌──────────────────┴──────────────────┐
             │                                     │
     Architecture Metadata                Instruction Metadata
       tooling.bin2bin                binary_translation
             │                                     │
             └──────────────────┬──────────────────┘
                                │
                                ▼
                       ┌─────────────────┐
                       │     Bin2Bin     │
                       │                 │
                       │ Decoder         │
                       │ Lifter          │
                       │ Translation IR  │
                       │ Pattern Engine  │
                       │ Equinox         │
                       │ Optimizer       │
                       │ Target Lowering │
                       │ Encoder         │
                       └────────┬────────┘
                                │
             ┌──────────────────┼──────────────────┐
             │                  │                  │
             ▼                  ▼                  ▼
          Static              Dynamic             JIT
        Translation          Translation        Translation
             │                  │                  │
             └──────────────────┼──────────────────┘
                                │
                                ▼
                         LMDB Translation
                              Cache
```

---

# 28. Design Principles

Bin2Bin follows these principles:

1. **Metacode owns ISA knowledge.**
2. **Bin2Bin owns translation orchestration.**
3. **Static, dynamic, and JIT translation share the same semantic foundation.**
4. **Same-ISA transformation is a first-class translation mode.**
5. **Native, virtual, bytecode, and textual representations are explicitly distinguished.**
6. **Instruction semantics take precedence over syntactic similarity.**
7. **Correctness takes precedence over aggressive optimization.**
8. **Translation caches are version-aware.**
9. **LLM assistance is supplemental and must not replace authoritative machine semantics.**
10. **New architectures should be integrated through the Infobank whenever possible.**
11. **Translation rules should be declarative and reusable where practical.**
12. **Unsupported behavior must be explicit rather than silently approximated.**
13. **Architecture-level and instruction-level translation metadata must remain synchronized.**
14. **Changes to the Infobank are part of the Bin2Bin compatibility surface.**

---

# 29. Summary

Bin2Bin is the binary translation subsystem of Limestone.

It combines:

* Metacode ISA descriptions
* Architecture and instruction translation metadata
* Semantic lifting
* S-expression-based transformation rules
* Equality saturation
* Binary optimization
* Static translation
* Dynamic translation
* JIT translation
* Binary decompilation
* LLM-assisted analysis
* Translation caching
* C and C++ runtime APIs

The architectural boundary is intentionally clear:

```text
Metacode
    │
    │ ISA knowledge + semantics + translation metadata
    ▼
Bin2Bin
    │
    │ decode + lift + transform + optimize + lower + encode
    ▼
Translated Binary / Runtime Code
```

**Metacode describes what an architecture is. Bin2Bin determines how code is translated between those architectures and execution environments.**
