# Applicability Contracts

Kernels declare when they are valid through explicit applicability metadata.

## Positive requirements

Conditions that must hold for the kernel to apply:

```lisp
(requires
  (capability typed-expressions)
  (analysis dominance-information))
```

## Negative requirements

Conditions that must not hold:

```lisp
(forbids
  (unknown-memory-dependence)
  (unmodeled-observable-store-effects))
```

## Missing facts

A kernel must not interpret missing information as proof that a condition holds.
Unknown aliasing does not mean two memory references are independent.

## Transformation history vs. current properties

- "SSA construction has been applied" is historical information
- "The program is in SSA form" is a property of the current IR
- Prefer checking the current property when sufficient
- Use transformation history only when the history itself matters

## Prerequisites

```lisp
(prerequisites
  (optimization-applied "blackstone.mir.ssa.construction"))
```

## Forbidden prior transformations

```lisp
(forbidden-prior-transformations
  "blackstone.lir.register-allocation")
```

## Analysis contracts

```lisp
(analysis-contract
  (requires loop-information memory-dependence)
  (invalidates loop-information memory-dependence))
```

A kernel must declare the analyses it consumes and, where supported, the analyses it
preserves or invalidates.
