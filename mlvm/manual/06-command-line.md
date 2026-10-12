# 6. Command-line conventions

[Contents](README.md) · [Next: Contexts](07-contexts.md)

Every built tool supports `--help`, `--version`, `--color=never`, and `--` to
end option processing. Start with the installed tool's help when checking which
binary-tool mode was built; optional library-backed commands have a broader
option set than the dependency-free fallback.

```sh
build/mlvm --version
build/mlvm-as --help
build/mlvm-readelf --help
```

## Inputs and outputs

Native binary-producing commands require the documented output arguments;
`mlvm-as` and Cephyr `-c`, for example, require `-o FILE`. Text-producing modes
may default to stdout. `-` is supported for text output where documented; it is
not a general binary stdin/stdout convention.

Tools reject unsupported options. They also protect inputs from output overwrite,
including aliases through symlinks or hardlinks and relevant response/header
inputs. Use separate artifact paths throughout a pipeline.

## Response files

An `@FILE` argument expands quoted or escaped arguments from a response file.
For example, a file named `build/opt.rsp` containing:

```text
--passes=constant-fold,dce
examples/answer.mlvm
-o build/from-response.mlvm
```

can be used as `build/mlvm @build/opt.rsp`. Nested response files are bounded to
16 levels, and cycles are rejected. These files contain arguments, not shell
programs; do not rely on shell expansion inside them.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Operation, compilation, or link failure |
| 2 | Invalid command usage |
| 3 | I/O or object/archive format error |

An executable produced by the toolchain has its own program status. The example
program returning 42 is therefore a successful demonstration even though a shell
normally treats a nonzero program status as failure.

Reference: [tool CLI contract](../docs/TOOLS.md#cli-and-verification).
