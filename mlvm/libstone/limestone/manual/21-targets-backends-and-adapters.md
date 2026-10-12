# Chapter 21. Targets, Backends, and Adapters

[Previous: CLI and languages](20-cli-and-textual-languages.md) · [Contents](README.md) · [Next: Testing and development](22-testing-diagnostics-and-development.md)

## 21.1 A target is a set of explicit contracts

A Limestone target connects a source computation vocabulary to legal machine
operations and their downstream implementation. It can describe a native CPU,
a virtual register machine, bytecode, or another explicitly defined execution
domain. Its name identifies the target; its contracts establish support.

Start with the authoritative Infobank description when the ISA is already
represented there. Normalize it through Metacode and UMD rather than duplicating
registers, effects, or encodings in an orchestration callback. When required
information is absent, add the appropriate declarative contract or an isolated
adapter with a documented execution model.

The orchestration model is
[PipelineTarget](../limestone/limestone.hpp). It contains owning patterns,
instruction models, scheduling/allocation facts, BURS rules, spill contracts,
metadata, and host callbacks. Specialized algorithms remain in their component
libraries.

## 21.2 Define the execution model first

Before writing patterns, establish what source values and machine state mean:

- integer widths, signedness, and overflow behavior;
- pointer/address domains and memory access semantics;
- architectural registers, flags, stack state, and implicit effects;
- branch/call/return behavior and direct/indirect targets;
- native or virtual execution domain;
- externally visible inputs, outputs, traps, and state changes.

A pure i64 addition and a CPU addition that writes condition codes may need
different source/effect handling even when both produce the same integer bits.
A stack bytecode operation may have no ordinary SSA result while changing
observable stack state. A checked arithmetic operator requires its failure
behavior to survive lowering.

Choose a small complete slice first. The native constant fixture implements a
closed signed-32-bit value returned as i64 in RAX. The four-register backend
fixture implements arithmetic, direct CFG control, and private-frame spills.
Both are valuable precisely because their execution boundaries are explicit.

## 21.3 Map source computations to legal instructions

Declare generic compiler equivalences through `selection_tree`, not inventory
mnemonic similarity:

```text
tooling = {
  instruction_selection = {
    selection_tree = "add(?lhs:i64, ?rhs:i64):i64";
    cost = 1;
  };
};
```

This fragment belongs in a semantically justified target operation. Add immediate
ranges, bindings, class restrictions, and `where` constraints when the instruction
implements only a subset of the source computation. Mark rules that preserve
observable effects explicitly.

Unisel and Limeburg consume the normalized pattern contract independently.
Validate generic patterns with both selectors when both are supported. Inventory
rules with `ISA_*` roots retain a different contract: a source adapter must
establish ISA context and operation legality before using them.

Pattern costs describe a documented selection objective. They do not prove
equivalence or supply missing timing. A cheaper invalid form must never enter
the candidate/state set.

## 21.4 Normalize and inspect the target

```cpp
auto architecture = limestone::metacode::load_isa_file(target_path);
if (!architecture) return report(architecture.error());
auto description = limestone::unisel::from_metacode(architecture.value());
if (!description) return report(description.error());
auto target = limestone::make_target(architecture.value());
if (!target) return report(target.error());
```

This fragment assumes the component headers and host path/reporter. The normalized
description is useful for inspection and canonical UMD output; the final target
also installs a metadata codec backend when the declared binary contract is
supported.

`make_target` can succeed for an IR-only target without a backend. A requested
encoding stage still requires one. Conversely, malformed declared codec metadata
can fail ingress even when the host initially intended only inspection. Check
the returned error rather than interpreting construction success as a universal
native-code capability flag.

Keep `metadata` on the target and instruction models. It preserves original
declarations for diagnostics, specialized adapters, and future consumers. Known
interpreted contracts are checked strictly; unrelated metadata remains available.

## 21.5 Supply timing, resources, and effects

Every selected opcode needs an instruction model. Scheduling needs known latency
for each selected instruction. Resource capacities, occupancy, offsets,
alternatives, issue width, and slots must come from a target model whose provenance
is explicit.

Result-latency overrides use selected definition operand indexes at target ingress.
Architectural result latency uses physical-state identities. Ties pair definition
and use indexes; early definitions identify selected definitions. Do not copy
source SSA IDs into these target operand-index fields.

Source memory/control/trap effects remain authoritative. Target attachment can add
required implicit uses/definitions and restrictions; it cannot relax a source
effect. Throughput is retained independently from latency and hard resource
reservations.

Grouping belongs after selection, when actual instruction identities exist.
`grouping_adapter(program,region)` returns groups over those IDs. Schedrow checks
block locality, member order, adjacency, equal-cycle requirements, and slot/resource
feasibility. Preserve the group contract through allocation and spilling.

## 21.6 Define storage and boundary transfers

Provide physical classes, aliases, source/selected value classes, and allowed,
forbidden, or fixed constraints. A default class must name a declared class.
An empty logical class can classify virtual values, but supplies no physical
choices for allocation.

The default allocation adapter derives a RegTL function from the selected region
and emission order. A custom `allocation_adapter` can supply an alternative
verified `regtl::Program`. Fixed selected operands are merged before every
allocator, including custom-adapter problems.

ABI transfers need an explicit lowering model for arguments, results, call
clobbers, stack/frame behavior, preserved storage, and parallel moves. Target
names, register widths, or an ELF machine number do not establish these rules.
Conflicting fixed operands require transfer lowering rather than a relaxed
allocator constraint.

If the target needs banks, register tuples, lane constraints, or rematerialization,
introduce an adapter at the storage boundary. Keep the underlying RegTL value and
physical-state identity domains separate.

## 21.7 Make spilling executable

For each spillable class, declare load/store opcodes, private address space,
size/alignment, and scratch storage. The transfer opcodes also need instruction
models and encoding bindings. Reserve scratch and its direct aliases before
ordinary allocation.

The private frame is a logical target contract. It is not a guessed host stack
frame. A native backend that implements it with a stack pointer must define its
frame allocation, addressing, alignment, calling convention, and restoration.

Test pressure that actually spills, then independently execute reload/store
behavior. Include protected scheduling units, alias overlap, live architectural
state, and control exits. Boundary values remain nonspillable without a boundary
transfer adapter, because an arbitrary private slot is not an external argument
or result location.

## 21.8 Bind final operands to encoding fields

The metadata backend uses `encoding_operands` to map selected operands to fields:

```text
encoding_operands = {
  dst = { kind = definition; index = 0; };
  lhs = { kind = use; index = 0; };
  rhs = { kind = use; index = 1; };
};
```

Other kinds are `immediate`, `block`, `fixed_definition`, and `fixed_use`.
Direct conditional branch encoding uses block target index zero; target index one
is the next-layout fallthrough block. Fixed bindings name a physical register
without manufacturing encoded bits.

The codec declares widths, fixed mask/base, endianness, field kind, scale, and
relative-base policy. It must cover every bit with unambiguous instruction
boundaries. Register fields consume final physical names, not virtual IDs.
Spills must be fully materialized before encoding.

Use a custom backend for a format outside fixed8/masked support. It returns owning
`BackendOutput` bytes and named relocations. It must consume the final region:

```cpp
const auto& region = module.materialized
    ? module.materialized->region : module.selected;
const auto& assignment = module.materialized
    ? module.materialized->allocation : module.allocation.value();
```

This fragment assumes the backend has already checked that allocation exists
when required. Use `module.order` for emission. A backend handling unallocated
forms should implement its own explicit alternative rather than dereferencing
an absent assignment.

## 21.9 Attach a typed optimizer with owning configuration

A justified pure graph optimizer can be attached through an owning session and
typed adapter options:

```cpp
auto rules = std::make_shared<limestone::tunah::Session>();
auto loaded = rules->load_rules(
    "(operator iadd 2)(rule add-zero (iadd ?x 0) ?x)", "add-zero.rules");
if (!loaded) return report(loaded.error());

limestone::tunah::GraphAdapterOptions graph_options;
graph_options.operators["add"] = {"iadd", "i64", 2, false};
target.value().optimizer = [rules, graph_options](
    const limestone::unisel::Program& input)
    -> limestone::Result<limestone::unisel::Program> {
  auto optimized = limestone::tunah::optimize_graph(input, *rules, graph_options);
  if (!optimized) {
    return limestone::Result<limestone::unisel::Program>::err(optimized.error());
  }
  return limestone::Result<limestone::unisel::Program>::ok(
      std::move(optimized.value().program));
};
```

This fragment requires `tunah/unisel_adapter.hpp` and `Limestone::tunah_unisel`.
The host establishes that adding zero preserves this exact pure i64 operation's
semantics. The captured session is retained through the callback; avoid mutating
shared configuration while it is used. C optimizer attachment provides an explicit
copied snapshot instead.

Ingress, extracted legality, reconstruction, identity remapping, and effect/CFG
preservation belong to the typed adapter. Keep semantic graph transformations out
of the orchestration implementation itself.

## 21.10 Add objects and invocation only with their contracts

Object support requires explicit `tooling.object_file`: format identity,
alignment, flags/ABI identity, and relocation types. Backend named fixups are
converted through this contract. Addressed linking needs the actual installation
base and external symbol addresses.

Native invocation additionally needs a callable ABI and owning mapping. Exolayer
invokes supported host functions under explicit signatures; Bin2Bin installers
retain generated executable code under an explicit runtime callable. Neither
infers the other interface from a raw byte pointer.

For VM translation, supply guest-state mapping, exits, region boundaries,
invalidation, and fallback/continuation policy. For TraceML guards, define failed
guard behavior. For general closures, define environments and lifetime. These
contracts are part of executable support, not optional commentary on encoded
bytes.

## 21.11 An end-to-end target-development sequence

Progress through inspectable milestones:

1. Load the ISA and preserve all target/provenance fields.
2. Normalize legal generic patterns and verify positive/negative operand cases.
3. Compare Unisel candidates or BURS states for the same computation.
4. Emit and verify selected effects, outputs, CFG, and dependencies.
5. Supply authoritative timing/resources and verify schedules.
6. Derive allocation, exercise fixed/tied/alias constraints, and verify assignment.
7. Materialize real spills and check final hazards/order.
8. Encode exact operand boundaries and relocated control targets.
9. Package/link objects under declared relocation identity.
10. Independently execute under the declared native or virtual state model.

The repository's backend/native/object fixtures demonstrate these layers.
Extend their style with tests for your target rather than treating successful
metadata ingestion as the final milestone.

## 21.12 Regenerate and version derived artifacts

An Infobank change can affect UMD, BURS specifications, cache identities, binary
equivalence, and object behavior. Update manifest counts and schemas/contracts
when applicable, regenerate the `.lburg` corpus, and run freshness checks.

Version host semantic analyses and runtime configuration used in cacheable
translation. Preserve deterministic identity construction, owning snapshots,
stable ordering, and structured failure. These properties make adapters reusable
across the complete connected framework.

[Previous: CLI and languages](20-cli-and-textual-languages.md) · [Contents](README.md) · [Next: Testing and development](22-testing-diagnostics-and-development.md)
