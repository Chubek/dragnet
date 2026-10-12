 # AGENTS.md

## Project Overview

Schedrow is an instruction-scheduling intermediate language and subsystem of Limestone, a late-stage compiler framework.

Schedrow sits between instruction selection and final machine scheduling, register allocation, and encoding:

```text
High-level IR
      |
      v
Instruction Selection
      |
      v
Scheduling IL
      |
      +-- SSA/data dependencies
      +-- machine resources
      +-- latency and throughput
      +-- register pressure
      +-- memory and ordering constraints
      +-- speculation and motion rules
      +-- scheduling regions
      +-- fusion and bundling
      |
      v
List / DAG / MI Scheduling
      |
      v
Register Allocation
      |
      v
Machine Encoding
```

Schedrow provides both C and C++ APIs. Its input and metadata are parsed using:

```text
scripts/third_party/dparser
```

The authoritative source for microprocessor and microarchitectural information is Metacode's infobank. The attached `schema.json` defines the structure of the ISA description bundles consumed by that ecosystem.

## Source Of Truth

Use the following precedence when resolving information:

1. Explicit Schedrow and Limestone source code behavior.
2. Metacode infobank data.
3. The schema represented by `schema.json`.
4. Tests and existing examples.
5. Comments and documentation.
6. General architectural assumptions.

Do not duplicate processor timing or resource data inside Schedrow when it belongs in the Metacode infobank or its generated metadata.

The schema identifies itself as JSON Schema draft 2020-12 in `schema.json` line 2. Its root format is fixed to `isa-description-bundle` at line 15, and its semantic representation is fixed to `s-expression` at line 24.

## Repository Navigation

Important areas include:

```text
scripts/third_party/dparser    Input parser and metadata ingestion
Schedrow/             Scheduling IL and scheduler implementation
include/              Public C and C++ API headers
src/                  Implementation sources
tests/                Parser, IL, scheduling, and integration tests
docs/                 User and developer documentation
```

Use the repository's existing directory layout when present. Do not introduce new top-level directories without a clear ownership boundary.

## Editing Guidelines

Keep changes narrowly scoped to the requested behavior.

Before modifying code:

1. Locate the relevant parser, IL type, or scheduling component.
2. Read nearby code and tests.
3. Identify whether the behavior is semantic, scheduling-specific, or machine-model-specific.
4. Check whether the required information already exists in the infobank schema or generated metadata.
5. Preserve existing C and C++ API conventions.

Prefer existing abstractions over adding parallel representations. Avoid making the scheduler rediscover properties that can be represented explicitly in the Scheduling IL.

Use ASCII for new source files unless the surrounding file already uses another character set. Add comments only where they clarify non-obvious invariants or scheduling legality.

## Scheduling IL Model

Schedrow instructions should preserve a stable distinction between semantic information and scheduling information.

### Instruction Identity

Each instruction should have stable identity and classification:

```text
instruction {
    id
    opcode
    opcode_class
    semantic_class
    encoding
    flags
}
```

Semantic classes may include:

```text
integer
floating_point
vector
memory
branch
call
return
conversion
compare
barrier
pseudo
```

Do not use opcode-name string matching when an instruction class, semantic class, or explicit attribute is available.

### Dependencies

Represent semantic dataflow dependencies separately from scheduler-created dependencies.

Supported dependency categories include:

```text
true
anti
output
memory
control
ordering
```

A dependency may carry:

```text
producer
consumer
kind
latency
distance
```

Loop-carried dependencies must preserve their iteration distance. Do not collapse a loop-carried edge into an ordinary intra-iteration dependency.

### Timing

Latency and throughput are independent properties.

Latency may be result-, predicate-, memory-, or operand-specific. Avoid reducing a richer latency model to one integer unless the target model explicitly requires it.

Throughput may be represented as a cycle count, issue rate, or equivalent machine-model quantity. Keep the original precision when importing data from Metacode.

### Resources and Reservations

Resource usage should support:

- Alternative execution resources.
- Resource quantities.
- Multi-cycle occupancy.
- Port and issue-slot constraints.
- Pipeline reservations.

Machine-specific resource and reservation data belongs to the microarchitectural model. The instruction's semantic description should not be polluted with data that changes between processor generations.

### Registers and Pressure

Represent register operands, register classes, fixed registers, tied operands, early clobbers, late definitions, and register banks explicitly.

Register-pressure metadata should distinguish pressure classes such as:

```text
GPR
FPR
VR
predicate
```

Pressure effects are scheduling information and must remain distinct from the semantic register definitions and uses.

### Memory

Memory instructions should expose:

```text
operation
address_space
size
alignment
alias_class
alias_set
may_read
may_write
volatile
atomic
ordered
```

Memory ordering must be represented explicitly:

```text
relaxed
acquire
release
acq_rel
seq_cst
```

Do not add conservative ordering merely because alias information was not queried. Preserve alias classes and sets supplied by the machine or compiler model.

### Control Flow and Effects

Branches, calls, returns, barriers, and terminators must expose their scheduling behavior.

Relevant properties include:

```text
terminator
may_fallthrough
is_return
is_call
is_indirect
has_delay_slot
delay_slots
```

Side effects should distinguish:

```text
reads_flags
writes_flags
reads_memory
writes_memory
may_trap
may_throw
has_io
```

Flags and condition codes are schedulable dependencies. Model individual flag definitions and uses where the target requires it; do not represent all flags as one undifferentiated effect.

### Speculation and Motion

Speculation legality is distinct from ordinary motion preference.

Use explicit speculation levels where supported:

```text
none
control
data
memory
full
```

Motion metadata may describe whether an instruction can be:

```text
hoisted
sunk
moved_before
moved_after
```

Hard legality constraints must not be represented as heuristic hints.

### Grouping and Fusion

Instruction grouping supports:

```text
bundle
atomic
adjacent
same_cycle
ordered
fusion
pair
```

Fusion metadata should identify compatible patterns and any scheduling benefit. Preserve grouping constraints through scheduling and lower them only when the target-specific pipeline requires it.

### Scheduling Regions

Scheduling must operate on explicit regions where possible:

```text
basic_block
trace
hyperblock
superblock
loop
region
function
```

Do not assume that a CFG basic block is always the complete scheduling scope.

### Costs and Hints

Keep independent objective components separate:

```text
latency
throughput
size
energy
register_pressure
spill_risk
```

Distinguish hard constraints from non-semantic scheduling hints such as:

```text
prefer_early
prefer_late
critical
noncritical
priority
```

Frequency, branch probability, loop depth, trip count, unroll factor, and loop-carried dependency distance should be preserved when available.

## Metacode Infobank Schema

The root object in `schema.json` requires:

```text
format
version
grammar
semantic_format
architectures
```

These required root properties are specified in lines 7-11.

The `format` property must be:

```text
isa-description-bundle
```

as specified at line 15.

The `semantic_format` property must be:

```text
s-expression
```

as specified at line 24.

The `architectures` collection contains architecture descriptions. Each architecture entry includes fields such as:

```text
file
arch
family
model
version
instruction_count
register_classes
semantics
compiler
```

These fields are defined in the architecture section at lines 30-40. `instruction_count` is a non-negative integer at line 57. `register_classes` is an array of strings at line 61. `semantics` is a required object with `format`, `field`, and `count` at line 67. `compiler` is an array of strings at line 75.

The schema also defines architecture-level and instruction-level tooling metadata:

```text
tooling
op_tooling
```

These appear at lines 81 and 85. Use these fields for Schedrow-specific metadata only when the metadata is genuinely tooling-related and consistent with the infobank contract.

The schema defines `tooling_schema_version` as the constant integer `1` at line 94.

The schema contains primarily ISA-description structure. It does not directly define the complete uArch scheduling model for latency, throughput, resources, reservations, issue slots, or pipeline occupancy. Those properties should be obtained from the Metacode infobank's tooling metadata or associated machine-model data rather than inferred from ISA fields.

## Parser Requirements

Changes involving infobank input must account for `scripts/third_party/dparser`.

When changing parsing behavior:

- Preserve compatibility with existing bundle versions unless the change is intentionally breaking.
- Validate required root fields before consuming architecture entries.
- Validate fixed values such as `format`, `semantic_format`, and `tooling_schema_version`.
- Produce diagnostics that identify the input file, architecture, instruction, and field where possible.
- Do not silently discard unknown scheduling metadata.
- Keep parsing and scheduling semantics separate: parsing should preserve source information, while the scheduler should interpret it.

If a new field is required by Schedrow, first determine whether it belongs in the schema, in `tooling`, in `op_tooling`, or in a separate machine scheduling model.

## C API

The C API must remain usable without C++ language features.

When adding public C types:

- Use stable, explicit ownership rules.
- Document lifetime and mutation behavior.
- Avoid exposing private C++ implementation details.
- Preserve ABI-sensitive layout unless a deliberate ABI change is required.
- Use opaque handles when the implementation requires internal ownership or polymorphism.

Return errors consistently with the surrounding API. Do not convert malformed machine metadata into silently degraded scheduling behavior unless that behavior is already part of the API contract.

## C++ API

The C++ API may provide safer wrappers around the C implementation, but it must not change scheduling semantics.

Follow existing project conventions for:

- Ownership.
- Value versus reference semantics.
- Error propagation.
- Iterators and ranges.
- Namespace structure.
- Const-correctness.
- Exception policy.

Do not add a second source of truth for instruction properties in C++ wrappers.

## Testing

Tests should cover the narrowest relevant layer and then the integration path when behavior crosses boundaries.

At minimum, changes to parsing or metadata should test:

- Valid draft 2020-12 bundles.
- Missing required root fields.
- Invalid fixed values.
- Invalid architecture field types.
- Invalid `semantics` objects.
- Invalid tooling schema versions.
- Preservation of `tooling` and `op_tooling` metadata.

Changes to scheduling should test:

- True, anti, and output dependencies.
- Memory and ordering dependencies.
- Operand-specific latency.
- Loop-carried dependency distance.
- Resource alternatives and multi-cycle occupancy.
- Issue-slot restrictions.
- Register-pressure changes.
- Barriers and calls.
- Speculation legality.
- Fusion, grouping, and bundling.
- Hot and cold scheduling regions.

Tests must not rely only on instruction order. Assert the resulting dependency graph, resource assignments, legality decisions, and schedule where those are part of the behavior.

Run the repository's standard formatter, unit tests, parser tests, and integration tests relevant to the changed component. Report any unavailable or failing checks rather than treating them as successful.

## Change Checklist

Before submitting a change:

- Confirm whether the change belongs to Schedrow, Limestone, `scripts/third_party/dparser`, or the Metacode infobank model.
- Preserve the distinction between ISA semantics and uArch scheduling behavior.
- Check the schema requirements in `schema.json`.
- Keep semantic dependencies separate from scheduling-only dependencies.
- Preserve latency, throughput, resource, memory, register, and speculation metadata without lossy conversion.
- Add focused tests for malformed metadata and scheduling edge cases.
- Verify both the C and C++ APIs when public behavior changes.
- Avoid unrelated formatting, generated-file, or metadata changes.
- Document compatibility or versioning implications.

## Generated and External Data

Do not manually edit generated infobank data if it is regenerated from Metacode. Modify the authoritative source or generator input and regenerate according to repository instructions.

Do not check in local processor-specific experiments as production machine models. Keep experimental scheduling data clearly separated from authoritative Metacode data.

## Examples

 Below are illustrative Schedrow examples. The exact parser syntax may differ because the provided description defines the IL concepts but not a complete Schedrow grammar.

## 1. Integer dependency chain

```schedrow
region %bb0 {
    instruction %i0 {
        opcode = add
        opcode_class = integer_alu
        semantic_class = integer

        def %r1
        use %r2
        use %r3

        latency {
            result = 1
        }

        resources {
            any_of {
                ALU0
                ALU1
            }
        }
    }

    instruction %i1 {
        opcode = mul
        opcode_class = integer_mul
        semantic_class = integer

        def %r4
        use %r1
        use %r5

        latency {
            result = 3
        }

        resources {
            resource_use {
                resource = MUL
                amount = 1
                duration = 3
            }
        }
    }

    dependency {
        producer = %i0
        consumer = %i1
        kind = true
        latency = 1
    }
}
```

The `mul` instruction cannot consume `%r1` until `%i0` has produced it.

## 2. Independent instructions competing for resources

```schedrow
region %bb1 {
    instruction %i10 {
        opcode = add
        semantic_class = integer

        def %r10
        use %r11
        use %r12

        resources {
            any_of {
                ALU0
                ALU1
            }
        }

        latency = 1
    }

    instruction %i11 {
        opcode = sub
        semantic_class = integer

        def %r13
        use %r14
        use %r15

        resources {
            any_of {
                ALU0
                ALU1
            }
        }

        latency = 1
    }

    instruction %i12 {
        opcode = load
        semantic_class = memory

        def %r16
        memory {
            operation = load
            address_space = generic
            size = 8
            alignment = 8
            alias_class = stack
        }

        resources {
            resource_use {
                resource = load_port
                amount = 1
                duration = 1
            }
        }

        latency {
            result = 4
            memory = 5..20
        }
    }
}
```

A scheduler can issue `%i10` and `%i11` together if the target has two usable ALUs. `%i12` consumes a load-port resource independently of the ALU instructions.

## 3. Operand-specific latency

```schedrow
instruction %i20 {
    opcode = multiply
    semantic_class = integer

    def %r20
    use %r21
    use %r22

    latency {
        from = result
        to = result
        cycles = 3
    }

    latency {
        from = result
        to = branch_condition
        cycles = 1
    }

    resources {
        resource_use {
            resource = MUL
            amount = 1
            duration = 3
        }
    }
}
```

This models a multiply whose general result is available after three cycles, while a branch condition derived from the result may be usable after one cycle.

## 4. Memory aliasing and ordering

```schedrow
region %bb_loads {
    instruction %load_a {
        opcode = load
        semantic_class = memory

        def %r1
        address %p0

        memory {
            operation = load
            size = 8
            alignment = 8
            alias_set = 17
            may_read = true
            may_write = false
            ordering = relaxed
        }
    }

    instruction %load_b {
        opcode = load
        semantic_class = memory

        def %r2
        address %p1

        memory {
            operation = load
            size = 8
            alignment = 8
            alias_set = 23
            may_read = true
            may_write = false
            ordering = relaxed
        }
    }

    instruction %store_a {
        opcode = store
        semantic_class = memory

        use %r3
        address %p0

        memory {
            operation = store
            size = 8
            alignment = 8
            alias_set = 17
            may_read = false
            may_write = true
            ordering = release
        }
    }

    dependency {
        producer = %store_a
        consumer = %load_a
        kind = memory
    }
}
```

The two loads use different alias sets and may be reordered. The store and `%load_a` require a memory dependency because they refer to the same alias set.

## 5. Register pressure

```schedrow
instruction %i30 {
    opcode = vector_add
    semantic_class = vector

    def %v3
    use %v1
    use %v2

    register_class {
        %v1 = VR
        %v2 = VR
        %v3 = VR
    }

    pressure_delta {
        VR = 1
    }

    latency = 2
}

instruction %i31 {
    opcode = vector_store
    semantic_class = memory

    use %v3
    address %p4

    memory {
        operation = store
        size = 32
        alignment = 32
    }

    pressure_delta {
        VR = -1
    }
}

dependency {
    producer = %i30
    consumer = %i31
    kind = true
    latency = 2
}
```

The vector add increases live vector pressure, while the store releases `%v3`.

## 6. Flags and conditional branch

```schedrow
instruction %cmp {
    opcode = compare
    semantic_class = compare

    use %r1
    use %r2

    flags {
        defines = { ZF, SF, CF, OF }
    }

    latency {
        result = 1
        predicate = 1
    }
}

instruction %branch {
    opcode = branch_if_equal
    semantic_class = branch

    flags {
        uses = { ZF }
    }

    branch {
        conditional = true
        target = %bb_true
        may_fallthrough = true
        terminator = true
    }
}

dependency {
    producer = %cmp
    consumer = %branch
    kind = control
    latency = 1
}
```

The branch depends specifically on `ZF`, rather than on an opaque “flags” register.

## 7. Call and scheduling barrier

```schedrow
instruction %call {
    opcode = call
    semantic_class = call

    use %r0
    def %r0

    effects {
        reads_memory
        writes_memory
        may_throw
    }

    barrier {
        kind = call
    }

    motion {
        hoist = false
        sink = false

        crosses {
            memory_barrier = false
            call = false
        }
    }
}
```

Instructions with memory or externally visible effects should not be moved across this call unless the target-specific scheduler explicitly permits it.

## 8. Speculative load

```schedrow
instruction %spec_load {
    opcode = load
    semantic_class = memory

    def %r30
    address %p8

    memory {
        operation = load
        size = 8
        alignment = 8
        may_read = true
        may_write = false
        may_trap = true
    }

    speculation {
        level = control
        safe = false
    }

    motion {
        can_hoist = false
        can_sink = true
    }
}
```

This load may be moved later, but it cannot be hoisted across control flow because it may trap and is not safe for control speculation.

## 9. Instruction fusion

```schedrow
instruction %cmp_branch_cmp {
    opcode = compare
    semantic_class = compare

    use %r1
    use %r2

    flags {
        defines = { ZF }
    }

    fusion {
        pattern = [compare, branch_if_equal]
        kind = macro
        benefit = 1
    }
}

instruction %cmp_branch_jcc {
    opcode = branch_if_equal
    semantic_class = branch

    flags {
        uses = { ZF }
    }

    branch {
        conditional = true
        target = %bb_exit
    }

    fusion {
        with = compare
        kind = macro
        benefit = 1
    }

    group {
        id = 4
        kind = fusion
    }
}
```

The scheduler can preserve adjacency between the compare and branch to enable target-specific macro-fusion.

## 10. Loop-carried dependency

```schedrow
schedule_region %loop0 {
    kind = loop
    depth = 1
    trip_count = unknown
    unroll_factor = 4

    instruction %iv_next {
        opcode = add
        semantic_class = integer

        def %iv_next
        use %iv
        use %one

        latency = 1
    }

    instruction %load_iter {
        opcode = load
        semantic_class = memory

        def %r40
        address %iv

        memory {
            operation = load
            size = 4
            alignment = 4
            alias_class = array
        }

        latency {
            result = 4
            memory = 5..12
        }
    }

    dependency {
        producer = %iv_next
        consumer = %iv_next
        kind = true
        latency = 1
        distance = 1
    }

    dependency {
        producer = %iv_next
        consumer = %load_iter
        kind = true
        latency = 1
        distance = 1
    }
}
```

The `distance = 1` attribute indicates that the dependency crosses one loop iteration.

## 11. VLIW bundle

```schedrow
bundle %bundle0 {
    issue {
        width = 2
        slots = [0, 1]
    }

    instruction %alu_inst {
        opcode = add
        semantic_class = integer

        def %r50
        use %r51
        use %r52

        issue {
            slots = [0]
            width = 1
        }
    }

    instruction %mem_inst {
        opcode = load
        semantic_class = memory

        def %r53
        address %p12

        issue {
            slots = [1]
            width = 1
        }
    }
}
```

This bundle allows one integer operation and one memory operation in the same cycle, provided their dependencies and resources are available.

## 12. Metacode-backed machine model

The instruction description should remain separate from the processor-specific scheduling model:

```schedrow
instruction add {
    opcode = add
    semantic_class = integer

    operands {
        def = register
        use = register
        use = register
    }
}

machine_model "metacode:cpu-family-x:model-y" {
    issue {
        width = 4
    }

    resources {
        ALU0
        ALU1
        MUL
        load_port
        store_port
    }

    timing {
        add {
            latency = 1
            throughput = 1.0
            resources {
                any_of {
                    ALU0
                    ALU1
                }
            }
        }

        multiply {
            latency = 3
            throughput = 0.5
            resources {
                resource_use {
                    resource = MUL
                    duration = 3
                }
            }
        }
    }
}
```

The same Schedrow instruction definition can then be scheduled against different `machine_model` instances for different processor generations.


