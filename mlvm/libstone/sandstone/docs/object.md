# Output formats

`sandstone-as -f bin` (default) writes a flat image; `-f obj` writes a
JSON object file (`sandstone-obj-1`) carrying everything a linker needs.

## `bin`: flat image

Alloc sections are concatenated at their assigned bases (`.bss`
contributes zero bytes). Every relocation must resolve to a locally
defined label; otherwise assembly fails with a diagnostic naming the
unresolved relocation count. Use for bare-metal images, shellcode, and
tests.

## `obj`: JSON object (`sandstone-obj-1`)

```jsonc
{
  "format": "sandstone-obj-1",
  "sections": [
    {"name": ".text", "base": 0, "bss": false, "data": "89d890..."}
  ],
  "symbols": [
    {"name": "_start", "section": ".text", "address": 0,
     "global": true, "defined": true},
    {"name": "puts", "section": "", "address": 0,
     "global": false, "defined": false}
  ],
  "relocs": [
    {"section": ".text", "offset": 4, "symbol": "target",
     "addend": 0, "type": "R_RISCV_CALL", "width": 32, "pcrel": true}
  ]
}
```

- `sections[].data` is lowercase hex; `base` is the section's load
  address; `bss` sections carry zero bytes of `data` length (their size);
  consumers may treat them as NOBITS.
- `relocs[].offset` is the byte offset of the field start within the
  section; whole-instruction fields are normalized to the trailing
  `width` bits (see `docs/assembler.md`). `type` is the bank relocation
  name (`ABS` for absolute directive fixups).
- Absolute symbols (`.equ` constants) use section `"*abs*"`.

The format is JSON precisely so linkers, loaders, and test harnesses can
consume it without a binary parser. It is versioned by the `format`
string; readers must reject unknown versions.
