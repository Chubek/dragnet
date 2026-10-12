# MLVM Assembler, Linker, and Binary Utilities

`mlvm-as` converts target assembly to relocatable objects; `mlvm-ld` resolves
symbols, lays out sections and applies relocations. Both consume shared target
and object abstractions. Neither may invent instruction encodings or relocation
semantics.

Utilities: `mlvm-ar`, `mlvm-nm`, `mlvm-objdump`, `mlvm-objcopy`,
`mlvm-readobj`, `mlvm-readelf`, `mlvm-strip`, `mlvm-size`, and `mlvm-strings`.
Format readers must validate bounds and offsets before access, reject malformed
inputs safely, and provide deterministic diagnostics.

Implement one target/object-format vertical slice end to end before expanding
the matrix. Test malformed inputs and relocation overflow as well as successful
cases. A command-line stub is not feature support.

## Assembler and static-link target

Target: `x86_64-unknown-linux-gnu`; ELF64 little-endian relocatable input/output,
regular GNU/BSD archives, and static Linux ET_EXEC output. ELF reader validates
headers, counts, ranges, alignments, strings, symbol indexes/local boundaries,
relocation widths, and cumulative payload before copying.
Decoded string bytes are also bounded by input size to prevent repeated-offset
allocation amplification. Unsupported classes,
endianness, architectures, NOBITS, COMDAT, TLS, dynamic metadata, REL relocations,
extended indexes, symbol visibility and processor flags fail explicitly.
Inspection currently accepts relocatable objects, not the linked ET_EXEC image.

Object model owns section bytes, symbols and RELA records. Supported section
flags are WRITE/ALLOC/EXEC; alignments are powers of two up to 4096. Supported
relocations: `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_PLT32` (statically resolved).
Relocations remain real records in relocatable output. Final relocation arithmetic
checks both signed PC32 limits and absolute addition/subtraction overflow.

### Assembler

AT&T operand syntax, one statement per line, `#` comments, named labels (same-line
instruction allowed). Directives: `.text`, `.data`, `.rodata`, `.section` selecting
those names, `.globl`/`.global`, `.weak`, `.type SYMBOL,@function|@object`,
`.size SYMBOL,INTEGER|.-SYMBOL`, `.balign`, `.byte`, `.long`, `.quad` (one value).
Integers are decimal or `0x` hex with optional minus. `.quad SYMBOL` emits ABS64;
`call SYMBOL` and `jmp SYMBOL` emit PC32 with addend -4. `add`/`addl` and
`sub`/`subl` accept `add $imm32, %r32` syntax; `push`/`pushl` and `pop`/`popl`
accept a single `%r32` operand.

Machine encoding is isolated in `mlvm/codegen.hpp`: `ret`, `nop`, `syscall`,
`mov $imm32,%r32`, `mov %r32,%r32`, `add $imm32,%r32` (`0x81 /0`),
`sub $imm32,%r32` (`0x81 /5`), `push %r32` (`0x50+rd`), `pop %r32` (`0x58+rd`)
for eax/ecx/edx/ebx/esp/ebp/esi/edi, near `call`/`jmp`. Register classes outside this subset, arbitrary addressing modes,
instruction prefixes, symbol arithmetic, numeric labels, aliases, BSS, listings,
dependencies and feature switches are unsupported. Unsupported syntax fails with
source line/column. `-o FILE` is required.

### Linker and archive

`mlvm-ld -r INPUT... -o OUT.o` combines matching sections with alignment, preserves
local symbols and relocations, resolves global strong/weak definitions, diagnoses
duplicates. Final linking defaults to `_start` (`-e SYMBOL` overrides); all nonweak
undefined symbols fail. Undefined weak symbols resolve to zero. Each nonempty
allocated section receives a page-aligned load segment at base `0x400000` with
matching permissions. Entry must name an instruction in an executable section.

For static and `-r` linking, archives are processed in command-line order: repeatedly extract members defining
currently unresolved strong symbols until no more are needed. No implicit archive
rescan after later inputs. GNU/BSD long names and symbol-index metadata can be read;
indexes are skipped and extraction uses decoded member symbols. Thin archives,
archive groups, common symbols, scripts, GC sections, trace-symbol, shared output
and dynamic linking remain pending in this mode.

### Baseline binary utilities (`MLVM_ENABLE_BINUTILS=OFF`)

The dependency-free fallback retains the original narrow commands:
`mlvm-ar q -o ARCHIVE FILE...` creates a deterministic archive (zero timestamps,
uid/gid; no index); `mlvm-ar t ARCHIVE` lists members. Names must be unique simple
filenames at most 15 characters. `q` creates rather than appends/replaces.

#### Inspection/manipulation

- `mlvm-nm`: stable symbol value/binding/section/name rows, including undefineds.
- `mlvm-readobj`, `mlvm-readelf`: shared validated ELF identity, sections, symbols,
  relocations; no GNU readelf option compatibility or program/dynamic metadata.
- `mlvm-objdump`: headers, section hex, symbols, relocations; `-d` decodes the machine
  subset above, emits `.byte` for unknown/truncated instructions. `-s`/`-t`/`-r`
  request the default comprehensive view. `add`/`sub` decode as
  `add $0xIMM, %reg` / `sub $0xIMM, %reg`; `push`/`pop` decode with `%reg` operands.
- `mlvm-size`: per-section and total byte counts.
- `mlvm-strings [-n LENGTH]`: printable ASCII (32..126), default length 4, includes
  trailing EOF strings; no Unicode/encoding/offset options.
- `mlvm-objcopy`: `--remove-section=NAME`, `--only-section=NAME`,
  `--rename-section=OLD=NEW`, and verified ELF reserialization. `-O binary` requires
  exactly one selected section with no unresolved relocations. A retained relocation
  referring to a removed section fails. `-o` required.
- `mlvm-strip`: remove only unreferenced local symbols, retaining globals and all
  relocation-required symbols; verified ELF reserialization, explicit `-o` required.
  Debug-specific/full-strip policies remain pending.

## Library-backed binary tools

`MLVM_ENABLE_BINUTILS=AUTO` (the default) enables these subsystems when the
checked-in dependencies are available. `ON` requires them with an explicit
configuration error for a missing source; `OFF` selects the baseline above.
`cmake/MLVMBinutils.cmake` compiles only Bin2Bin's object/archive library sources
from `libstone/limestone`, plus `vendored/tomlc99/toml.c`. `vendored/klib/ketopt.h`
parses tool-specific options. The adapter and all command implementations are
MLVM-owned; public headers expose no dependency types. Builds require no network,
parser regeneration, external linker invocation, or optional Limestone toolchain.

### Object support

Inspection, copying and stripping accept ELF32/ELF64 **relocatable** objects in
either byte order. Identity, PROGBITS/NOTE/NOBITS sections, symbol binding/type/
visibility, REL/RELA records, section groups and COMDAT metadata are preserved.
NOBITS size is logical storage rather than a fabricated byte vector. Unknown
relocation numbers are inspectable and reserializable; applying them requires an
explicit target contract. OS/processor content sections are preserved when they
carry no indexed link/info semantics.

Executables/shared objects, multiple symbol tables, common symbols, extended
section indexes, compressed sections and unsupported indexed metadata fail
explicitly. Reading a machine identity does not add assembler or disassembler
support for that machine. Only the documented x86-64 instruction subset is
disassembled; other identities fail for `-d`. Unknown/truncated x86 instructions
are printed as `.byte`.

Inputs, expanded content/names and serialized outputs are bounded to 256 MiB.
Limits also bound content sections (16,384), symbols/relocations (1,048,576 each),
groups (4,096), archive members/name-table entries (4,096). Archive object budgets
are cumulative and checked before decoding each member. Raw linking additionally
uses Bin2Bin's 64 MiB per-selected-object validation and bounded link work budget.

### Inspection and strings

All object-inspection tools accept multiple objects and ELF archives. Archive
member reports are headed by `archive(member)`.

| Command | Options and behavior |
| --- | --- |
| `mlvm-nm` | Stable name order by default; `-g`/`--extern-only`, `-u`/`--undefined-only`, `--defined-only`, `-n`/`--numeric-sort`, `-p`/`--no-sort`. Rows contain value, binding, section and name. Conflicting filters/sorts fail. |
| `mlvm-readobj`, `mlvm-readelf` | `--file-header`, `-S`/`--sections`, `-s`/`--symbols`, `-r`/`--relocs`, `--groups`, `-a`/`--all`. Default is all views. Symbols also report size/type/visibility. REL addends are labeled implicit. These are MLVM views, not GNU readelf output compatibility; `-h` means help. |
| `mlvm-objdump` | `-f`/`--file-header`, `--section-headers`, `-s`/`--full-contents`, `-t`/`--syms`, `-r`/`--relocs`, `-d`/`--disassemble`, repeatable `-j`/`--section NAME`. Explicit view flags select only those views; default includes headers, bytes, symbols, relocations and groups. Section filters apply to sections, bytes/disassembly and relocations. |
| `mlvm-size` | `-A`/`--format=sysv` (default) reports every content section and total, including zero-fill; `-B`/`--format=berkeley` reports allocated text/read-only, writable data, NOBITS and total. `--radix=d\|o\|x` selects numeric output. |
| `mlvm-strings` | Scans entire files; `-n`/`--bytes LENGTH`, `-t`/`--radix d\|o\|x` byte offsets, `-f`/`--print-file-name`, `-e`/`--encoding s\|l\|b`. Modes are ASCII and aligned UTF-16LE/BE **ASCII code points** (32..126), not general Unicode. Minimum defaults to 4 code points; final EOF runs are included, incomplete code units are ignored. |

### Object copying and stripping

`mlvm-objcopy INPUT -o OUTPUT` and `mlvm-strip INPUT -o OUTPUT` share transactional
section/symbol/relocation/group transformations:

- Repeatable `--remove-section=NAME` and `--only-section=NAME` operate on original
  section names. Multiple `only` names form a union; `remove` takes precedence.
- Repeatable `--rename-section=OLD=NEW` applies after selection. Conflicting
  renames for one source name are command-line errors.
- `--discard-all` removes unreferenced local symbols; this is `mlvm-strip`'s
  default. Relocation targets and group signatures remain pinned.
- `--strip-debug` removes `.debug*`, `.zdebug*` and `.stab*` sections.
- `--strip-all` removes all symbols and requires an input without relocations or
  groups. Combining different strip policies is diagnosed.
- A retained relocation or group referring to a removed definition fails without
  changing the source object. Surviving indexes are remapped and verified.
- `-O binary` requires exactly one file-backed selected section and no retained
  relocations. NOBITS raw extraction fails explicitly.

ELF output is canonically serialized, preserving the semantic object model rather
than original file offsets/padding. Input and output must be different files.

### Indexed archives

`mlvm-ar` preserves **member bytes exactly**, including arbitrary non-ELF members.
ELF members contribute global/weak definitions to a deterministic GNU symbol index
using Bin2Bin. MLVM remaps index offsets after restoring original member payloads.
GNU/BSD long names and indexes are accepted; thin archives are unsupported.

```sh
mlvm-ar q first.o 'long object name.o' notes.txt -o library.a
mlvm-ar t library.a
mlvm-ar r library.a replacement.o -o updated.a
mlvm-ar d updated.a first.o -o reduced.a
mlvm-ar p library.a notes.txt > notes-copy.txt
mlvm-ar x library.a 'long object name.o' --output-dir existing-directory
```

`q` creates; `r` replaces existing members in place in the member order and
appends new names; `d` deletes selected names. Creation/replacement require unique
basenames. Archive writes always use a distinct `-o` output, so `r ARCHIVE ... -o
ARCHIVE` fails. All writes rebuild the index. Non-ELF members have no index entries;
malformed/unsupported ELF payloads fail during indexing. `t`/`p`/`x` only require
valid archive framing. Without member names, `p`/`x` select all; missing requested
members fail. `p` writes binary stdout. `x` requires an existing output directory
and validates every selected destination before writing: path components,
traversal names and duplicate destinations are rejected. No implicit archive
input overwrite or directory creation occurs.

### Metadata-driven raw linking

The library-backed `mlvm-ld` also emits an owning addressed **raw binary image**:

```sh
mlvm-ld --target-config examples/targets/x86_64.toml -O binary \
  --base-address 0x1000 caller.o library.a --map image.map -o image.bin
```

This mode consumes an explicit version-1 TOML object/relocation contract parsed by
`tomlc99` and checked by Bin2Bin. Required top-level fields:

```toml
version = 1
class = 64                     # 32 or 64
machine = 62                   # explicit ELF e_machine
endianness = "little"          # little or big
flags = 0
osabi = 0
abi_version = 0
text_alignment = 16

[[relocations]]
type = 2
name = "R_X86_64_PC32"
kind = "pc_relative"           # absolute or pc_relative
storage_bytes = 4             # 1..8
bit_offset = 0
bits = 32                     # 1..64; contiguous storage field
scale = 1                     # positive; exact division required
signed = true
# pc_bias = 0                 # optional, PC-relative only
# implicit_addend_signed = true  # required when applying REL
```

`relocations = []` declares a target with no legal relocation types. Unknown keys,
wrong types, duplicates, invalid widths/alignment, NUL bytes/escapes and unsupported
versions fail. TOML integer fields are signed-64-bit values; identity/storage fields
are additionally checked against their defined widths. Optional REL signedness
describes **input addend interpretation**, independently of result signedness.
The shipped x86-64 example declares ABS64, PC32 and statically resolved PLT32.

Allocated sections follow object/section declaration order and preserve their
alignment and permissions metadata. Padding/NOBITS are zeroed. Roots are processed
first; all archives are then rescanned to a fixed point, extracting definitions
for unresolved strong references. Weak undefined references do not extract members
and resolve to zero. Strong/weak resolution, duplicate diagnostics and COMDAT
selection come from Bin2Bin. Object class, machine, byte order, ABI and processor
flags must match the contract. Nonallocated sections do not enter the image.

Absolute relocations encode `(S + A) / scale`; PC-relative ones encode
`(S + A - (P + pc_bias)) / scale`. Arithmetic, field bounds, scale divisibility,
overflow and overlapping patches are checked. `--defsym=NAME=ADDRESS` supplies
repeatable explicit external addresses; duplicates fail. `--base-address` defaults
to zero and accepts decimal/`0x` addresses. `--map FILE` records section placements,
sorted global symbols and discarded groups; map/image outputs must differ from
each other and all inputs, including the target contract and response files.

Raw images carry no executable container, entry point, ABI/runtime installation
or executable permissions. `-r`/entry selection are unsupported in this mode.
TLS, GOT/PLT construction, custom relocation adapters and allocated OS/processor
section contracts are not exposed by the current TOML grammar. The original
static ELF and relocatable modes use the assembler/static-link target above.

## CLI and verification

Every built tool supports `--help`, `--version`, `--color=never`, `--`, quoted/escaped
`@response` arguments (nesting 16; cycles rejected). Unsupported options fail rather
than being ignored. Text output defaults to stdout only where documented; `-` is
stdout for text `-o` and is not a binary stdin/stdout convention. Inputs are bounded
at 256 MiB. Inputs, included headers, response files, symlink and hardlink aliases
are protected against output overwrite. Exit statuses: 0 success, 1 operation/
compilation/link failure, 2 invalid usage, 3 I/O or object/archive format error.

`tests/integration/toolchain.py` exercises CLI failure statuses, deterministic
objects, compile/assemble/link/run, archive extraction, raw bytes, inspection and
input protection. GNU readelf serves as an independent ELF oracle when installed;
native execution is tested only on Linux/x86-64. C++ regressions test every truncated
prefix of a valid object, corrupt offsets, symbol resolution and relocation limits.
`tests/binutils.cpp` tests both ELF classes/byte orders, REL/RELA, zero-fill,
group-preserving transformations, transactional failure, contracts and raw linking.
`tests/integration/binutils.py` exercises the new command views, indexed/long-name
archives, byte-preserving extraction, raw images/maps, string encodings, overwrite
protection and native GNU `ar`/`nm`/`as`/`readelf` interoperability when available.
