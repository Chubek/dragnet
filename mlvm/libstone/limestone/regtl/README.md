# RegTL

RegTL describes allocation problems independently of an allocator. Link
`Limestone::regtl` for `regtl.hpp`, `Limestone::regtl_text` for textual loading, and
`Limestone::regtl_schedrow` for allocation/scheduling/spill adaptation.

## Analyze, allocate, verify

```cpp
auto live = limestone::regtl::analyze(function);
if (!live) return live.error();
auto assignment = limestone::regtl::graph_color(live.value().problem);
if (!assignment) return assignment.error();
auto checked = limestone::regtl::verify(live.value().problem, assignment.value());
```

`Function` contains declared virtual values, classes, aliases, basic blocks,
successors/live-outs, and instruction definitions/uses. Liveness yields block
live-in/out sets, per-instruction before/after sets, lifetime segments, and exact
interference. Architectural physical-state liveness uses a separate domain.

All allocators consume `Program` and return `Allocation` register assignments plus
spilled values. Available strategies are `linear_scan`, `greedy`, `graph_color`,
and `constraint_allocate`. Coloring/constraint strategies use a shared grouped,
saturation-degree-ordered bounded search. Linear/greedy strategies delegate tied
problems to grouping/coloring. This is not a PBQP solver.

The verifier checks completeness, classes, fixed/allowed/forbidden constraints,
aliases, reserved storage, interference, ties, spillability, and clobber legality.
Instruction analysis also supports early definitions and physical uses/defs.
Register overlap is supplied as explicit pairwise aliases; it is not assumed
transitive. Banks, tuples, subregister lane constraints, and rematerialization
require target adapters.

`TransferOperand` distinguishes virtual, physical, immediate, spill, and owned
memory-address expressions. Transfers retain their direction and parallel-copy
semantics. `resolve_parallel_moves` resolves physical cycles using a caller-
supplied scratch register; the host is responsible for scratch legality.

## Spill materialization

`scheduling_adapter.hpp` exposes `reserve_spill_registers`, `materialize_spills`,
and `allocated_dependencies`. A `SpillClass` specifies ordinary target load/store
opcodes, private address space, storage size/alignment, and explicit scratch
registers. Scratch storage is protected from allocated and live architectural
state. Frames and temporary identities are deterministic.
Protected scheduling units share an analyzed scratch-allocation problem. Reloads
precede the unit and stores follow it, preserving adjacency and same-cycle
contracts. Unit-internal spilled values can reuse scratch storage once dead.

Materialization retains original assignments and creates reload/store instructions,
temporary value-source maps, frame slots, and a final verified spill-free problem.
The top-level pipeline attaches transfer instruction timing and runs final
physical-hazard scheduling. Original spill decisions stay inspectable separately.
External inputs, outputs, and explicit live-outs require calling-convention/
boundary transfers before spilling. Unsupported target transfer effects require
an operand/control adapter.
Insufficient scratch storage is an explicit allocation failure. A terminating
unit with values needing stores on outgoing paths requires a control-flow
transfer adapter.

## Textual IL and C API

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

`load_regtl` validates classes, constraints, storage, transfers and CFG references;
`print_regtl` emits canonical inspectable units. Unit live ranges are one explicit
problem; each function is analyzed as a separate CFG problem. Opaque C assignment
handles in `limestone/il.h` support both modes and every allocator. Their spills
are decisions; target-specific materialization belongs to the pipeline.

```sh
build/limestone-cli --allocate-il --allocator constraint tests/fixtures/allocation.regtl
```
