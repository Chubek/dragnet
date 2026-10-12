# DomSymAsCC stdspec

For a smaller, directly loadable set of representative instruction encodings,
see `../../simple-isa/README.md`, `../../simple_isa.py`, and the header-only
C++ interface `../../simple_isa.hpp`.

`.dsymspec` files are assembler-target specifications written in the
DomSymAsCC DSL (a small Lua library run under the embedded DomMune
interpreter). Each file describes one architecture's registers, extensions,
relocations, sections, and a representative instruction set, and prints a
JSON document that `domlibs/domsymas/compiler` maps onto `domsymas::Builder`
to emit a SymAS databank (`.symas`).

```text
  .dsymspec (Lua DSL)  ->  JSON document  ->  domsymas::Builder  ->  .symas
       (DomMune)             (this file)        (spec_document.cpp)
```

A spec may also describe *how the target prefers to be used*: which form an
operation lowers to, what that form costs, which pipeline resources it uses,
which CPU models exist, what each model changes, and which instruction pairs
may be fused. Those records go into the optional `ISEL`, `SCHED`, `UNITS`,
`MODELS`, `TUNING` and `FUSION` sections (SymAS format 1.1) and are documented
in the last section of this file.

## Using the DSL

```lua
local symas = require("dsl").new()

symas.target{ arch = "riscv64", addr_size = 64, byte_order = "little", ... }
symas.feature{ name = "m", desc = "integer multiply/divide" }
symas.register{ name = "x0", class = "gpr", width = 64, enc = 0,
                flags = { "readable", "writable" } }
symas.encoder{ name = "add_rr", source = "emit32le(0x0033 | (rd << 7) | (rs1 << 15) | (rs2 << 20));" }
symas.mnemonic{ name = "add", ci = false }
symas.form{ mnemonic = "add", encoder = "add_rr", ops = 3,
            operands = { { kind = "reg", class = "gpr" },
                         { kind = "reg", class = "gpr" },
                         { kind = "reg", class = "gpr" } } }
symas.syntax{ name = "default", comment = "#" }
symas.section{ name = ".text", flags = { "exec", "alloc", "code" }, align = 4,
               content = "code" }
symas.reloc{ name = "R_RISCV_CALL", width = 32, signed = true, pcrel = true,
             addend = "explicit", expr = "S + A - P" }

print(symas.emit())
```

`require("dsl")` returns a module; `dsl.new()` returns a fresh builder so
multiple spec files can be compiled into one databank without sharing state.
`emit()` performs a referential-integrity check (unknown mnemonic/encoder/
feature/syntax names are rejected) before serializing.

## JSON document schema

The document is a single object. `schema` is always `1`. Records reference
one another by **name**; the C++ compiler resolves names to numeric ids.

```jsonc
{
  "schema": 1,
  "targets":    [ Target ],
  "features":   [ Feature ],
  "registers":  [ Register ],
  "mnemonics":  [ Mnemonic ],
  "encoders":   [ Encoder ],
  "forms":      [ Form ],
  "syntaxes":   [ Syntax ],
  "sections":   [ Section ],
  "linkages":   [ Linkage ],
  "relocs":     [ Reloc ],
  "models":     [ Model ],
  "units":      [ Unit ],
  "isels":      [ Isel ],
  "scheds":     [ Sched ],
  "tunes":      [ Tune ],
  "fusions":    [ Fusion ],
  "debug":      [ Debug ]
}
```

The six optimization arrays (`models` .. `fusions`) are optional; a document
that omits them produces a bank without the optimization sections, which is
valid.

### Target

```jsonc
{ "arch": "riscv64", "subarch": "", "addr_size": 64, "op_size": 32,
  "addr_def": 64, "align": 2, "byte_order": "little", "obj_format": "ELF64",
  "abi": "LP64D", "calling_conv": "lp64d", "features": ["i", "m", "c"] }
```

`byte_order` is `"little"`/`"le"` or `"big"`/`"be"`. `align` must be a power
of two. `features` lists feature **names** declared in the `features` array.

### Feature

```jsonc
{ "name": "m", "desc": "integer multiply/divide", "flags": 0 }
```

### Register

```jsonc
{ "name": "w0", "class": "gpr", "width": 32, "enc": 0, "parent": "x0",
  "sub_lo": 0, "sub_hi": 32, "flags": ["readable", "writable"] }
```

`class` ∈ `gpr seg ctrl debug vec mask fp status special`. `parent` is a
register **name** (or absent). `width` ∈ `{8,16,32,64,80,128,256,512}`.
`flags` ∈ `readable writable imp_used imp_mod modedep`.

### Mnemonic

```jsonc
{ "name": "add", "aliases": ["ADD"], "ci": false }
```

`ci` sets the case-insensitive bit.

### Encoder

```jsonc
{ "name": "add_rr", "source": "emit32le(...);", "flags": 0,
  "max_bytes": 4, "max_relocs": 0 }
```

`source` is the Algol-like encoding language parsed by
`domlibs/domsymas` (`domdsl`). Statements: `emit8`, `emit16le/be`,
`emit32le/be`, `emit64le/be`, `emit_imm(width,v)`, `emit_disp(width,v)`,
`emit_modrm(dst,src)`, `emit_sib(a,b,c)`, `emit_relocation(sym,TYPE)`,
`emit_reloc`, `require(cond);`, `if (...) {...} else {...}`, `fail("msg")`.
Operands are referenced by name (`rd`, `rs1`, `imm`, ...).

### Form (with inline operands)

```jsonc
{ "mnemonic": "add", "ops": 3, "encoder": "add_rr",
  "flags": [], "required": [], "forbidden": [], "syntaxes": [],
  "operands": [ Operand, Operand, Operand ] }
```

`ops` defaults to `#operands`. `flags` ∈ `privileged branch cond_branch
return terminator may_trap reads_mem writes_mem delayed implicit_ops reloc
aliases`. `required`/`forbidden` list feature names; `syntaxes` lists syntax
names.

### Operand

```jsonc
{ "kind": "reg", "width": 64, "class": "gpr", "imm_min": 0, "imm_max": 0,
  "signed": false, "role": 0, "access": "read", "hidden": false,
  "constraint": "" }
```

`kind` ∈ `reg imm disp mem abs rel sym cond reglist implicit`. `access` ∈
`read write readwrite`. For `kind == "reg"` the `class` may name a register
class.

### Syntax

```jsonc
{ "name": "default", "order": 0, "reg_prefix": "", "imm_prefix": "",
  "comment": "#", "separator": ", ", "tmpl": "{mnemonic} {ops}", "ci": false }
```

`order` `1` reverses operand order (AT&T-style).

### Section / Linkage / Reloc / Debug

```jsonc
{ "name": ".text", "flags": ["exec", "alloc", "code"], "align": 4,
  "addrspace": 0, "content": "code" }

{ "name": "global_func", "binding": "global", "visibility": "default",
  "symtype": "func", "flags": 0 }

{ "name": "R_RISCV_CALL", "width": 32, "offset": 0, "signed": true,
  "pcrel": true, "overflow": "error", "addend": "explicit", "expr": "S + A - P" }

{ "msg": "...", "kind": "form", "ref": "add" }
```

`section.flags` ∈ `exec write alloc readonly zerofill merge discard tls debug
code data meta`; `content` ∈ `unknown code data meta`. `reloc.overflow` ∈
`error truncate`; `reloc.addend` ∈ `explicit infield implied`. `debug.kind` ∈
`target register feature mnemonic form operand encoder syntax section linkage
reloc isel sched unit model tune fusion`.

## Optimization records

These records answer "how should a consumer use this target", not "how is it
encoded". Every record references other records **by name**; the compiler
resolves those names against the same document and fails if a name is unknown.

### Form references

Instruction selection, scheduling, tuning and fusion all name an instruction
form with a *form reference* string:

| Reference      | Resolves to                                        |
| -------------- | -------------------------------------------------- |
| `"add"`        | every form of the mnemonic `add`                   |
| `"add#2"`      | the second form of that mnemonic (1-based)          |
| `"add/add_rr"` | the form of that mnemonic that uses encoder `add_rr` |

A reference may also be a list of such strings. `sched` and `tune` expand to
one record per matching form; `fuse` emits one record per `(a, b)` combination.
A form name that resolves to no form is an error.

### Model (machine description)

```jsonc
{ "name": "skylake-client", "vendor": "intel",
  "issue_width": 6, "rob": 224, "loop_buffer": 50,
  "load_latency": 5, "mispredict": 14,
  "high_latency": 0, "high_uops": 0,
  "flags": [ "out_of_order", "has_stalls", "default" ],
  "features": [ "sse2", "avx" ], "tune": [ "macrofusion" ],
  "desc": "6-wide, 224-entry ROB" }
```

`rob` is the reorder (micro-op) buffer size; `0` means in-order. `features`
lists architectural features (see `Feature`), `tune` lists tuning-only
features. `flags` ∈ `in_order post_ra complete prefer_post_ram no_itineraries
vliw predicable morphable has_stalls forwarding out_of_order bundle_width
default`. Exactly one model per target should carry `default`.

### Unit (functional unit / execution port)

```jsonc
{ "name": "p0", "model": "skylake-client", "kind": "int",
  "count": 1, "latency": 1, "throughput": 1,
  "flags": [ "pipelined" ] }
```

`model` names a model or is empty for a target-wide unit. `kind` ∈ `other int
fp load store branch vector mask divider mul complex pipe system`. `flags` ∈
`shared superscalar pipelined noncoincident`.

### Isel (instruction selection)

```jsonc
{ "node": "add", "kind": "node", "form": "add#2", "cost": 1,
  "complexity": 2, "priority": 0, "flags": [ "commutes" ],
  "required": [ "sse4.2" ], "forbidden": [],
  "alts": [ "add" ], "pattern": "(add GPR:$a, (i64 0))" }
```

`kind` ∈ `node pattern pseudo expansion select postisel copy aggressive`.
`flags` ∈ `prefer expand needs_expand variadic commutes chain side_effects
unprofitable convergent tied`. `pattern` is documentation only. `alts` lists the
other forms of the same operation, best first.

### Sched

```jsonc
{ "form": "add", "name": "WriteALU", "latency": 1, "uops": 1, "cycles": 0,
  "flags": [ "variadic" ], "writes": [ "p0" ], "reads": [ "p0" ] }
```

`latency` of `4294967295` means variadic. `flags` ∈ `variadic multicycle
serializing barrier may_fault side_effects fused unmodeled nop`. At most one
sched record may name a given form.

### Tune (per-model optimization hint)

```jsonc
{ "model": "atom", "kind": "latency", "name": "latency", "form": "imul",
  "form2": "", "value": 4, "flags": [ "override" ],
  "features": [], "desc": "in-order multiply latency" }
```

`kind` ∈ `generic latency uops cost prefer avoid threshold unroll vec_width
pair strategy preroll split`. `prefer` raises a form's selection priority,
`avoid` removes it from the candidate set for that model, and `latency`,
`uops` and `cost` override the base record. `form2` scopes a `pair` hint to
two forms. `flags` ∈ `only_if override experimental disable negative`.

### Fusion

```jsonc
{ "name": "cmp-jcc", "a": "cmp", "b": "jcc", "kind": "cmp_branch",
  "latency": 1, "flags": [ "profitable", "requires_flags" ],
  "features": [ "macrofusion" ], "pattern": "cmp/test ; jcc" }
```

`kind` ∈ `macro cmp_branch branch_pair mem_arith mul_add shift mov load_op
cmp_select vsetvli target`. `flags` ∈ `profitable requires_flags same_reg
adjacent lock_eligible suppress_unfuse bidirectional`.

## Layout

Each file in this directory (`<arch>.dsymspec`) mirrors a canonical backend
key from `domainlet/bekern/arch`. Register files, register classes, ABI
metadata, extensions, and target triples are drawn from
`domainlet/bekern/arch/_arch_specs.py`; instruction encodings follow the
corresponding ISA manuals and are cross-checked with the `arch_oracle` /
`isa_reference` MCP tools.

The optimization blocks are cross-checked with the `tablegen_inquiry` MCP tool,
which reads the same LLVM `.td` files under `$TABLEGEN_LIB`
(`llvm/lib/Target/<Arch>`): `SchedMachineModel` records supply the model
numbers (issue width, micro-op buffer, load latency, mispredict penalty),
`ProcResource`/`ProcResGroup` definitions supply the functional units, the
`WriteRes<...>`/`SchedWriteRes<...>` tables supply per-class latencies, and the
macro-fusion definitions (for example `RISCVMacroFusion.td`) supply the fusion
pairs. Each spec names the exact files it used in a comment above its
optimization block. Targets that LLVM does not model with a
`SchedMachineModel` (AVR, BPF, LoongArch, SPARC, WebAssembly, NVPTX, SPIR-V,
DirectX) take their numbers from the architecture manuals and from
`arch_oracle`, and say so.
