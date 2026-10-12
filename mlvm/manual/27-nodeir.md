# 27. NodeIR graphs and translation

[Contents](README.md) · [Next: Targets](28-targets.md)

NodeIR is a header-only graph representation in namespace `mlvm::nodeir`. Its
four implementation headers are `Commo.hpp`, `NodeHIR.hpp`, `NodeMIR.hpp`, and
`NodeLIR.hpp`, under `include/mlvm/NodeIR/`. Link `MLVM::NodeIR` for include paths
and C++20 requirements; basic graph use needs no Optilisp runtime.

## Graph guarantees

HIR, MIR, and LIR are distinct `Graph<Stage>` types. Graphs own nodes, monotonic
IDs, roots, operand edges, control successors, type/effect metadata, attributes,
and provenance. Data and control cycles are representable.

Structural verification checks IDs, dangling references, literal shape, and
resource bounds. SSA dominance, opcode signatures, target legality, and
arithmetic semantics require host contracts. This differs from the builtin
generic SSA verifier and is important when choosing a representation.

## Graph rewriting

With the loader integration, `NodeRewriteSystem<NodeHIR>` and its MIR/LIR
counterparts accept stage-filtered catalogs. Another stage's kernel fails
installation atomically. Repeated variables bind identical definitions, retaining
sharing instead of duplicating structurally equal computations.

Rewriting replaces all uses, preserves bound IDs and metadata, and rejects
control-node replacement or a replacement depending on the old definition.
Unreachable definitions remain allocated; matching visits reachable nodes.
Fingerprints detect cycles independently of allocation IDs and source locations.

## Import and export

`translate_to<StageGraph>` and `translate_from<ForeignIR>` accept callbacks
producing newly owned representations. Check the returned `ok()` before reading
`value`. Explicitly map opcodes, types, effects, shared definitions, blocks,
successors, and provenance; translation does not guess lowering rules.

`optimize_translated` combines import, rewrite, and export transactionally.
Analysis/history facts commit only when all three succeed. A custom
`materialize` callback can build candidate results with host-specific metadata;
the prover must establish legality across every replaced use. Default structural
construction alone is not a semantic proof.

Reference: [NodeIR guide](../optilisp/README.md#header-only-nodeir),
[working adapter tests](../optilisp/tests/nodeir.cpp).
