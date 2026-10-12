# 17. Pass pipelines and transactions

[Contents](README.md) · [Next: Analyses](18-analyses.md)

A `Pass` describes a named module transformation and its analysis requirements.
`PassManager` executes passes in insertion order. The current scheduler operates
at module scope; function-, region-, and block-scoped scheduling remain future
work.

```cpp
mlvm::PassManager manager;
manager.add(mlvm::constant_fold_pass());
manager.add(mlvm::canonicalize_pass());
manager.add(mlvm::dead_code_elimination_pass());
bool ok = manager.run(module, diagnostics);
```

Link `MLVM::IR` and include `mlvm/pass.hpp`. The equivalent builtin CLI selection
is `--passes=constant-fold,canonicalize,dce`.

## Commit boundaries

The manager verifies the input before running transformations. Each pass runs
on an independent module copy. A false return, emitted error, exception, changed
context, or failed verification discards that pass's edits. Earlier successful
passes remain committed.

This is a per-pass transaction, not automatically an all-or-nothing pipeline.
If an application needs the entire pipeline to commit together, run the manager
against an outer module copy and publish that copy only on success. Kernel batch
execution already supplies such an outer transaction for its request list.

## Ordering decisions

Folding can make operands dead; following it with DCE removes them. Identity
canonicalization may expose additional simplifications. Choose a documented
pipeline for your input rather than assuming the manager implicitly repeats it
to a global fixed point.

Analysis producers must be registered before a pass requires them. Cached data
is invalidated after mutation unless the pass explicitly preserves it. Pass
metadata is part of the execution contract, not a substitute for correct
transformation logic or semantic proofs.

Reference: [pass API](../include/mlvm/pass.hpp), [transaction rules](../docs/API.md).
