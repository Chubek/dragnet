# VMWeave -- AGENTS.md

VMWeave is Limestone's Lua DSL for describing virtual machines and generating C execution skeletons. It is intentionally a declarative frontend; generated code is expected to use Limestone memory-management and optimization facilities when those facilities are supplied by the embedding project.

## Source of truth

`vmweave.lua` is the DSL implementation. VM descriptions consist of:

- a VM name;
- named state fields;
- named instructions;
- optional instruction bodies;
- generation configuration supplied by the embedding application.

The public Lua API is:

```lua
local vmweave = require("vmweave")
local vm = vmweave.vm("MyVM")
vm:state("pc", { type = "u64" })
vm:instruction("add", { body = "..." })
local c_source = vm:emit()
```

## Generated-code rules

Generated C must be deterministic and self-contained as a skeleton. Do not emit hidden global mutable state. VM state belongs in an explicit VM structure. Instruction handlers must be ordinary functions so embedders can add dispatch strategies without rewriting the DSL.

Memory management and optimization hooks are extension points; do not hard-code a particular allocator or optimizer into the language frontend.

## Semantics

VMWeave describes execution state and instruction behavior. It does not define an ISA database, scheduler, register allocator, or binary encoder. Those concerns remain in Metacode and the corresponding Limestone subsystems.

## Errors

Invalid VM names or malformed declarations should fail during DSL construction rather than producing silently malformed C. Generated C should not claim safety properties that the input DSL did not specify.

## Determinism

Generated declarations and handlers must be emitted in a stable order. Do not use Lua table iteration order as an externally visible ordering guarantee; the implementation should sort names before emission when deterministic output matters.

## Testing

Changes should test:

1. loading the module;
2. VM construction;
3. state and instruction registration;
4. deterministic C generation;
5. invalid declarations;
6. generated C compilation where a C compiler is available.

## Development rule

Keep the DSL small and compositional. Add a new VM concept only when it cannot be expressed through existing state/instruction metadata or when a corresponding generated-runtime contract is being added.
