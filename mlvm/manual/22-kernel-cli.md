# 22. Applying kernels from textual IR and the CLI

[Contents](README.md) · [Next: Kernel API](23-kernel-api.md)

An Optilisp-enabled `mlvm` driver reads ordered kernel requests from textual IR.
Save this complete example as `build/kernel-demo.mlvm`:

```text
mlvm 1 {
  apply "blackstone.hir.constant-folding.integer-mul";
  block() {
    "builtin.func"() ["result"="i32", "sym"="answer"] region {
      block() {
        %a:"i32" = "arith.constant"() ["value"="6"];
        %b:"i32" = "arith.constant"() ["value"="7"];
        %r:"i32" = "arith.muli"(%a, %b) [];
        "func.return"(%r) [];
      }
    };
  }
}
```

```sh
build/mlvm build/kernel-demo.mlvm --passes=dce
```

The request runs before ordinary passes. The multiply becomes a constant 42 and
DCE removes its unused inputs. The return keeps the result live.

## Loading user source

Both options below are repeatable:

```sh
build/mlvm input.mlvm --kernel-file=examples/kernels/and-self.olisp \
  --apply-kernel=blackstone.hir.custom.and-self
```

`--kernel-file` embeds complete source in the module; `--apply-kernel` appends an
application request. A nonmatching valid kernel is normally a successful no-op.
Duplicate definitions and unresolved IDs are errors.

Textual authoring uses `kernel "COMPLETE OPTILISP SOURCE";` before the blocks.
Escape embedded quotes and newlines using IR string rules. For convenient
self-contained serialization, load a source file and use `--print -o FILE`
rather than manually escaping a large definition.

## Execution lifecycle

`--print` and `--verify` retain directives without executing them. Generic
verification does not establish kernel executability. Normal execution consumes
successful `apply` requests while preserving kernel declarations, so reparsing
the output does not automatically reschedule the same batch.

The driver searches source-tree data, then its configured installation data.
Use `--blackstone-dir=DIR` to select the root containing `kernels` and either
`optilisp/stdlib` or `stdlib`.

Reference: [CLI kernel contract](../docs/KERNELS.md).
