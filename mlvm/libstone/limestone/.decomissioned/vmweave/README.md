# VMWeave

VMWeave is a Lua DSL for declaring VM state, named instruction handlers, and
optional dispatch/runtime hooks. Generated C is deterministic and self-contained;
mutable state belongs to an explicit VM structure.

```lua
local vmweave = require("vmweave")
local vm = vmweave.vm("Counter")
vm:state("value", {type = "i64"})
vm:instruction("increment", {opcode = 1, body = "  vm->value += 1;"})
local c_source = vm:emit({dispatch = "switch", hooks = true})
```

State types are `u8/u16/u32/u64`, `i8/i16/i32/i64`, `f32/f64`, `ptr`, and `size`.
An optional positive `count` declares an array. Names, types, counts, opcodes,
metadata keys, and duplicates are validated during construction. Handler bodies
are caller-supplied C source.

`emit()` produces the state and ordinary handler functions. `dispatch = "switch"`
adds `Counter_init`, `Counter_step`, and named opcode macros. Explicit opcodes are
honored; remaining opcodes are assigned in sorted handler-name order. Unknown
opcodes return `-1` before hooks or handlers execute.

`hooks = true` adds userdata, a before hook returning a status, and an after hook.
A nonzero before status prevents execution. Embedders can observe hot regions,
call a Bin2Bin runtime, or integrate memory/optimization through these hooks. The
DSL does not choose an allocator or executable installer. Initialization zeroes
state and hooks; synchronization belongs to the embedder. CTest compiles and
executes generated C in addition to checking DSL behavior.
