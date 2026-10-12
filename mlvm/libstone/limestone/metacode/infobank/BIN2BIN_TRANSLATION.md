# Bin2Bin Translation Metadata

## Purpose

The Bin2Bin extension adds a small, stable contract to the existing ISA tooling augmentation. It identifies the architecture information that a binary translator needs to decode instructions, recover data/control flow, preserve architectural state, lower semantic operations, and decide when a translated block may be cached.

The extension is deliberately layered on top of the existing ISA description. It does not replace `semantics`, `encoding`, `operands`, `dataflow`, `vmm`, `disassembly`, or `reverse_engineering`.

## Architecture-level contract

Every supported ISA contains:

```text
tooling {
  ...
  bin2bin = {
    schema_version = 1;
    ...
  };
}
```

The block records:

- the execution domain (`native_machine_code`, `bytecode`, `virtual_isa`, etc.);
- the semantic representation;
- instruction encoding and width information;
- word and address sizes;
- instruction-boundary rules;
- sources for operands, semantics, dataflow, control flow, and branch targets;
- the source of implicit architectural-state effects;
- relocation and indirect-target policies;
- handling of unknown or unsupported semantics;
- the basic translation unit used by static, dynamic, and JIT translation;
- the semantic equivalence basis;
- conditions under which a translated result can safely participate in the cache.

Values such as microarchitectural latency, throughput, and resource usage remain outside this contract and belong to the existing scheduling/microarchitecture metadata.

## Per-instruction contract

Every real `op` has a nested:

```text
op {
  ...
  tooling = {
    ...
    binary_translation = {
      schema_version = 1;
      ...
    };
  };
}
```

The per-instruction block makes the translation-critical effects explicit by indexing the existing instruction semantics and tooling metadata. It records:

- control-flow category;
- whether the instruction terminates a translation block;
- branch-target classification;
- memory effects;
- trapping, atomic, serialization, and privilege properties;
- sources for implicit register and flag effects;
- the instruction-selection pattern used for target lowering;
- the semantic equivalence basis.

The semantic S-expression remains authoritative for instruction meaning. The Bin2Bin metadata is an index over that representation and the existing tooling contracts.

## Correctness rules

Bin2Bin consumers should:

1. Treat `op.semantics` as the semantic source of truth.
2. Treat encoding definitions as the source of instruction-boundary and operand-decoding information.
3. Preserve implicit architectural state unless the semantics explicitly prove that it is unaffected.
4. Treat `unknown`, `target_dependent`, `abi_dependent`, and `architecture_specific` values conservatively.
5. Reject or preserve opaque operations when semantics are insufficient for a correctness-preserving translation.
6. Include the Infobank version and translation-rule version in persistent translation-cache identity.
7. Never derive microarchitecture-specific performance numbers from this metadata.
8. Keep static, dynamic, and JIT translation implementations on the same semantic contract.

## Non-native and virtual instruction sets

Not every Infobank entry is a conventional native CPU ISA. PTX, SPIR-V, WebAssembly, DXIL, and other virtual/bytecode/textual representations may appear as translation sources or targets.

`execution_domain` distinguishes these cases so that Bin2Bin can select an appropriate decoder/lifter and execution model instead of assuming a native machine-code byte stream.

## Versioning

The Bin2Bin extension is independently versioned as `schema_version = 1`. This avoids changing the existing tooling augmentation contract while allowing Bin2Bin-specific metadata to evolve independently.
