# 31. Static and relocatable linking

[Contents](README.md) · [Next: Archives](32-archives.md)

The baseline linker combines x86-64 ELF64 objects into a relocatable object or a
static Linux executable. These modes remain available with library-backed tools;
the broader metadata-driven raw-image mode is a separate path.

```sh
build/mlvm-ld -r build/start.o build/answer.o -o build/combined.o
build/mlvm-ld build/start.o build/answer.o -o build/answer
```

These inputs are produced in chapter 38. Relocatable linking combines matching
sections with alignment, preserves local symbols and relocation records, resolves
global strong/weak definitions, and diagnoses duplicate strong definitions.

## Executable layout

Final linking defaults to entry symbol `_start`; `-e SYMBOL` overrides it.
The entry must identify an instruction in an executable section. Nonempty
allocated sections receive page-aligned load segments starting at base
`0x400000`, with permissions reflecting their section flags.

Unresolved nonweak symbols fail the link. Undefined weak symbols resolve to
zero. Relocation arithmetic checks signed PC32 range and absolute
addition/subtraction overflow; successful symbol lookup alone does not guarantee
that an encoding fits.

## Archive ordering

Static and `-r` linking process archives in command-line order. At each archive,
the linker repeatedly extracts members defining currently unresolved strong
symbols. It does not implicitly rescan an earlier archive after later inputs
create new unresolved references.

Consequently, place an object that calls `answer` before the archive defining
`answer`. This policy differs from the fixed-point archive processing used by
the raw-image mode in chapter 35.

## Scope

The baseline path does not implement dynamic linking, shared output, linker
scripts, archive groups, common symbols, or garbage collection of sections.
It produces an executable container without supplying a hosted runtime. The
example startup sequence performs a Linux exit syscall directly.

Reference: [static linker contract](../docs/TOOLS.md#linker-and-archive).
