# Limestone implementation status

The framework has connected C/C++ subsystems, standalone textual IL adapters,
opaque C APIs, a D MachineIR package, and a relocatable installation. Target
properties come from explicit metadata or host adapters. The component
`AGENTS.md` files describe the architecture, including facilities that require
further adapters; they are not a declaration that every listed feature is
implemented.

## Implemented

| Component | Available behavior |
| --- | --- |
| Metacode | Source-located ISA ingestion; registers/classes/aliases, operation/encoding normalization, shared declarative operand predicates, unknown-metadata preservation, deterministic JSON |
| Unisel | Semantic UMD load/print, bounded source-located include graphs, explicit Infobank selection patterns, typed/repeated-binding/register-class and declarative operand legality, owning C/Python candidate/constraint/selection inspection, Satie global selection, greedy selection, CFG/effect-preserving Scheduler IR emission |
| Limeburg | Deterministic typed BURS dynamic programming, stable-ID state tables and rejection traces via C/C++/Python/CLI, bounded textual includes, nested patterns, immediate/identity predicates, rule priorities and provenance, effect legality, UMD adapter, 23 reproducible Infobank-derived `.lburg` specifications with explicit coverage, tree-only and shared-value-preserving graph policies |
| Schedrow | Register/physical/memory/control hazards, SSA and architectural result latency domains, alternative resources, fractional quantities, occupancy/offsets, throughput metadata, priorities/critical paths, joint issue-slot/resource matching, ordered/adjacent/atomic/same-cycle/bundle/fusion/pair groups, list/CFG/modulo scheduling, schedule and sequential-order verification |
| RegTL | CFG liveness, precise interference and lifetime segments, architectural-state liveness, classes, aliases, fixed/allowed/forbidden registers, ties, early definitions, clobbers, linear/greedy/coloring/constraint allocation, parallel-move resolution, private spill frames and scratch transfers, post-allocation scheduling adapter |
| Tunah | Equinox-NG-backed bounded saturation; transactional EkippX/SExprTk/DSLtk rule loading; host predicates, checked costs and traces; owning C/Python sessions/results, typed target attachments and binary/runtime transforms; typed bidirectional Unisel graph reconstruction/remapping with effect/CFG/live-out preservation; validated lifted-semantic Bin2Bin adapter with owning rule snapshots and deterministic cache identity |
| Bin2Bin | Fixed8 and explicit masked codecs, signed/register/PC-relative fields, status/control/trap preservation, semantic operand matching/lifting/transforms, CFG analysis, relocated same/cross-ISA translation, monotonic branch relaxation, complete selected operand bindings, bounded owning ELF32/ELF64 REL/RELA object ingestion/emission, metadata-driven symbol/bitfield relocation and addressed linking via C/C++/Python/CLI, version-sensitive memory/LMDB caches, bounded heat-tracking C/C++ runtime with owning installers and invalidation, Python observation handles |
| MachineIR | Architecture-neutral D instructions/operands/effects/CFGs; dominance, liveness, use/def and serialization; validated versioned C++/D region/CFG/spill/group exchange |
| TraceML | Source-located S-expression frontend, lexical call-by-name MetaKrivine execution, checked integer primitives, observers/events, budgets/cancellation, guard-preserving trace-to-graph lowering, explicit-target C/C++/Python/CLI compilation and a callable native constant/return integration |
| Exolayer | Opaque C contexts, typed callbacks, scalar layouts, extension loading/lifetimes, fixed/variadic scalar native signatures, owning exact-width scalar/struct/embedded-array descriptors and fixed/variadic transactional by-value data calls, explicit calling conventions, owning native-library bindings through an isolated libffi adapter |
| VMWeave | Kaguya-backed Lua compiler; validated owning model, lossless versioned STK-00, deterministic optional C components, four execution models, checked stack/tapes/labels, allocator adapters, objects/lifetimes, frames, module exports, atomic values, IPC, explicit rewrites and inline caches; built-in MachineIR foreign-call lowering, assembly/AOT native output and owning executable handles, generated C baseline-JIT binding |
| Orchestration | C/C++ target/program/configuration APIs, optimizer/allocation/backend adapters, selected fixed-operand/ABI requirements merged into allocation and independently checked in encoding, inspectable stages, final spill/register/frame inspection, MachineIR handoff, byte/relocation/ELF output and CLI |

Textual Unisel, Limeburg, Schedrow, and RegTL loaders have independent targets and canonical
serializers. `limestone/il.h` exposes document/result handles that can be used
without the top-level pipeline. All ten grammars and AST schemas are generated
through DParser and `scripts/syngen.pl`; generated tables and parser headers are
build/install artifacts. Optional SWIG Python bindings consume the C ABI.

The Tunah instruction corpus has 13 tuner files and 239 rules, with explicit
width, overflow, memory-forwarding, and vector-shape preconditions. Detailed
rule semantics and optimizer measurements remain in `tunah/README.md` and
`tunah/BENCHMARKS.md`.

The Limeburg Infobank corpus has 1,935 instruction rules across 3,395 source
instructions. Instruction-scoped roots preserve inventory semantics; explicit
selection trees declare generic source computations. Another 1,460 operations
have documented coverage boundaries. Provenance, metadata, naming, regeneration,
and the per-architecture counts are in `limeburg/specs/README.md`.

## Integration contracts

- Source value graphs are acyclic SSA; CFGs may contain cycles. Values crossing
  blocks must dominate their uses. Phi elimination is an input adapter.
- Blocks are laid out in declaration order with entry first. A conditional
  branch's second target is the next block; the direct encoding binding uses
  target index zero. Scheduling cycles are block-local.
- Selection preserves observable source-effect order before fusion. Memory and
  control contracts remain separate from costs, resources, and allocation.
- Allocation uses the selected emission order. Final scheduling includes register
  reuse/alias hazards. Original spill decisions remain in `Module::allocation`;
  final transfers, frame, assignment, and schedule are in `Module::materialized`.
- Spill transfers use an explicit private address space and target load/store
  models. Live-ins, outputs, and explicit live-outs are nonspillable unless a
  boundary-transfer adapter is supplied.
- MachineIR exchange version 1 supports simple regions; version 2 carries CFG,
  control flow, spill frames, and architectural-result latency. Version 3 adds
  groups, including overlapping membership, adjacency, equal cycles and slots. Both sides check group
  constraints, SSA availability, dependency/order/latency, CFG issue layout and malformed fields.
  Returned D programs admit immediate and provenance edits; structural/effect
  changes require reconstruction adapters. Machine capacities remain target data.
- Native invocation uses scalar `EXL_I64`, `EXL_F64`, and `EXL_PTR` arguments,
  scalar/void results, and at most 32 arguments. Variadic registrations describe
  an exact already-promoted argument shape and a fixed prefix. Extensions use the distinct
  host callback ABI. Runtime installers retain executable ownership through calls.
- Native data-buffer calls use independent owning exact-width scalar and natural
  nested-struct/embedded-array types. Registrations retain type snapshots; inputs
  are copied into aligned storage, and result bytes are committed on success.
  Variadic data calls support aggregate arguments/results, exact named argument
  types and an explicitly default-promoted ellipsis shape.
- ELF32/ELF64 object identity and relocation encodings require explicit
  `tooling.object_file` metadata. Object emission retains all named fixups;
  linking resolves local/global/weak symbols at an explicit installation base,
  checks ranges/overlaps/scales and returns immutable owning image bytes.
  REL and RELA ingestion/emission support both byte orders; REL linking requires
  explicit implicit-addend signedness in the relocation metadata. ELF32 field
  narrowing is checked, and the C ABI retains its existing descriptor layouts.

## Remaining adapters and unsupported contracts

These are concrete implementation boundaries, rather than silently approximated
behavior:

- VMWeave's built-in MachineIR C backend uses a GCC/Clang-compatible compiler and
  POSIX process/shared-library loading. Host-compatible ABI/layout is checked;
  other executable platforms require a backend adapter. Moving/tracing GC,
  dynamic module formats, and richer object layouts use subsystem extensions;
  built-in objects use explicit reclamation.

- Production Infobank entries need complete legal selection patterns, timing,
  operand/ABI lowering, and encoder contracts before they can generate executable
  programs. Ingestion of 23 ISAs is covered; universal production code generation
  for those ISAs is not established.
- Native variable-length codecs beyond the masked model, archives,
  dynamic linking, COMDAT, TLS/GOT/PLT construction and complex relocations,
  generalized cross-ISA state/ABI transformations, and higher-level/LLM decompiler
  plugins need backend adapters. ELF32/ELF64 ET_REL with REL/RELA and scalar relocation
  fields are implemented; see `bin2bin/OBJECTS.md`.
- Executable TraceML guards need a backend deoptimization and continuation
  contract. General closure/environment code generation needs runtime lowering.
- Target-specific predicates beyond the closed operand-legality contract, string graph arguments, and unsupported graph
  properties are rejected. Richer Limeburg memory contracts require adapters.
  Cross-block motion, multi-slot instructions and ranged/operand-to-operand
  timing require adapters.
- RegTL bank/tuple/subregister-lane/rematerialization constraints need target
  adapters. Aliasing currently represents pairwise overlapping storage. Coloring
  and constraint allocation share a bounded grouped search; it is not a PBQP
  implementation. Spill boundary/control transfers and target-specific implicit,
  tied, or early-clobber transfer instructions require adapters.
- Tunah region-level binary and additional stage IL adapters, complete preprocessing source maps,
  preemption within vendor operations, and full e-node derivation provenance
  remain open. Time/cancellation checks are cooperative.
- Native packed/over-aligned records, unions, bitfields, vectors,
  and foreign exception propagation need FFI adapters. Top-level C
  arrays decay to pointers; data calls accept arrays embedded in structs.
  The supplied FFItk native-call function returns `FFI_ENOSYS`;
  the isolated libffi adapter implements the supported native subset.

## Validation coverage

The standard Linux x86-64 configuration registers 39 CTest tests when Lua and D tools are
available; enabling Python bindings adds two integration tests. Coverage includes
positive/negative parsing, solver legality, CFG/issue-vector scheduling verification, all allocators,
native/callback ownership, mixed integer/floating/pointer variadic stack calls,
fixed and variadic aggregate argument/return classification, owning type snapshots, exact scalar
extrema, nested/array layouts, Infobank specification freshness and selection,
bounded filesystem/virtual include graphs and provenance, executable generated VM code, the tuner corpus, C99
and C++ consumers, exported C ABI symbols, standalone owning optimizer handles,
callback snapshot lifetimes, optimized binary/runtime C APIs, native Python proof
callbacks, owning object/link APIs and installed standalone object consumers,
and relocation of the installed CMake package.

Backend tests independently execute encoded branch, fused-memory, shared-value,
and spill/reload programs. Object tests link and execute two compiler-produced
native ELF objects (result 42); reemission is also linked and executed by the
system compiler/linker. The native TraceML slice exercises all three selectors
and four allocators, executes signed-32-bit boundary/negative results, and links
an emitted function with a C consumer. Exchange tests run C++ -> D -> C++, re-encode edited
CFG/spill programs, and execute results 43 and 44. The D suite runs four unittest
modules and loads all 23 Infobank ISAs. Validation configurations include native
FFI enabled/disabled and C++ AddressSanitizer/UndefinedBehaviorSanitizer/leak
detection. See the build commands in `README.md` to reproduce these checks.

Metadata regression coverage checks real JSON line/column provenance, UTF-8
boundaries, Unicode escapes and control-character round trips, decimal unsigned
64-bit extrema, and preservation of sections declared before `arch`. C++ and D
ISA loaders reject malformed quoted strings, and the D semantic-expression
parser preserves their decoded values through printing and reparsing. Embedded
NUL input is rejected by the C++ ISA and TraceML entry points.

Scheduling regressions check relaxed atomic read/read coherence in list and
modulo schedules, sequential-order verification, and D MachineIR exchange.
Unknown aliases preserve order; explicitly disjoint alias sets or address
spaces retain independent scheduling.
