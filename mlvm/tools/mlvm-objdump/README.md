# mlvm-objdump

Library-backed ELF32/64 and archive inspection. View flags select actual output:
`-f` identity, `--section-headers`, `-s` bytes, `-t` symbols, `-r` relocations,
`-d` disassembly; repeatable `-j NAME` filters section-oriented views. Without view
flags, prints the comprehensive object report. NOBITS is reported as zero-fill.

Disassembly uses the documented x86-64 machine decoder; other identities fail for
`-d`, and unknown/truncated x86 instructions print `.byte`. Container loading uses
Limestone, and option parsing uses vendored klib. See [TOOLS.md](../../docs/TOOLS.md).
