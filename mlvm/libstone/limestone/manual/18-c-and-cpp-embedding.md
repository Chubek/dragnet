# Chapter 18. C and C++ Embedding

[Previous: VM generation](17-vmweave-vm-generation.md) · [Contents](README.md) · [Next: Python](19-python-bindings.md)

## 18.1 Choosing the embedding boundary

Limestone exposes owning C++ models for direct construction and opaque C handles
for ABI-stable integration. Both interfaces consume the same semantic algorithms.
C++ is convenient for custom target adapters and structured IL manipulation; C is
convenient for language bindings, plugins, and hosts that cannot share C++ types.

Select a public library according to the workflow:

| Interface | Header | Exported target |
| --- | --- | --- |
| Full compiler | `limestone/limestone.hpp` or `.h` | `Limestone::core` |
| Standalone textual ILs | `limestone/il.h` | `Limestone::il` |
| Owning optimizer | `limestone/optimization.h` | `Limestone::optimization` |
| Standalone objects | `limestone/object.h` | `Limestone::object` |
| Translation runtime C API | `limestone/runtime.h` | `Limestone::core` |
| Component C++ APIs | Component `.hpp` | Corresponding component target |
| Host FFI | `exolayer/exolayer.h` | `Limestone::exolayer` |

Attaching an optimizer to a pipeline target and packaging a compiler module as an
object use core entry points in addition to their standalone libraries. Use the
exported CMake targets so static implementation dependencies remain correct.

## 18.2 Structured C++ results

`Result<T>` owns either a value or `Error`. Test its Boolean state before accessing
`value()` or `error()`. Incorrect accessor use raises a logic error rather than
inventing a default result.

Error categories include invalid argument, parse, not found, conflict, unsupported,
unsatisfiable, internal, timeout, interrupted, and resource limit. Preserve these
categories when passing an error through a host adapter. Their meanings help
distinguish malformed input, absent contracts, impossible constraints, and a
bounded algorithm that did not finish.

```cpp
auto result = limestone::run_pipeline("(add 20 22)");
if (!result) {
  std::cerr << result.error().message << '\n';
  return 1;
}
std::cout << result.value().machine_ir;
```

The orchestration boundary contains exceptions from host optimizer/grouping/
allocation/backend callbacks and adds active-stage context. Explicit Limestone
error codes are retained. Vendor implementation types are not part of the public
result model.

## 18.3 A complete explicit-target C++ example

```cpp
#include <limestone/limestone.hpp>
#include <metacode/metacode.hpp>
#include <iostream>

int main() {
  auto metadata = limestone::metacode::load_isa_file(
      "tests/fixtures/native-constant.isa");
  if (!metadata) {
    std::cerr << metadata.error().message << '\n';
    return 1;
  }
  auto target = limestone::make_target(metadata.value());
  if (!target) {
    std::cerr << target.error().message << '\n';
    return 1;
  }
  limestone::PipelineOptions options;
  options.selector = limestone::SelectionStrategy::BURS;
  options.allocator = limestone::AllocationStrategy::GraphColoring;
  options.allocate = true;
  options.encode = true;
  auto module = limestone::run_pipeline("(add 20 22)", target.value(), options);
  if (!module) {
    std::cerr << module.error().message << '\n';
    return 1;
  }
  std::cout << module.value().machine_ir;
  std::cout << "encoded bytes: " << module.value().encoded->bytes.size() << '\n';
}
```

This creates bytes under an explicit bounded native contract. The program prints
inspection data; mapping and invocation are separate host responsibilities. For
a direct graph, call the `unisel::Program` overload instead of the source overload.

## 18.4 Pipeline options and target adapters

`PipelineOptions` defaults to optimization and scheduling enabled, allocation and
encoding disabled, global selection, linear-scan allocation, and trace execution
disabled.

`PipelineTarget` owns patterns, instruction models, scheduling model, classes,
aliases, value-class constraints, optional BURS rules, spill classes, metadata,
and explicit optimizer/grouping/allocation/backend adapters. `make_target` builds
it from normalized UMD or Metacode data.

Skipping a stage does not manufacture its target facts. Even without scheduling,
every selected opcode needs a model that retains architectural state and operand
constraints. Encoding needs its backend and complete final operands/allocation.
An optimizer callback is a semantically checked IL adapter, not a generic
permission to mutate source effects.

Chapter 21 describes adapter construction in detail. Component libraries remain
usable independently when a host wants to orchestrate its own sequence.

## 18.5 Inspecting the returned module

`Module` retains target/module identity, the graph passed to selection, selected
region, schedule, allocation problem, original allocation/spill decisions, stage
names, final order, optional execution trace, machine listing/exchange, optional
encoded output, and optional materialized region.

When spills exist, encoding/exchange use `materialized`, which owns temporary
assignments, transfer instructions, frame slots, and final schedule. Without
spills, final physical hazards are reflected in selected/scheduled data.

Do not derive final emission order from unordered assignment maps. Use
`Module::order`. Keep original spill decisions separate from final temporary
register inspection. This avoids presenting a reloaded value as if it never
spilled.

## 18.6 A complete minimal C compiler consumer

```c
#include <limestone/limestone.h>
#include <stdio.h>

int main(void) {
  limestone_error error;
  limestone_module *module =
      limestone_compile_checked("(add 20 22)", NULL, &error);
  if (!module) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  puts(limestone_module_text(module));
  limestone_module_destroy(module);
  return 0;
}
```

The checked compile accepts NULL options and diagnostic pointers. The returned
module is caller-owned; returned text/exchange/bytes are borrowed from it until
destruction. C++ exceptions do not cross the ABI.

The simple unchecked `limestone_compile` remains available, but the checked API
is more useful when a host needs diagnostics. Destroy functions accept NULL for
ordinary optional-handle cleanup.

## 18.7 Extended C configuration

The legacy `limestone_options` structure contains optimization, scheduling, and
allocation fields. Extended algorithms, encoding, and tracing use an opaque
`limestone_configuration`, preserving that existing structure's ABI.

```c
limestone_configuration *configuration = limestone_configuration_create();
if (!configuration) return 1;
limestone_status status = limestone_configuration_set_pipeline(
    configuration, 1, 1, 1, 1, 0, &error);
if (status == LIMESTONE_OK) {
  status = limestone_configuration_set_algorithms(configuration,
      LIMESTONE_SELECT_BURS, LIMESTONE_ALLOCATE_COLOR, &error);
}
if (status != LIMESTONE_OK) {
  limestone_configuration_destroy(configuration);
  return 1;
}
/* target is an independently loaded explicit target handle. */
limestone_module *module = limestone_compile_target(
    "(add 20 22)", target, configuration, &error);
limestone_configuration_destroy(configuration);
```

This fragment assumes `error` and `target` from the embedding function. Source,
target, and configuration are borrowed during the synchronous call. A successful
module owns its result independently and may outlive them.

`limestone_target_load` consumes UMD text; `_load_file` supports bounded relative
UMD includes. `limestone_target_load_isa` consumes ISA text, not a pathname. Load
the file into host-owned text first when using that C entry point.

## 18.8 Constructing graphs through C

`limestone_program_create` returns an independent mutable graph handle. Add nodes,
outputs, dependencies, properties, memory access, blocks, entry, and control
targets through checked setters. Input arrays/strings are copied by mutation.

```c
limestone_program *program = limestone_program_create();
if (!program) return 1;
uint32_t inputs[] = {1, 2};
limestone_status status = limestone_program_add_node(
    program, 1, "const", "i64", NULL, 0, 1, 20, 1, 1, &error);
if (status == LIMESTONE_OK) status = limestone_program_add_node(
    program, 2, "const", "i64", NULL, 0, 1, 22, 1, 1, &error);
if (status == LIMESTONE_OK) status = limestone_program_add_node(
    program, 3, "add", "i64", inputs, 2, 0, 0, 1, 1, &error);
if (status == LIMESTONE_OK) status =
    limestone_program_add_output(program, 3, &error);
```

The graph can then be compiled with a compatible target through
`limestone_compile_program_configured`. Check each mutation and release the graph
on every exit path. Boolean fields accept 0/1; memory alignment is unknown zero
or a power of two. Blocks are declared in layout order with entry first, and
copied successor references may name later declarations.

## 18.9 Owning snapshots and borrowed views

| Owner | Independent result lifetime |
| --- | --- |
| IL document | Models/selections/schedules/assignments can outlive it |
| Selection model | Selection can outlive it |
| Optimizer | Optimization results and attached snapshots can outlive it |
| Program/target/configuration | Compiled module can outlive them |
| Object inputs | Serialized objects and linked images can outlive them |
| Runtime | Region bytes can outlive it, while validity expires |
| Native type handle | Parent types/registrations retain independent snapshots |

Borrowed views do not independently extend these owners. Copy strings, byte spans,
and metadata fields when storing them outside the handle lifetime. Object mutation
can invalidate inspection views even before destruction.

Host callback ownership has interface-specific rules: optimizer releases adopt
userdata only on successful registration; runtime installer executable records
are adopted on every outcome; Exolayer callback userdata remains registrant-owned.
Do not apply one convention to all callbacks merely because they use `void *`.

## 18.10 RAII for C handles

C++ applications can wrap opaque C owners with `std::unique_ptr` and their public
destroy function:

```cpp
using ModuleHandle = std::unique_ptr<limestone_module,
                                   decltype(&limestone_module_destroy)>;
limestone_error error;
ModuleHandle module(
    limestone_compile_checked("42", nullptr, &error),
    &limestone_module_destroy);
```

This fragment assumes `<memory>` and the C header. The wrapper owns exactly one
handle and does not change algorithm semantics. Do not manually destroy a pointer
still owned by that wrapper.

Copyable C++ target/session snapshots can retain callbacks. Ensure nonthrowing
releases and code-library lifetime remain valid through the final snapshot.

## 18.11 Threading and active-call lifetime

Independent invocations and handles avoid mutable global state. Shared mutable
programs, targets, optimizer sessions, object builders, runtimes, contexts, and
handle destruction require host synchronization. Read the component's callback
reentrancy contract before acquiring application locks around a synchronous call.

An owning result protects its data, but it does not authorize destroying an active
runtime or context. Active runtime executable owners survive self-invalidation;
that guarantee is different from active runtime object lifetime.

## 18.12 Installation and ABI validation

Use `find_package(Limestone CONFIG REQUIRED)` and exported namespaced targets.
Public includes mirror the source component paths. C consumers of static C++
implementations need an appropriate final C++ link driver/runtime.

The repository checks public C symbols, C/C++ header use, multiple translation
units, standalone consumers, and relocated installations. Mirror those boundaries
in an embedding project: build without source-private include paths, exercise
error cleanup, and verify that retained results survive intended source-handle
destruction.

[Previous: VM generation](17-vmweave-vm-generation.md) · [Contents](README.md) · [Next: Python](19-python-bindings.md)
