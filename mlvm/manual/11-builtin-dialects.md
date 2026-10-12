# 11. Builtin functions and integer operations

[Contents](README.md) · [Next: Verification](12-verification.md)

Register the builtin dialects before parsing their operations:

```cpp
mlvm::register_builtin_dialects(context, diagnostics);
```

This registration is idempotent. The dialect names are independently `builtin`,
`arith`, and `func`; legality sets used in conversion should reflect those names.

## Function structure

`builtin.func` has no SSA operands or results and exactly one region with one
entry block. Its `sym` attribute names a unique function; `result` specifies the
return type and defaults to void when omitted. `func.return` is a terminator
inside that function and takes zero or one operand matching the declared return.
These operations do not imply implemented generic call semantics.

## Arithmetic

`arith.constant` produces one integer value from a decimal `value` attribute.
The binary operations below require two operands and one result of identical
integer type, from `i1` through `i64`.

| Operation | Meaning |
| --- | --- |
| `arith.addi`, `arith.subi`, `arith.muli` | Modular addition, subtraction, multiplication |
| `arith.divsi` | Signed division |
| `arith.remsi` | Signed remainder |
| `arith.and`, `arith.or`, `arith.xor` | Bitwise operations |

Constants accept signed decimal int64 values. Narrower integer types additionally
accept nonnegative bit patterns through `2^width - 1`. Interpret arithmetic using
the IR type width, not the host C++ signed integer range.

Division by zero and signed-division overflow are undefined and never folded.
Remainder by zero is likewise undefined; checked folding skips undefined cases.
The generic integer kernel backend supports a smaller operator set than the
builtin arithmetic dialect: do not assume every builtin operation has a kernel
adapter merely because constant folding supports it.

When adding arithmetic rules, establish type compatibility and overflow behavior
before applying algebraic identities. C source constant evaluation and modular
IR arithmetic intentionally have different contracts.

Reference: [builtin semantics](../docs/IR.md#builtin-operations).
