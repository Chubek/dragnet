# MLVM Architecture

MLVM is a multi-level, multi-dialect compiler infrastructure. A dialect owns
operation/type/attribute definitions and verifiers; a conversion maps legal
operations between dialects; a pass transforms IR while maintaining declared
invariants. The IR core must not depend on a specific language frontend or target.

## Layers

1. Source representation and language ASTs.
2. Typed language/high-level dialects.
3. Mid-level SSA, control flow, arithmetic and memory operations.
4. Low-level ABI, address-space and explicit-memory dialects.
5. Machine IR, register constraints, scheduling metadata and target operations.
6. Assembly, relocatable object, archive, and linked-image representations.

Conversions form a directed graph, not a mandatory linear chain. Conversion
requests specify source/target dialect sets, legality, and whether partial
conversion is allowed. Default behavior is failure on any illegal operation.

## Ownership and invariants

Contexts own registries and stable IDs. Modules own regions, regions own blocks,
blocks own operations, and SSA values maintain explicit use-def lists. Public
handles do not imply ownership unless documented. Verifiers are available at
each abstraction level. Transformations preserve source provenance and observable
effects where possible.

## Integration with libstone

`libstone/` is read-only. Integrate through adapters under MLVM's own source tree.
Do not make the MLVM public API expose private implementation types from a
libstone component. Missing optional components must be isolated behind feature
checks and adapters.

`MLVM::Binutils` wraps Limestone Bin2Bin's object, archive and raw relocation/link
APIs through `src/binutils/adapter.cpp`. Its public owning types are independent
of Limestone. `src/binutils/archive.cpp` preserves original archive member bytes
while remapping the library-generated GNU index. `contract.cpp` isolates vendored
tomlc99; `transform.cpp` supplies verified transactional object edits and string
scanning. Tool command implementations live in their own `tools/mlvm-*/main.cpp`
and share `tools/common` I/O, diagnostics, inspection and klib option parsing.
The compiler/assembler and static ELF-link path retain `src/driver` orchestration.
`MLVM_ENABLE_BINUTILS` gates the library adapter and commands as one build feature.
