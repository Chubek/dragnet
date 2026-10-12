# Assembler framework

The assembler (`assembler/`, library `sandstone_assembler`, CLI
`sandstone-as`) assembles any dialect a `.symas` bank describes. Nothing
about an instruction set is hard-coded: form resolution, operand checking,
and byte emission all go through the bank.

```sh
sandstone-as --bank x86.symas --syntax intel -o out.bin prog.s
sandstone-as --bank riscv64.symas -f obj -o prog.obj.json prog.s
```

## Pipeline

1. **Parse** (`parser.c`): each line is `[label:] (directive | mnemonic
   operands...)`. Comments follow the selected syntax record (a comment
   token starts a comment at line start, or when surrounded by
   whitespace, so `#4`-style immediates survive a `#` comment).
   Operands split on the syntax separator (whitespace when the separator
   is blank), aware of `[...]` memory expressions and quotes. AT&T-style
   syntaxes (`operand_order == 1`) have their operands un-reversed before
   encoding.
2. **Operand classification**: registers resolve through the bank
   (syntax prefix stripped, case-insensitive fallback), `$`-prefixed and
   bare literals become immediates, `[base+...]` becomes a memory
   operand, anything else a symbol reference.
3. **Binarize** (`binarizer.c`): pass 1 assigns addresses and sizes,
   iterating up to 8 rounds until the layout converges, so `.equ`
   constants and directive sizes may reference forward labels; pass 2
   re-encodes and resolves relocations against locally defined labels.
   Self-referential layouts (an address depending on itself) fail with a
   `layout failed to converge` diagnostic instead of silently emitting
   arbitrary bytes. Symbols in numeric, relocation-less operand positions
   must be defined; other undefined symbols become relocations.

## Operand-name binding

SymAS operands are positional; encoding functions use names (`dst`,
`src`, `rd`, `rs1`, `imm`, `sym`, `cond`, ...). The assembler binds each
positional operand under every conventional name for its position
(operand 0 also as `dst`/`rd`, operand 1 also as `src`/`rs1`, operand 2
also as `rs2`), plus `imm` for immediates, `sym`/`target` for symbols,
`mem` for memory operands, and `cond` for conditions. Unused bindings
are ignored by the encoder. `COND` operands accept x86 condition names
(`e`, `ne`, `b`, `ae`, ...) as well as numbers.

## Relocation handling

- Absolute relocs against local labels patch as `S + A`.
- PC-relative relocs patch as `S + A - P`, where `P` is the end of the
  containing instruction when the field is a strict suffix (trailing
  `rel32`-style immediates) and the start of the instruction otherwise
  (whole-word fields).
- The field location always comes from the encoder-reported occurrence
  offset; whole-instruction fields recorded at the instruction end are
  normalized to the trailing `width` bits.
- Fields sharing bytes with opcode bits (nonzero placeholder) are never
  patched: they need relocation-type-specific bit splicing (e.g. RISC-V
  `jal`/`branch` immediates) that only a linker performs. Such
  relocations — and relocations against undefined symbols — are kept for
  `obj` output and rejected for flat `bin` output with a diagnostic.

## Directives (`directives.c`)

Sections (`.text` `.data` `.rodata` `.bss`, `.section NAME`), symbols
(`.globl`/`.global`, `.extern`, `.equ`/`.set` with fixpoint evaluation),
data (`.byte` `.short` `.word` `.long` `.quad`, `.ascii` `.asciz`
`.string`, `.zero`/`.space`/`.skip`, `.fill`, `.align`/`.balign`
`.p2align`, `.org`), and GAS-compatible no-ops (`.type`, `.size`).
`.align N` follows GAS (`2^N` for small `N`; `.balign` takes bytes).
Expressions allow integers, defined symbols, parentheses, and
`+ - * / %` (plus unary `-`/`~`).

## Lua hook (`lua.c`)

`sandstone-as --lua hook.lua` (or `sandstone_asm_load_hook`) runs the
source through a user `preprocess(source, name)` function. liblua loads
via `dlopen()` at run time, so the framework links and runs without Lua;
absent Lua or absent hook, preprocessing is the identity.

## APIs

- C: `assembler/assembler.h` (`sandstone_asm_*`; borrowed getters, owned
  `char *` from `sandstone_asm_preprocess`).
- C++: `assembler/assembler.hpp` (`sandstone::Assembler`, RAII,
  `std::vector` views, exceptions).
