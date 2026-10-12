# 37. Cephyr semantics, lowering, and code generation

[Contents](README.md) · [Next: End-to-end workflow](38-end-to-end.md)

The implemented semantic unit is a unique `int name(void)` function with one
return statement whose expression can be evaluated as a supported constant.
The target has 32-bit `int` and the x86-64 System V integer-return convention.

## Supported expressions

Expressions include unsuffixed signed-int literals, parentheses, unary `+`/`-`,
and binary `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, and `>>`.
Evaluation diagnoses constant overflow, zero division, and invalid shift ranges.
This checked C semantic evaluation is distinct from the modular arithmetic
defined for generic IR integer operations.

Locals, parameters, calls, declarations beyond the supported function form,
pointers, qualifiers, aggregates, storage classes, varargs, atomics, and control
flow are rejected. A syntactically valid expression can therefore fail semantic
validation before lowering.

## Output stages

```sh
build/cephyr --emit-ir examples/answer.c -o build/answer.from-c.mlvm
build/cephyr -S examples/answer.c -o build/answer.from-c.s
build/cephyr -c examples/answer.c -o build/answer.o
```

Lowering produces registered `builtin`, `arith`, and `func` operations for a
constant-return function. Typed-C/high/mid/low staged lowering remains planned.
Machine output loads an immediate into `%eax`, returns with `ret`, and records
global ELF function symbols and sizes.

`-O0` and `-O1` currently produce identical constant-return code: evaluation is
required semantic work, not an optional optimization. Other optimization levels
and language standards are not implied. The driver accepts `-std=c17`,
`-ffreestanding`, and the supported `-target` selection.

Cephyr accepts one input file per invocation; conflicting modes are invalid
usage. `-E`, `-S`, and `--emit-ir` default to stdout, while `-c` requires `-o`.
Link object files explicitly with `mlvm-ld` and provide startup code.

Reference: [Cephyr contracts](../docs/CEPHYR.md), [example C](../examples/answer.c).
