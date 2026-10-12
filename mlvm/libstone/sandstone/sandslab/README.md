# SandSLAB

SandSLAB is a header-only C++20 machine-near code generator. Its IR is a
sequence of the named instruction forms and operands defined by `isa/*.isa`,
plus labels and local relative fixups. The existing ISA loader performs register,
operand, range, alignment, and encoding validation. DomDSL supplies the
compile-time string tags and pipeline mixin, and the ISA source parser uses its
parser combinators.

## Build

The repository build enables `SANDSTONE_BUILD_SANDSLAB` by default. To build just
SandSLAB and the example from the repository root:

```sh
cmake -S sandslab -B build-sandslab \
  -DDOMDSL_INCLUDE_DIR=/mnt/warble/domweave/domlibs/domdsl
cmake --build build-sandslab
ctest --test-dir build-sandslab --output-on-failure
```

DomDSL is discovered automatically in the sibling `domlibs/domdsl` directory;
`DOMDSL_INCLUDE_DIR` overrides that location. Link the interface target
`Sandstone::sandslab` (or `sandslab`) in a source build. Installation provides
the headers, ISA data, `forthcc`, and `find_package(SandSLAB CONFIG REQUIRED)`.
The installed interface target is `Sandstone::sandslab`.

## Instruction IR

```cpp
#include <SandSLAB.hpp>

namespace sl = sandslab;
struct Add : sl::Instr<"add"> { using Instr::Instr; };

auto rv = sl::isa::load_arch("riscv64", "isa");
sl::Program program(std::move(rv));
program.emit(Add{{{"rd", "a0"}, {"rs1", "a1"}, {"rs2", "a2"}}});
auto code = program.compile();
// code.bytes: 33 85 c5 00
// code.assembly: "add a0, a1, a2\n"
```

`Instr<"form">` names an **operation form**, such as `add` or `mov_imm`, whose
encoding and syntax come from the loaded ISA. It can be subclassed as above or
used directly. `emit("add", operands)` is the runtime equivalent. `Program`
owns an immutable target snapshot and retains the IR, accessible via `nodes()`.
Compilation is repeatable and does not mutate the IR. DomDSL pipelines can use
`program.wrap(value) | dsl::pipe(transform)` for frontend transformations.

`assembly()` renders programs without fixups, including text-only targets.
`compile()` returns bytes, label byte offsets, and a rendered listing after
fixup resolution. It rejects text-only encodings. Exceptions use
`sandslab::Error` (the ISA loader's `ISAError`).

### Local branches

```cpp
sl::Program p(sl::isa::load_arch("x86", "isa"));
p.emit(sl::Instruction{"jmp", {}},
       {{"offset", "done", sl::RelativeBase::next_instruction}})
 .emit("ret")
 .label("done")
 .emit("ret");
auto code = p.compile(); // e9 01 00 00 00 c3 c3
```

A fixup supplies an operand instead of an explicit numeric value. Its value is
`(label_offset - base_offset - bias) / unit`, with exact divisibility required.
The default base is the instruction start and the default unit is one byte.
Use `next_instruction` for x86 branches; use instruction start for RISC-V and
AArch64 byte-relative branches. An ARM PC bias or BPF instruction-unit offset
can be specified explicitly. The ISA encoder checks final operand ranges.

Fixups require stable instruction sizes; stream/LEB fixups and encodings whose
size changes after resolution are rejected. There is no branch relaxation,
external linker, object-file writer, register allocation, or CPU-feature
selection. The caller selects valid forms and manages its target ABI. Rendered
branch operands in `code.assembly` are numeric displacements, **not assembler
labels**; this is a listing rather than a portable reassemblable branch source.
WebAssembly/SPIR-V emission produces fragments, not complete modules.

## Forth compiler example

`examples/FORTHCC.cpp` compiles a closed Forth expression to a callable AMD64
leaf function with a 64-bit result in RAX. `examples/Forth.hpp` contains its
reusable lowering routine. It uses only caller-saved RAX/RCX registers and
balances the machine stack before returning. Integer arithmetic wraps at 64
bits. It supports signed decimal int64 literals and these words:

* Arithmetic: `+`, `-`, `*`, `negate`
* Bitwise: `and`, `or`, `xor`
* Stack: `dup`, `drop`, `swap`, `over`
* Comments: `\\` to end of line, or whitespace-delimited `( ... )`

Stack underflows, unknown words, malformed/overflowing literals, unterminated
comments, and a final stack depth other than one are errors. This is an
expression compiler; dictionaries, definitions, control flow, and I/O words are
outside the example's language.

```sh
echo '6 7 *' | build-sandslab/forthcc --isa-dir isa -o answer.bin
echo '3 dup * 2 +' | build-sandslab/forthcc --isa-dir isa --asm -o answer.s
cc -c answer.s -o answer.o
```

The binary output is raw function bytes, not an executable file. The assembly
output is GNU Intel syntax with an exported `forth_entry` symbol, suitable for
linking with a C declaration `extern long long forth_entry(void);` on AMD64.
Input defaults to stdin; specify a filename or `-`. Output defaults to stdout;
`-o` selects a file. The default ISA directory is the configured installation's
`share/sandstone/isa`; use `--isa-dir` for an uninstalled or relocated build.

Tests check known instruction bytes, forward/backward branches, invalid IR,
range failures, text-only targets, and Forth diagnostics. On x86-64 Unix they
also execute the generated Forth functions and check their return values.
