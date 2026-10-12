# Limestone pipeline API

`limestone.hpp` coordinates the subsystem APIs. `run_pipeline(program, target,
options)` accepts an owning `unisel::Program` model and returns `Result<Module>`.
`make_target` accepts a normalized UMD or Metacode architecture. The orchestration
model contains adapter functions; algorithms and ISA facts live in their owning
components.

```cpp
#include <limestone/limestone.hpp>

auto target = limestone::make_target(architecture);
if (!target) return target.error();
limestone::PipelineOptions options;
options.selector = limestone::SelectionStrategy::BURS;
options.allocator = limestone::AllocationStrategy::GraphColoring;
options.allocate = true;
options.encode = true;
auto module = limestone::run_pipeline(program, target.value(), options);
```

The host supplies `architecture` and `program`. `tests/fixtures/backend-machine.isa`
is an explicit four-register bytecode/private-frame example; `tests/backend.cpp`
constructs graphs and independently executes their encodings.

UMD file inputs use `unisel::load_umd_file` and the corresponding
`limestone_target_load_file` / `limestone_compile_umd_file` C entry points. They
resolve relative includes with cumulative budgets and cycle diagnostics,
retaining source origins through the selection pipeline. Text-loading entry
points require an explicit C++ resolver to expand includes.

## Stages and inspection

The normal sequence is ingest, optional optimization, selection, optional
scheduling (with an optional target grouping stage), optional allocation, final allocated scheduling/spill materialization,
MachineIR, and optional encoding. `Module::stages` records stages actually run.
Skipping a stage does not create the missing target facts.
Every selected opcode needs a target instruction model even when scheduling is
disabled, so implicit architectural state and operand constraints are retained.
Host optimizer/grouping/allocation/backend exceptions return structured errors
with the active stage; explicit Limestone error codes remain intact.

`Module::optimized` retains the input to selection. `selected`, `scheduled`,
`allocation_problem`, and `allocation` retain inspectable stage data. With spills,
`materialized` owns the final region, order, spill-free temporary assignments,
schedule, value-source mapping, slots, and frame size. `Module::order` is always the
final emission order. Encoding and exchange use the materialized representation
when present. Without spills, `selected` and `scheduled` include final allocation
hazards. `machine_ir` is a diagnostic listing; `machine_ir_exchange` is the
validated C++/D contract.

Original spilled values have frame locations rather than final registers. The
C API distinguishes original spill decisions (`limestone_module_spill_*`) from
final assignment (`limestone_module_value_register`) and frame/slot inspection
(`limestone_module_frame_size`, `limestone_module_spill_slot`).

## Target contracts

Infobank conversion creates patterns only from
`tooling.instruction_selection.selection_tree`, not opcode names or generic
semantic classes. Scheduling requires explicit known latency for each selected
instruction. Target `result_latencies` keys are definition operand **indices**;
the pipeline resolves them to SSA IDs. `implicit_result_latencies` keys are
architectural register IDs and stay in a separate domain.

`priority` and signed `pressure_delta` entries belong to instruction scheduling
metadata and pass through selection, spill transfers and MachineIR exchange.
`speculative=false` restricts motion across trapping instructions; target
metadata cannot relax a source effect's restriction. Normalized target and
instruction models retain their owning original `metadata` for inspection.
Unknown fields in explicit scheduling, resource-reservation and memory-access
contracts produce source/target/instruction diagnostics; unrelated metadata is
preserved. The default register class must name a declared class.

Allocation requires classes, aliases, and a class for each materialized value,
through source/selected classes, `value_classes`, or an explicit default class.
`constraints` supplies fixed/allowed/forbidden assignments. Target operand ties
and early definitions refer to selected operand indices. `allocation_adapter`
can construct an alternative RegTL allocation problem.
`grouping_adapter` returns ordered instruction groups over selected identities.
The scheduler validates those constraints and retains them through final
allocation, spill transfers, encoding order and version-3 MachineIR exchange.

Spill metadata supplies class, private address space, size, alignment, scratch
registers, and load/store opcodes. These transfers also require instruction
models. Calling-convention/boundary transfers are separate adapters.

Fixed machine/ABI operands can be declared in `encoding_operands`:

```text
encoding_operands = {
  result = { kind = fixed_definition; index = 0; register = rax; };
  imm = { kind = immediate; index = 0; };
};
```

`fixed_definition` and `fixed_use` entries identify a selected operand and a
physical register name without naming encoded bits. Target ingress resolves the
register identity; RegTL merges the requirement with class/allowed/forbidden/call
constraints before any allocator runs, including custom allocation adapters.
Conflicting fixed uses/definitions require an explicit transfer adapter and fail.
The encoder independently verifies the resulting physical assignment.

Encoding requires a `backend` callable returning bytes and relocations. The
Metacode-backed adapter uses Bin2Bin codecs plus `encoding_operands` entries
mapping each encoded field to a definition, use, immediate, or block-target
operand index. It requires spill-free physical allocation for register fields.
`make_object` and Bin2Bin's object layer package encoded output and resolve
explicit target relocation fields. The host backend owns ABI lowering and named
external-reference generation; see [object contracts](../bin2bin/OBJECTS.md).

An optimizer callback consumes/returns `unisel::Program`. For equality saturation,
use `tunah::optimize_graph` from `Limestone::tunah_unisel` and provide concrete
operator types and semantic rules; see `tunah/README.md`.

## C embedding

```c
#include <limestone/limestone.h>
#include <stdio.h>

limestone_error error;
limestone_module *module = limestone_compile_checked("(add 20 22)", NULL, &error);
if (!module) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
}
puts(limestone_module_text(module));
limestone_module_destroy(module);
```

Graph construction uses `limestone_program_create`, node/output/block/effect
setters, and `limestone_compile_program_configured`. Load targets with
`limestone_target_load` or `limestone_target_load_isa`. An opaque
`limestone_configuration` selects algorithms and enables scheduling/allocation/
encoding/tracing without enlarging the legacy `limestone_options` structure.
`limestone_compile_target(source, target, configuration, error)` also compiles a
closed TraceML program through an explicit target. It preserves lazy lexical
evaluation, source diagnostics and frontend/lowering stages. The C++ equivalent
is `run_pipeline(source, target, options)`. Constant/return or recorded trace
operations must have legal target patterns; target properties remain explicit.
Compile results own their data and may outlive programs, targets, and configuration.
Create/load/compile calls return `NULL` on failure; checked setters return
`limestone_status`. A diagnostic pointer may be `NULL`.

`limestone/il.h` and `Limestone::il` supply independent textual graph-selection, BURS, scheduling,
and allocation handles. Every document/result has a destroy function, and results
may outlive their documents. Returned strings and byte spans remain borrowed until
their owning handle is destroyed. Binary architecture/buffer handles expose
translation and disassembly through the same error boundary.
Unisel models own their source/pattern snapshots and expose coverage candidates,
signed clauses and provenance before global/greedy selection. Empty coverage
clauses remain inspectable on an unsatisfiable model. Selection results outlive
models and provide a canonical Schedrow handoff.

`limestone/optimization.h` and `Limestone::optimization` supply independent Tunah
sessions and saturation results. Define operators and justified rules, configure
literal/operator costs and iteration/node/class/time limits, and inspect the
extracted expression, statistics, and source-located match trace. Host predicates
and cancellation callbacks have optional release hooks retained by owning
snapshots. Errors preserve callback status codes and bounded diagnostics.

`limestone_optimizer_define_graph_operator` declares a pure typed source opcode.
`limestone_target_set_optimizer` copies the full session and graph configuration
into an existing target, including costs, budgets, callbacks, and provenance.
The source optimizer can be mutated or destroyed immediately; the target runs
the typed graph adapter when pipeline optimization is enabled. Passing NULL
removes the attachment. See `tunah/README.md` for an embedding example.

`limestone_optimizer_binary_transform` snapshots a session into an immutable
Bin2Bin semantic transform, requiring a host legality proof callback and versioned
semantic context identity. Its rules/costs/budgets enter the cache identity.
`limestone_binary_translate_with_transform` and
`limestone_runtime_create_with_transform` use that same adapter for offline and
runtime translation. Runtime creation copies the transform, retaining callback
userdata until the last owning snapshot is released. Deadlines and cancellation
disable cached-byte and resident translation reuse, preserving observation heat
and independent retained views. Runtime destruction releases semantic callbacks
and installer/executable ownership while its destruction guard remains active.

`limestone/object.h` and `Limestone::object` expose standalone owning ELF64
builders/loaders, immutable serialized data and addressed linked images.
`limestone_module_object` packages an encoded compiler result using a copied
target contract and named relocations. Its object result outlives the module and
target. See `bin2bin/OBJECTS.md` for target metadata, symbol resolution and limits.

`limestone/runtime.h` exposes Bin2Bin's owning translation runtime through
`Limestone::core`. Source/target architectures and runtime options are copied.
Prepare explicit guest/target addresses, observe heat, and optionally install
code via `limestone_runtime_installer`. The installer supplies execution,
userdata, and release callbacks. Executable userdata is adopted on every outcome,
released once, and retained during active execution, including self-invalidation.
Regions own their bytes and may outlive the runtime; validity expires on
invalidation or runtime destruction. Byte views remain readable until region
destruction. See `bin2bin/README.md` for the runtime lifecycle.

## CLI

`--compile-umd` consumes a UMD document containing a graph. `--target-isa FILE`
supplies the target from an Infobank description, and `--encode --allocate`
requests bytes. `--object SYMBOL` packages those bytes as ELF64 using the target's
`tooling.object_file` contract. `--machineir-json` exports the region exchange.

TraceML also accepts `--target-isa`. The experimental native fixture implements a
System V x86-64 no-argument function returning an i64 signed-32-bit constant in
RAX; its encoding, fixed operands, ABI identity and ELF contract are all explicit.
All three selectors and four allocators are tested through callable encoded
functions, including lazy untaken overflow branches and signed boundary values.

```sh
printf '%s\n' '((lambda x (add x 2)) 40)' | limestone-cli \
  --target-isa tests/fixtures/native-constant.isa --allocate --encode \
  --object native_entry -o native.o
```

The native integration also links this emitted object with a C consumer and
executes result 42. This fixture is a bounded native contract; production targets
need their own complete legal instruction and ABI models.
`--selector global|greedy|burs` and
`--allocator linear|greedy|color|constraint` choose algorithms.

The default source frontend accepts closed TraceML programs. It emits portable
IR; executable compilation needs an explicit target/runtime lowering adapter.
