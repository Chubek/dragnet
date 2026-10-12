# STK-00 version 1

STK-00 is VMWeave's architecture-neutral, stack-effect-oriented interchange.
`stk.cpp` is its sole text reader/writer; backends consume the owning `Module`.
There was no existing STK-00 implementation in this checkout. This document
defines the initial format. Changes must update reader, writer, consumers,
examples, and tests together.

## Grammar

The prefix is mandatory and ordered. Subsequent declarations can be reordered;
printing canonicalizes fields/instructions/components by name. Rewrite order
preserves rule priority.

```text
module       = "stk-00" "1"
               "vm" string
               "configuration" execution cell stack-capacity frame-capacity
                   ipc-capacity allocator hooks
               "source" string line
               { declaration }
               "end" EOF
declaration  = "component" string
             | "field" string string count
             | "subsystem" string c-primitive
             | word | rewrite
word         = ":" string "opcode" integer
               "operands" operand-count { string }
               "effect" string "flow" string
               "source" string line
               c-primitive ";"
rewrite      = "rewrite" from-count { string } "to" to-count { string }
c-primitive  = "c{" byte-count LF exactly-byte-count-bytes LF "}c"
```

Execution/cell/allocator values are strings. A string is double quoted; backslash
quotes the following character. The canonical writer escapes quotes/backslashes
using C++ `std::quoted` rules; embedded newlines are literal, not JSON escapes.
Counts specify exactly how many items follow. Numbers are nonnegative decimal
integers; hooks is 0 or 1. Field count zero means scalar, otherwise fixed array.
No comments/unknown declarations are accepted. Input is bounded to 4 MiB;
embedded NUL, truncation, invalid counts, unsupported versions, and trailing
input are errors with STK source locations.

Semantic validation matches Lua: names, stack effects, flow, operand kinds,
capacities, component dependencies, and rewrite contracts. Source locations are
provenance independent of where the serialized module is saved.

## Words and foreign C

```text
stk-00 1
vm "Tiny"
configuration "switch" "intptr_t" 64 64 64 "custom" 0
source "tiny.lua" 1
component "dispatch"
component "insncode"
: "HALT" opcode 0 operands 0 effect "( -- )" flow "halt" source "tiny.lua" 5
c{ 10
vm_halt();
}c ;
end
```

The Forth-style `: ... ;` word defines an instruction, input/output stack contract,
operands, control flow, and authoritative semantic implementation. Stack effects
use `( before -- after )` with whitespace-separated values; token counts determine
checked requirements.

The foreign-C primitive is **length framed**. Its bytes can contain quotes,
braces, semicolons, `}c`, comments, and newlines without ambiguity and survive
serialization/lowering verbatim. It is an explicit semantic escape, not a guess
at how arbitrary C maps to stack arithmetic. VM stack/control/memory operations
use the API in [README.md](README.md). Subsystem extensions use this primitive too.

C lowering derives instruction implementations solely from these words. Component
directives select runtime facilities. Built-in native lowering creates a validated
MachineIR CFG with explicit foreign-runtime calls; the MachineIR C backend compiles
the authoritative handlers and subsystem bodies with the selected C toolchain.
Unsupported C or unresolved external functions are compiler/linker errors.
Custom MachineIR adapters must also preserve foreign-C semantics; no path may
assign meanings based on mnemonics. Native binding envelopes are defined in
`metacode/machine-ir/README.md`; they do not change STK-00 syntax.

Saving/reloading a canonical module produces identical C. Opcodes are already
assigned and are never renumbered when parsing STK-00.
