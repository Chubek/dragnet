# NodeIR Stages

Blackstone kernels target one of three NodeIR stages.

## NodeHIR (High-level IR)

Preserves high-level operations and structures where possible.

**Typical optimizations**:
- Constant folding
- Algebraic simplification
- Constant propagation
- Copy propagation
- Control-flow simplification
- Specialization

**Capabilities**: typed expressions, high-level operations, source-level semantics

## NodeMIR (Mid-level IR)

Exposes explicit control flow, data dependencies, and lower-level operational details.

**Typical optimizations**:
- SSA-based optimizations
- Loop optimizations
- Polyhedral transformations
- Interprocedural optimizations
- Vectorization
- Parallelization

**Capabilities**: SSA form, explicit control flow, dominance information, loop information

## NodeLIR (Low-level IR)

Suitable for transformations close to target instructions and machine operations.

**Typical optimizations**:
- Instruction combining
- Strength reduction
- Register pressure optimization
- Instruction selection
- Target-specific combinations
- ABI lowering

**Capabilities**: machine operations, target description, register constraints

## Stage selection

Choose the stage at which the transformation's assumptions are naturally expressed
and its intended effects are valid. A kernel must not silently depend on an invariant
that its target stage does not guarantee.
