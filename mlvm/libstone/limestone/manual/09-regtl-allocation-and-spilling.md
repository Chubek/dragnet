# Chapter 9. RegTL Allocation and Spilling

[Previous: Scheduling](08-schedrow-instruction-scheduling.md) · [Contents](README.md) · [Next: Equality saturation](10-tunah-equality-saturation.md)

## 9.1 Allocation as an independent problem

RegTL is the register-allocation IL and algorithm boundary. It describes values,
lifetimes, physical choices, overlap, clobbers, ties, and transfers independently
of a particular allocation strategy. The same verified problem can be consumed
by linear scan, greedy assignment, graph coloring, or bounded constraint search.

Selection determines which target operations implement the graph. Allocation
determines where their values reside. A class restriction retained by a selected
operand is an input to allocation; it is not already an assignment. Similarly,
an architectural register effect is real machine state, not an ordinary virtual
value awaiting a register.

The public model is [regtl/regtl.hpp](../regtl/regtl.hpp). Text documents use
[text.hpp](../regtl/text.hpp); final scheduling/spill adaptation uses
[scheduling_adapter.hpp](../regtl/scheduling_adapter.hpp).

## 9.2 Explicit ranges and analyzed functions

RegTL offers two useful entry representations:

**`Program`** is an allocation problem with inclusive live ranges, classes,
aliases, clobbers, optional exact interference, ties, and reserved registers.
It is convenient for direct allocator experiments.

**`Function`** contains declared virtual values, classes, aliases, reserved
storage, CFG blocks, and instructions with virtual and physical uses/definitions.
`analyze` derives the allocation problem through liveness and interference.
This is the normal path from the connected compiler region.

Inclusive range endpoints matter. Values live through the same point overlap
unless exact interference establishes otherwise. Do not construct ranges as
half-open intervals by habit. For CFGs, a convex range can overestimate overlap;
the analysis provides lifetime segments and explicit interference instead.

## 9.3 Liveness and exact interference

Backward fixed-point dataflow computes block live-in and live-out sets, including
loops and disconnected blocks. Definitions remove a value from backward live
state; uses add it. Explicit live-outs preserve boundary obligations. The analysis
then derives lifetime segments and the pairs that cannot share physical storage.

Architectural physical-state liveness uses a separate identity domain with its
own block and per-instruction facts. A live status register or special state can
protect overlapping storage from scratch or ordinary allocation choices.

```cpp
auto live = limestone::regtl::analyze(function);
if (!live) return report(live.error());
auto allocation = limestone::regtl::graph_color(live.value().problem);
if (!allocation) return report(allocation.error());
auto valid = limestone::regtl::verify(live.value().problem, allocation.value());
```

This fragment assumes a caller-supplied `Function` and error reporter. Inspect
`live_in`, `live_out`, `segments`, and the resulting interference when a surprising
spill occurs. Changing the allocator does not repair incorrect source uses or
missing boundary liveness.

## 9.4 Classes, aliases, reservations, and constraints

`RegClass` names legal physical members. Each virtual value has a class and may
have allowed, forbidden, or fixed choices. Reserved registers are unavailable
for ordinary assignment. Clobbers restrict values that live across their position.

A legal assignment must satisfy the intersection of all these restrictions.
A fixed requirement is not permission to ignore the class or a forbidden set.
If the intersection is empty, the problem must fail or take a legal explicit
transfer path supplied by a target adapter.

Aliases describe pairwise overlapping storage. Sharing the same physical ID or
an explicitly overlapping alias conflicts for interfering values. Alias overlap
is not made transitive: the target must describe the actual storage relation.

Operand ties require selected values to share an appropriate assignment. Early
definitions expose clobber timing relative to uses. Register banks, tuples,
subregister lanes, and rematerialization require richer target adapters; the
basic scalar class/alias model cannot silently stand in for them.

## 9.5 Four allocation strategies

| Function | Role |
| --- | --- |
| `linear_scan` | Deterministic interval-oriented assignment |
| `greedy` | Deterministic incremental assignment |
| `graph_color` | Grouped coloring/search over interference and constraints |
| `constraint_allocate` | Complete bounded search with explicit resource exhaustion |

Coloring and constraint strategies share grouped, saturation-degree-ordered search
machinery. Linear and greedy strategies delegate tied problems to grouping/coloring
so ties are solved coherently. The implementation is not a PBQP solver.

`Allocation` contains a register map and a vector of spilled virtual values.
A successful allocation may include spills if the affected values are spillable.
Nonspillable or fixed values must receive legal physical storage. A bounded search
that exhausts its budget reports `ResourceLimit`, not an unsupported claim of
unsatisfiability.

Always run the verifier when integrating an independent allocator. It checks
completeness, classes, allowed/forbidden/fixed choices, aliases, reservations,
interference, ties, spillability, and clobber legality. A custom algorithm can
change policy without changing these obligations.

## 9.6 Fixed machine and ABI operands

An instruction can require a selected definition or use to occupy a named physical
register. In target encoding metadata:

```text
encoding_operands = {
  result = { kind = fixed_definition; index = 0; register = rax; };
  imm = { kind = immediate; index = 0; };
};
```

`fixed_definition` and `fixed_use` constrain selected operand indexes without
inventing encoded fields. Target ingress resolves the register name. The pipeline
merges the requirement into an owning allocation problem through
`with_fixed_registers`, retaining existing classes, allowed/forbidden constraints,
and call restrictions. Fixed ranges cannot be spilled.

The merge applies even to a custom allocation adapter. The encoder subsequently
checks the physical assignment independently. A conflicting fixed use/definition
requires an explicit move or ABI transfer adapter; choosing another allocator is
not a semantic solution.

The native constant fixture demonstrates a definition in RAX and a return use
in RAX. Its general register class also contains another register, showing why
fixed operand metadata must constrain every allocator.

## 9.7 Transfers and parallel moves

`TransferOperand` distinguishes virtual values, physical registers, immediates,
spill locations, and owning memory-address expressions. A transfer records its
destination and source. Memory-address children are owned expressions rather
than pointers into a target object.

Parallel transfer semantics differ from sequential moves. The swap
`r0 <- r1; r1 <- r0` requires both original values. Executing the first assignment
and then reading the updated `r0` would lose one value.

`resolve_parallel_moves` lowers location cycles using explicit distinct scratch
storage, which can be a register or spill slot. The host supplies scratch legality,
including overlap, live state, class, and ABI restrictions. The helper resolves
movement order; it does not discover a universally safe scratch register.

Use these transfers for calling-convention moves, boundary values, and other
target constraints through adapters. Do not represent a required transfer merely
as an allocator preference.

## 9.8 A spill decision needs materialization

A value listed in `Allocation::spilled` has a storage decision, not an executable
reload/store implementation. Materialization requires a `SpillClass`:

| Property | Purpose |
| --- | --- |
| `klass` | Value storage category |
| `load_opcode`, `store_opcode` | Target operations that implement reload/store |
| `address_space` | Explicit private frame domain |
| `size`, `alignment` | Slot storage contract |
| `scratch` | Declared temporary physical storage |

`reserve_spill_registers` protects scratch registers and their direct aliases
before ordinary allocation. Materialization also checks allocated storage and
live architectural state. It does not guess stack offsets, a stack pointer, or a
callee-saved convention from a target name.

Frame slots and temporary identities are deterministic. Reloads define short-lived
temporary values; stores use those temporaries and target-private offsets.
`value_sources` maps new values back to original spilled values, retaining
inspection and provenance.

## 9.9 Protected units and scratch pressure

Adjacency, bundles, same-cycle groups, atomic scheduler units, fusion, and pairs
must remain valid after spills. Materialization analyzes a protected unit as a
shared scratch-allocation problem. Reloads precede the unit, and stores follow it;
transfers are not inserted between protected members.

Unit-internal spilled values can reuse scratch storage after they die. A simple
one-scratch-per-spilled-value rule would be unnecessarily restrictive, while
reusing scratch without liveness could overwrite a needed operand.

Insufficient scratch is an explicit allocation/materialization failure. Values
crossing external input/output/live-out boundaries need boundary transfer lowering
before spilling. A terminating unit that requires stores on outgoing paths needs
a control-flow transfer adapter. These failures identify an absent contract rather
than authorizing an invalid memory sequence.

## 9.10 The final allocated representation

`AllocatedRegion` owns the final region, order, spill-free temporary allocation,
verified problem, slots, frame size, value-source map, and schedule. The top-level
`Module` retains the original allocation problem and original spill decisions
separately from this materialized result.

The pipeline attaches timing/effects for reload/store opcodes, adds physical
storage hazards through `allocated_dependencies`, and runs final scheduling.
Encoding and MachineIR exchange consume the final materialized region when it
exists. `Module::order` always represents final emission order.

In C inspection, `limestone_module_spill_*` reports original decisions,
`limestone_module_value_register` reports final assignments, and frame/slot
functions report private storage. An original spilled value does not suddenly
acquire a final permanent register because one of its reload temporaries has one.

## 9.11 Text documents and standalone allocation

```text
regtl scalar {
  regclass G = [$0, $1];
  live %1:G [0, 2] { spillable = false; }
  function main {
    block entry {
      move %1 <- #42;
      instruction ret { use %1; }
      live_out = [%1];
    }
  }
}
```

Unit ranges form an explicit problem. Each function is analyzed separately.
Text loading validates classes, references, constraints, spill storage, transfers,
and CFG. Canonical output retains inspectable metadata.

```sh
build/limestone-cli --allocate-il --allocator linear tests/fixtures/allocation.regtl
build/limestone-cli --allocate-il --allocator color tests/fixtures/allocation.regtl
build/limestone-cli --allocate-il --allocator constraint tests/fixtures/allocation.regtl
```

The standalone C assignment API chooses a unit/function or the explicit range
problem. Its results own assignments and spill lists independently of the document.
Target-specific transfer materialization remains part of the connected pipeline.

## 9.12 Diagnosing allocation failures

Inspect the value's class, allowed/fixed/forbidden restrictions, reservation and
alias relation, interference, early definitions, ties, and call clobbers. For a
spill failure, additionally inspect frame layout, scratch liveness, group boundaries,
and outgoing control paths.

Compare every allocator against the same verified problem. Test difficult cases
with independent execution after materialization, including alias overlap,
protected groups, loops, and fixed operands. The verifier establishes storage
legality; execution establishes that the transfers implement the intended values.

[Previous: Scheduling](08-schedrow-instruction-scheduling.md) · [Contents](README.md) · [Next: Equality saturation](10-tunah-equality-saturation.md)
