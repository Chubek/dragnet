# Blackstone upstream hardening report

This report records issues in the read-only `libstone/blackstone` dependency.
It is a local upstream issue report, not a published issue or an applied patch.

## Reproduction

From the MLVM root:

```sh
python3 -B -m tools.blackstone audit --json
```

The combined catalog contains 200 records. At this revision the strict audit
reports **1,724 diagnostics**, all in upstream kernel files. The 50 supplemental
kernels have no strict-profile errors.

| Diagnostic | Count | Interpretation |
|---|---:|---|
| E-DOC | 1,200 | Eight missing documentation topics per upstream kernel |
| E-REQUIRED | 150 | Missing analysis-contract section |
| E-CONTRACT | 368 | Missing clauses, invalidation, or mismatched consumed analyses |
| E-REFERENCE | 3 | Referenced analysis IDs absent from the registry |
| E-STAGE | 1 | Required capability incompatible with stage |
| E-ID | 1 | ID inconsistent with canonical identity convention |
| E-REDUNDANCY | 1 | Duplicate optimization contract |

Most diagnostics are **stricter-profile completeness gaps**, not independently
proven unsafe transformations. Reference, stage, identity, and redundancy defects
are distinguished below. Contract validation cannot prove semantic preservation.

## Validator defects

### Diagnostic sink replacement hides errors

`optilisp/src/optilisp/validate.py:438` uses `sink = sink or DiagnosticSink()`.
`DiagnosticSink.__len__` makes an empty supplied sink falsey, so validation writes
diagnostics into a new sink instead of the caller's sink. CLI tools can report
zero errors despite unresolved references. Replace the expression upstream with
an explicit `if sink is None` check. The MLVM extension collects errors directly
without this truthiness fallback.

### Original source is discarded before structural validation

`validate_all` passes `None` to `validate_structure` at line 448. The duplicate
section check therefore never sees original sections. `_keyword_args` overwrites
duplicate keywords; `_section` picks only the first section; `_body_forms` stops
at an unexpected atom. `_requirement_items` discards unknown shapes and accepts
extra arguments; `_transform` selects only the first nested transformation.

Upstream remediation: validate all original keyword pairs and section forms,
reject unknown/duplicate fields and trailing items, and check nested arity before
constructing semantic records. The supplemental validator checks raw ASTs first.

### Schema support does not enforce all published constraints

`jsonschema_lite.py` does not enforce the ID `pattern` used by the kernel schema;
unsupported keywords and unresolved references can be ignored. Optional schema
documents for analyses, capabilities, and monoids are not validated by the
normal kernel-validation path. Either implement the exact documented subset and
reject unsupported schema constructs or use a deliberately scoped validator.
The extension uses explicit structural checks and does not claim full JSON
Schema conformance.

### Dependency cycles and duplicate optimization contracts

Upstream validates reference existence but does not check dependency cycles.
Transformation-equivalent declarations can have distinct IDs and still duplicate
the same optimization. Add deterministic graph checks and duplicate-contract
review. Extension tests include self-cycles and a 1,500-node acyclic graph.

## Concrete catalog defects

- `kernels/mir/analysis-driven/null-check-elimination.olisp` references the
  unregistered `null-state-analysis` identifier.
- `kernels/mir/parallelization/loop-parallelization.olisp` references the
  unregistered `parallelization-analysis` identifier.
- `kernels/mir/vectorization/loop-vectorization.olisp` references the
  unregistered `vectorization-analysis` identifier.
- `kernels/mir/interprocedural-optimization/function-inlining.olisp` requires
  `instruction-cost-model`, registered only for LIR. Separate profitability from
  correctness and define any MIR cost capability through a documented interface.
- `kernels/hir/constant-propagation/copy.olisp` declares
  `blackstone.hir.copy-propagation`, missing the operation component. Correct the
  ID with an explicit migration policy rather than silently changing identity.
- `kernels/mir/dead-code-elimination/trivial-phi.olisp` and
  `kernels/mir/ssa-optimization/trivial-phi-elimination.olisp` describe the same
  trivial-phi elimination contract. Select one canonical definition and document
  deprecation of the other identifier.

## Semantic review findings requiring upstream attention

- Integer identities must exclude poison/undef unless the IR's refinement rules
  justify their treatment. Removing an operation must retain operand-producing
  effects. Double negation can trap on a minimum signed value even when its final
  mathematical result would be representable.
- Redundant loads/stores and store forwarding need explicit volatile, atomic,
  external-observer, lifetime, aliasing, exception, and byte-coverage contracts.
  Matching apparent addresses is insufficient.
- LICM and invariant-load hoisting must preserve zero-trip behavior and prevent
  speculative traps on paths that previously did not execute the operation.
- Tail-call elimination needs ABI compatibility, frame-lifetime, cleanup,
  exception, and calling-convention requirements, not tail position alone.
- Same-source phi simplifications need a precise treatment of self-references
  and undefined incoming values, plus valid dominance/use-def invariants.
- Instruction-selection, ABI-lowering, spill-insertion, and stack-slot-allocation
  entries are infrastructure transformations rather than necessarily optimization
  kernels. Review catalog scope instead of padding optimization counts.
- Target combinations need a real target pattern interface and exact flag,
  clobber, register, width, and trap equivalence. Abstract pattern names alone do
  not establish that a backend supports the requested instruction.
- `testing-strategy` prose in the original kernels repeatedly says semantic
  cases “are exercised.” The upstream suite is mostly reader/schema/metadata
  testing; it is not execution evidence for all 150 transformations. Correct
  those claims and link actual tests where available.

## Monoid review

The sampled law checker substitutes hardcoded operations and identities rather
than checking the declared identity/law expressions. Wrong declarations can
therefore pass. Transformation function comparison now samples behavior, but
the catalog does not establish closure of a general transformation carrier.

Integer-addition laws cannot be claimed indiscriminately for trapping arithmetic:
for example, signed 8-bit `(127 + 1) + (-1)` traps, while `127 + (1 + (-1))`
does not. Restrict the domain to total mathematical/modular operations or supply
an explicit partial-operation algebra. Composition of partial transformations
with incompatible contracts is not a universal monoid. Document carrier,
equality relation, closure, identity, and executable evidence for each definition.

## English text

A read-only scan found no Chinese text in the upstream Blackstone files. All
supplemental source, metadata, documentation, and CLI output are English. Chinese
messages in the earlier conversation are not stored source files.
