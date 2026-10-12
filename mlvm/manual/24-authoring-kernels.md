# 24. Authoring an Optilisp kernel

[Contents](README.md) · [Next: Contracts](25-kernel-contracts.md)

A useful first kernel removes an integer AND whose two operands reference the
same SSA definition. This complete declaration is also checked in as
`examples/kernels/and-self.olisp`:

```lisp
(define-kernel
  :id "blackstone.hir.custom.and-self"
  :name "Custom bitwise identity"
  :stage hir
  (category algebraic-simplification)
  (paradigms algebraic-rewriting)
  (objective "Forward the operand of a repeated-operand integer AND.")
  (requires (capability typed-expressions) (capability integer-overflow-semantics))
  (preconditions (operand-has-compatible-integer-type))
  (forbids (operation-has-observable-side-effects))
  (transform (rewrite (bitwise-and x x) x))
  (correctness (preserve-observable-behavior))
  (expected-effects "Remove the redundant AND while retaining its operand's definition.")
  (limitations "Requires two uses of the same integer SSA definition; no memory or floating-point rewrites.")
  (documentation
    (purpose "Demonstrate authoring an executable custom Optilisp kernel.")
    (motivation "An integer bit pattern ANDed with itself is unchanged.")
    (applicability "Identical SSA operands on a pure same-width integer AND.")
    (semantic-assumptions "MLVM i1 through i64 bitwise semantics; operand computations remain allocated.")
    (known-limitations "The generic executor checks the identity per candidate; other IRs require host proofs.")
    (testing-strategy "The CLI integration test checks forwarding and preservation of the resulting SSA IR.")))
```

## Understanding the pattern

`bitwise-and` is a constructor recognized by the generic integer adapter; `x` is
a bound operand variable. Repeating `x` requires identical SSA definitions,
including block arguments. The result forwards that existing definition instead
of recomputing it. Producer computations remain present unless a later pass
independently proves them removable.

Test it with this `build/and-self.mlvm` input:

```text
mlvm 1 {
  block(%x:"i32") {
    %r:"i32" = "arith.and"(%x, %x) [];
  }
}
```

```sh
build/mlvm build/and-self.mlvm \
  --kernel-file=examples/kernels/and-self.olisp \
  --apply-kernel=blackstone.hir.custom.and-self
```

The redundant operation disappears while the block argument remains. Also test
a two-distinct-operand input: it must not match merely because the operands have
the same type or happen to carry equal constants.

Keep authored kernels in your own files. Extend through MLVM's loader and host
adapters rather than editing the read-only `libstone` dependency.

Reference: [authoring example](../examples/kernels/and-self.olisp).
