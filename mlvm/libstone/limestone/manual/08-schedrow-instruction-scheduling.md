# Chapter 8. Schedrow Instruction Scheduling

[Previous: Limeburg](07-limeburg-burs-and-generated-specifications.md) · [Contents](README.md) · [Next: Allocation](09-regtl-allocation-and-spilling.md)

## 8.1 Scheduling consumes selected operations

Schedrow answers when selected target operations can issue and which resources
they occupy. Its IL separates instruction/effect structure from the machine
resource model and scheduling algorithms. It does not select instructions or
assign physical registers.

The principal types and functions are in
[schedrow/schedrow.hpp](../schedrow/schedrow.hpp), exported by `Limestone::schedrow`.
Text loading is a separate `Limestone::schedrow_text` library. The standalone C
document/result interface lives in `limestone/il.h`.

An instruction has stable identity, definitions/uses, implicit architectural
effects, timing, resources, optional memory access, control classification,
block membership, issue slots, priorities, pressure metadata, and provenance.
A region adds explicit dependencies, CFG blocks, entry, and instruction groups.
A machine model adds capacities, issue width, aliases, and priority policy.

## 8.2 The dependency graph

Dependency kinds are true, anti, output, memory, control, and ordering. An edge
records producer/consumer instruction IDs, latency, iteration distance, and
whether it is scheduler-only.

True dependencies preserve value availability. Anti and output dependencies
preserve storage ordering. Memory edges preserve aliasing and ordering obligations.
Control and ordering edges retain barriers, calls, traps, terminators, and explicit
source constraints. `dependencies` augments the explicit region with the hazards
implied by virtual and architectural state.

`scheduler_only=true` marks artificial scheduler restrictions separately from
semantic edges. This distinction matters when an optimizer reconstructs a graph:
a source dependency cannot be discarded merely because a scheduling heuristic
would prefer another order.

A zero-latency dependency still requires sequential producer-before-consumer
order. Co-issuing operations in the same cycle does not permit reversing a value
definition and its use in the emitted vector.

## 8.3 Latency has multiple identity domains

An instruction's default `latency` governs ordinary result availability.
`result_latency` overrides individual SSA value results. Architectural state uses
the separate `implicit_result_latency` map.

Suppose an operation produces two SSA values with availability after one and
four cycles, and writes an architectural flag after two cycles. The result maps
can represent those differences without changing the default for unrelated
results. Numeric ID coincidence between a value and a flag does not connect
their maps.

At target ingress, definition-result latency keys are selected definition operand
indexes. The pipeline resolves them to SSA IDs after matching. Architectural
latency keys remain architectural identities. Use the appropriate layer's
index domain when constructing metadata.

Throughput is another property. It is retained as metadata, not converted into
an implicit hard issue-rate limit. Use resources, reservations, issue width,
and slots to express hard machine capacity constraints.

## 8.4 Resource reservations

`ResourceUse` describes a resource, duration, quantity, start offset, and optional
alternatives. This supports fractional resource consumption and multi-cycle
occupancy. The machine model declares capacities by resource name.

A reservation starting at issue cycle `c`, offset `o`, and duration `d` consumes
its quantity during the declared occupancy window beginning at `c + o`. A resource
latency need not equal the value-result latency. A multiply may occupy a pipeline
briefly while its result becomes available several cycles later.

Alternative resources require a joint legal choice when instructions issue
together. Assigning every operation its first preferred port independently can
create a false conflict or miss a feasible bundle. The scheduler and verifier
retain the chosen resource names in `Scheduled::resources`.

Issue width limits simultaneous issue. An instruction's allowed slots refine
which issue positions it can use. A bundle may add group-wide slot and width
constraints. Resource and slot feasibility are solved together for such units.

## 8.5 A textual scheduling example

```text
machine_model fixture {
  issue { width = 2; }
  resources { ALU = 1; LOAD = 1; }
  timing {
    add {
      latency = 3;
      resources = [{ resource = ALU; duration = 2; }];
    }
  }
}
region test {
  instruction %1 { opcode = add; use %input; def %value; }
  instruction %2 {
    opcode = load;
    latency = 1;
    use %value;
    def %loaded;
    resources = [{ resource = LOAD; }];
    memory { read = true; address_space = heap; size = 8; alignment = 8; }
  }
  instruction %3 {
    opcode = ret;
    latency = 0;
    use %loaded;
    control_flow = return;
  }
}
```

Every instruction needs explicit latency or a matching opcode timing entry. The
loader retains metadata and stable identity domains. It rejects richer timing or
motion syntax for which the semantic adapter cannot establish a model, even if a
generic attribute could be parsed.

Run the complete repository example:

```sh
build/limestone-cli --schedule-il tests/fixtures/scheduling.schedrow
```

Its per-result override allows a use before the instruction's unrelated default
latency would suggest. Inspect both the dependency graph and issue assignments
to understand that behavior.

## 8.6 Memory, traps, and motion

`MemoryAccess` records read/write, volatile access, atomicity, memory ordering,
address space, alias sets, size, and alignment. Ordering classes include relaxed,
acquire, release, acquire-release, and sequential. Unknown alias information is
conservative; explicitly disjoint sets and address spaces can establish separation.

An atomic memory operation is a memory semantic property. An `atomic` scheduling
group is an indivisible scheduling unit. Neither implies the other.

Calls and barriers retain their ordering role. Potential traps and the
`speculative` restriction govern movement relative to observable effects.
Priorities and pressure deltas affect preference; they cannot relax a source
memory or trap constraint. Target attachment must preserve the source's more
restrictive semantics.

Operand-to-operand or ranged latency, richer speculation classifications,
register banks, and broader motion models need adapters. The current supported
flags and resource contracts are checked rather than guessed from instruction
classes.

## 8.7 List scheduling and priority

Given a host region and machine model:

```cpp
auto scheduled = limestone::schedrow::schedule(region, model);
if (!scheduled) return report(scheduled.error());
auto valid = limestone::schedrow::verify(region, model, scheduled.value());
if (!valid) return report(valid.error());
```

The scheduler chooses ready legal operations subject to dependency availability,
capacity, slots, grouping, and deterministic priorities. Explicit instruction
priority and optional critical-path priority can guide decisions. Pressure deltas
retain target-provided class information, but are not a substitute for exact
allocation liveness.

`Scheduled` stores the instruction ID, cycle, optional slot, and chosen resources.
The vector also provides sequential ordering information. Re-sorting it solely
by cycle or opcode can destroy zero-latency dependencies or group order.

## 8.8 Groups, bundles, pairs, and fusion

Groups contain stable IDs and member IDs in emission order:

| Kind | Required contract |
| --- | --- |
| `ordered` | Members appear in listed order; other operations may intervene |
| `adjacent` | Members are contiguous and ordered |
| `atomic` | Members form an indivisible contiguous scheduling unit |
| `same_cycle` | Members are ordered and share a cycle |
| `bundle` | Members share a cycle and are contiguous, with optional width/slots |
| `fusion` | Members are contiguous; a pattern and benefit hint are retained |
| `pair` | Exactly two members, adjacent and ordered |

All groups stay in one block. Non-ordered units have disjoint membership; ordered
groups can overlap them. Fusion benefit changes priority while preserving timing,
resources, and semantics. A pattern name documents the target opportunity.

Same-cycle members may have intervening zero-latency dependency paths. The
scheduler must co-issue that path legally or reject the group if positive latency,
capacity, slots, or adjacency prevents it. Treating group members independently
would miss this obligation.

```sh
build/limestone-cli --schedule-il tests/fixtures/grouping.schedrow
```

The pipeline's grouping adapter runs after selection, when target instruction
identities exist. Spill materialization protects groups by placing required
reloads before a unit and stores after it, subject to scratch and control legality.

## 8.9 CFG scheduling and layout

CFG scheduling handles each block independently. Cycles and resources are local
to a block, so two different blocks may both start at cycle zero. The issue vector
still follows declared layout with entry first and conditional target `1` as
fallthrough.

Cross-block motion requires a legal region adapter. Block-local scheduling does
not turn a CFG into one giant acyclic instruction list or consume successor edges
as ordinary value dependencies.

`schedule_cfg`, `verify_cfg`, and `block_region` support explicit CFG workflows.
Sequential verification rejects interleaved or reordered blocks, contradictory
terminators/targets, and dependence violations even when each block's individual
cycle assignment seems plausible.

## 8.10 Modulo scheduling

`schedule_modulo(region,model,options)` searches a bounded periodic schedule at an
explicit initiation interval. A dependency with iteration distance `d` retains
its loop-carried latency obligation across iterations. The conceptual constraint
is that the consumer's periodic issue time plus `d * II` respects the producer's
availability.

The machine's periodic resource and slot usage must also fit. `verify_modulo`
checks those constraints, groups, and intra-iteration order. `ModuloOptions`
provides initiation interval, cycle bound, and search limit.

```sh
build/limestone-cli --schedule-il --modulo 2 loop.schedrow
```

This command assumes a caller-provided loop document with valid distance edges.
A modulo result is an iteration schedule. Executable sequential output requires
loop expansion with prologue/kernel/epilogue and value handling supplied by an
adapter. Passing the periodic vector directly to a normal encoder is not that
lowering.

## 8.11 Verification after allocation

`validate_region` checks structure and effects without timing. `verify_order`
checks sequential emission and hazards. `verify` checks timing, resources, slots,
groups, and issue-vector order. These are complementary checks.

After allocation, `regtl::allocated_dependencies` adds physical-storage hazards
without replacing semantic operands. The pipeline reschedules the resulting
region, including spill transfers when present. This closes the gap between SSA
independence and physical register reuse.

When debugging, retain the original region, augmented dependencies, machine model,
group definitions, and final issue vector. A useful test asserts availability,
resource assignments, layout, and final order, then independently verifies them.
Chapter 9 explains the storage decisions that trigger the final scheduling step.

[Previous: Limeburg](07-limeburg-burs-and-generated-specifications.md) · [Contents](README.md) · [Next: Allocation](09-regtl-allocation-and-spilling.md)
