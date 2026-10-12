# Sandstone guide

## 1. Compile a `.symas` bank

List the shipped architecture specs, then build one (or fold several
into a single multi-architecture bank):

```sh
python3 scripts/spec-compiler.py --list-arches
python3 scripts/spec-compiler.py --validate -o x86.symas \
    compiler/stdspec/x86.dsymspec
python3 scripts/spec-compiler.py -o rv.symas \
    compiler/stdspec/riscv64.dsymspec compiler/stdspec/riscv32.dsymspec
```

Use `--compression none|zlib|zstd` to select the payload codec and
`--emit-spec merged.json` to inspect the intermediate JSON document the
Lua DSL produces. To bypass Lua, write the JSON by hand (schema:
`compiler/stdspec/README.md`) and run the compiler binary directly:

```sh
sandstone-spec-compiler --validate -o custom.symas custom.json
```

## 2. Write assembly

```asm
.text
.globl _start
_start:
    mov eax, ebx
    call target
    ret
target:
    nop
```

Syntax (comment character, separators, register/immediate prefixes,
operand order) comes from the bank's syntax record; select it with
`--syntax` (`intel` and `att` for x86). Labels end with `:`. Available
directives: `.text` `.data` `.rodata` `.bss` `.section`, `.globl`
`.extern` `.equ`/`.set`, `.byte` `.short` `.word` `.long` `.quad`,
`.ascii` `.asciz` `.string`, `.zero` `.space` `.fill`, `.align`
`.balign` `.p2align` `.org`.

## 3. Assemble

```sh
sandstone-as --bank x86.symas --syntax intel -o out.bin prog.s
sandstone-as --bank riscv64.symas -f obj -o prog.obj.json prog.s
sandstone-as --bank x86.symas --list-syntaxes
sandstone-as --bank x86.symas -F avx -F -sse2 -o out.bin prog.s
```

Flat (`bin`) output requires every symbol to resolve locally; external
references need `obj` output, which records sections, symbols, and
relocations as JSON for a linker (see `docs/object.md`). Whole-word
PC-relative relocations whose immediates share bytes with opcode bits
(e.g. RISC-V `jal`/`branch` in the shipped specs) always take the `obj`
path — the framework never guesses linker bit-splicing.

## 4. Inspect and query banks

- `sandstone_spec_core` maps spec JSON onto the builder; the same
  `domsymas::Builder` API builds banks programmatically (see
  `tests/smoke.cpp` for a complete example).
- Query APIs cover targets, features, registers, mnemonics, forms,
  operands, encoders, syntaxes, sections, linking, relocations, and the
  optimization sections (`ISEL`/`SCHED`/`UNITS`/`MODELS`/`TUNING`/
  `FUSION`): form selection for a target operation, per-model latency
  and cost, functional units, and fusion pairs.
- `domsymas_disassemble` renders candidate disassemblies for raw bytes.

## 5. Use the small `.isa` sources

For tools that need register names, spelling, and encodings without a
bank, `isa/` ships 23 populated sources with pure-Python and header-only
C++20 loaders:

```python
import sys; sys.path.insert(0, "isa")
from simple_isa import load_arch
rv = load_arch("riscv64", directory="isa")
assert rv.encode("add", rd="a0", rs1="a1", rs2="a2") == bytes.fromhex("3385c500")
```

```sh
python3 isa/simple_isa.py --list
```

## 6. Bindings

```sh
./bindings/generate-bindings.sh --language python --out build/bindings --build
python3 build/bindings/python/smoke.py
```

Details: `docs/bindings.md`.
