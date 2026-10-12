# 5. First IR workflow

[Contents](README.md) · [Next: CLI conventions](06-command-line.md)

The checked-in `examples/answer.mlvm` computes six times seven and returns the
result from a function. Its essential structure is:

```text
mlvm 1 {
  block() {
    "builtin.func"() ["result"="i32", "sym"="answer"] region {
      block() {
        %a:"i32" = "arith.constant"() ["value"="6"];
        %b:"i32" = "arith.constant"() ["value"="7"];
        %answer:"i32" = "arith.muli"(%a, %b) [];
        "func.return"(%answer) [];
      }
    };
  }
}
```

## Verify, print, optimize

```sh
build/mlvm --verify examples/answer.mlvm
build/mlvm --print examples/answer.mlvm
build/mlvm --passes=constant-fold,dce examples/answer.mlvm -o build/answer.opt.mlvm
build/mlvm --verify build/answer.opt.mlvm
```

Verification checks structural and registered operation invariants. Printing
produces canonical text with traversal-assigned SSA names; it does not preserve
your original spelling or whitespace.

Constant folding replaces the multiplication with an `i32` constant whose value
is `42`. DCE then removes the now-unused input constants. The result remains live
because `func.return` uses it. A top-level arithmetic example with no consumer
would instead be entirely removable by DCE.

## What this workflow establishes

You have parsed, verified, transformed, and serialized IR. This command is not a
general native-code compiler for arbitrary generic IR. Native code generation is
demonstrated through the supported Cephyr and assembler paths later in the manual.

If parsing fails, inspect the first diagnostic before changing a pass pipeline.
Typical mistakes are omitted `[]`, missing operation semicolons, unquoted type
names, and references to values defined later in the text.

Reference: [example source](../examples/answer.mlvm), [IR grammar](../docs/IR.md).
