# 19. Builtin optimization passes

[Contents](README.md) · [Next: Custom passes](20-custom-passes.md)

MLVM supplies three module passes with corresponding CLI names.

## `constant-fold`

The folder evaluates supported integer operations when operands are constants.
Addition, subtraction, multiplication, AND, OR, and XOR use unsigned modular
arithmetic at the actual IR width. Signed division and remainder use checked
arithmetic and leave undefined cases unfolded. Source locations are preserved.

For `i8`, an addition yielding mathematical 256 wraps to the zero bit pattern.
This is intentional IR semantics, not permission for the implementation to
overflow a host signed integer. A divide-by-zero expression is not replaced by
an arbitrary convenient constant.

## `canonicalize`

Canonicalization implements selected identities, including addition/OR/XOR with
zero, multiplication with one, and subtraction/XOR of a value from itself. It
forwards an existing operand or creates the appropriate zero constant.

It does not promise reassociation, distribution, global value numbering, or a
general algebraic simplifier. The custom AND-self kernel in chapter 24 illustrates
an independently authored identity rather than an undocumented builtin rule.

## `dce`

Dead-code elimination removes unused pure operations that are neither
terminators nor region owners. Read, write, and unknown effects are retained.
Unused results alone do not prove that deleting an operation is valid.

## Inspecting a pipeline

```sh
build/mlvm --passes=constant-fold examples/answer.mlvm
build/mlvm --passes=constant-fold,canonicalize,dce examples/answer.mlvm
```

Compare the two outputs: the first exposes folding while retaining input
constants; the second cleans up dead computations. Canonical names may change,
so reason about operation structure and use-def relationships rather than the
original `%a` spelling.

Reference: [arithmetic and pass semantics](../docs/IR.md), [implementation](../src/pass/pass.cpp).
