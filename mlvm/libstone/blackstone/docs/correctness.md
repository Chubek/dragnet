# Correctness and Semantic Preservation

Correctness is a primary property of every kernel.

## Semantic equivalence

A semantics-preserving optimization ensures that the original and transformed programs
are equivalent under the declared language and execution model.

## Sources of semantic hazards

Kernel authors must consider:

- Integer overflow and underflow
- Division by zero
- Signed versus unsigned arithmetic
- Floating-point rounding and exceptions
- NaNs, infinities, and signed zero
- Undefined, unspecified, or implementation-defined behavior
- Pointer aliasing
- Volatile memory accesses
- Atomic operations and memory ordering
- Exceptions and traps
- Function calls and external side effects
- Control-flow reachability
- Concurrency and data races
- Target-specific instruction behavior

## Correctness obligations

Every kernel must declare its correctness obligations:

```lisp
(correctness
  (preserve-observable-behavior)
  (respect-integer-overflow-semantics))
```

## Correctness must not depend on documentation alone

Important constraints must be represented in machine-readable metadata, checked by a
validator, or enforced by the transformation executor. Documentation explains the
contract; executable validation enforces it.

## Testing

Tests are evidence, not proof. A finite test suite does not establish general semantic
correctness. Claim formal proof only when an actual proof or verification procedure
supports the claim.
