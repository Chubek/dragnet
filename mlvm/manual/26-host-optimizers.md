# 26. Host optimizers and tree rewriting

[Contents](README.md) · [Next: NodeIR](27-nodeir.md)

Use a host adapter when a kernel targets custom dialects or transformations
beyond the generic integer executor. `ModuleOptimizer` is the MLVM module-facing
extension interface:

| Method | Responsibility |
| --- | --- |
| `supports` | Reject unsupported kernel transformations explicitly |
| `stage` | Identify the module's adapter stage |
| `fact` | Supply namespaced capability, analysis, and other evidence |
| `apply` | Match candidates, prove legality, and execute changes |

Load descriptors successfully before passing them to `kernel_pass(kernel,
optimizer)`. Register every consumed analysis with the pass manager. Facts
queried globally must justify the complete application; candidate-specific
conditions belong in `apply`. The manager supplies candidate-copy rollback,
post-pass verification, and analysis invalidation.

## Expression-tree backend

`RewriteSystem` is a separate backend for Optilisp terms. Constructor heads are
literal symbols; operand symbols bind terms. Repeated bindings compare structural
equality, unlike the definition-identity rule used by graph and generic SSA
adapters. Result variables must be bound by the input pattern.

The tree engine supports rewrite forms, not arbitrary computed fold results or
global optimization verbs. `RewriteContext` supplies stage, namespaced facts,
and a candidate-specific prover receiving the kernel, input, proposed result,
and bindings.

## Termination and transactions

Tree optimization traverses preorder in stable kernel-ID order until a fixed
point. Successful rewrites invalidate unpreserved analyses and record history.
Cycles, exhausted budgets, callback exceptions, or resource violations roll back
both terms and facts.

Callbacks should be side-effect-free. A module or term transaction cannot undo
changes to an external database, a file, or application state captured by a
callback. Shared mutable optimizer instances require caller synchronization.

Use the smallest backend matching your semantics: terms for structural trees,
NodeIR for explicit graph identity, and `ModuleOptimizer` for dialect-specific
generic-module behavior.

Reference: [host interface](../include/mlvm/optilisp/pass.hpp), [rewrite guide](../optilisp/README.md).
