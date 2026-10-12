# 21. Blackstone and Optilisp

[Contents](README.md) · [Next: Kernel CLI](22-kernel-cli.md)

Blackstone supplies optimization kernels under `libstone/blackstone/kernels`.
Kernels are written in Optilisp and describe identity, stage, requirements,
conditions, transformations, correctness obligations, and documentation.
MLVM supports both applying selected catalog entries and authoring new kernels.

## Enabling the integration

```sh
cmake -S . -B build -DMLVM_ENABLE_OPTILISP=ON
cmake --build build --parallel
```

The sfsexp reader dependency must be available. The root build exposes three
layers: `MLVM::Optilisp` reads source forms, `MLVM::OptilispLoader` validates and
installs descriptors, and `MLVM::OptilispPass` connects kernels to generic MLVM
module passes. `MLVM::NodeIR` provides a separate header-only graph model.

## Declaration versus execution

Loading a declaration establishes that its schema and references satisfy a
chosen validation profile. Execution additionally requires a backend that
supports the transformation and can establish its semantic conditions.

The generic SSA backend treats verified builtin integer operations as its HIR
domain. It supports selected modular folds and operand-forwarding identities.
Other stages, history-dependent contracts, global transformations, and unhandled
semantics need explicit host implementations.

## Catalog layouts

In the source tree, the registry is `libstone/blackstone/optilisp/stdlib` and the
catalog is `libstone/blackstone/kernels`. Installed data uses
`share/mlvm/blackstone/stdlib` and `share/mlvm/blackstone/kernels`.
Identities come from `:id` declarations, not filenames.

MLVM also maintains supplemental catalog tooling under `tools/blackstone`.
Validation of those supplemental records is distinct from execution support for
the original catalog. Known upstream reference and stage issues are recorded
separately; the integration does not silently invent missing evidence.

Reference: [kernel integration](../docs/KERNELS.md), [Optilisp overview](../optilisp/README.md).
