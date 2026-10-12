# Chapter 3. First Programs and Workflows

[Previous: Building](02-building-installing-and-validating.md) · [Contents](README.md) · [Next: Metacode](04-metacode-and-the-infobank.md)

## 3.1 A tour through increasing levels of lowering

The quickest way to understand Limestone is to run small programs at several
boundaries. This chapter starts with source evaluation, then examines portable
compilation, target selection, standalone algorithms, binary translation, and
object emission. Each workflow adds a contract: source meaning, target patterns,
timing, storage, encoding, or ABI.

Build the tools as described in Chapter 2. All commands below run from the
repository root and use `build/limestone-cli`. The version and command summary
are available through:

```sh
mkdir -p /tmp/opencode
build/limestone-cli --version
build/limestone-cli --help
```

Use output files for encoded bytes and objects. Text-oriented inspection modes
are convenient on standard output; binary modes emit raw bytes.

## 3.2 Evaluate a closed TraceML program

```sh
printf '%s\n' '((lambda x (add x 2)) 40)' | build/limestone-cli --evaluate
```

The result is `42`. The lambda captures its lexical environment and receives its
argument lazily. When `add` needs `x`, evaluation obtains the integer argument;
the primitive then evaluates its operands strictly from left to right.

Try a branch whose unused arm would fail:

```sh
printf '%s\n' '(if (lt 1 2) 42 (add 9223372036854775807 1))' | build/limestone-cli --evaluate
```

The result is again `42`, because the untaken arm is not evaluated. This is a
source-semantic property, not an optimization that a later backend is free to
reverse. Chapter 15 explains checked arithmetic, closures, and execution budgets.

Evaluation returns a value. It does not select target instructions or create a
callable native function.

## 3.3 Compile to portable machine-oriented output

Omit the evaluation mode:

```sh
printf '%s\n' '(add 20 22)' | build/limestone-cli
```

The default source workflow evaluates the closed computation and lowers the
result through a portable constant/return graph. The output is a diagnostic
machine-oriented listing. It shows a successful compiler handoff, without needing
a particular CPU encoding or register ABI.

For an owning serialized region contract:

```sh
printf '%s\n' '(add 20 22)' | build/limestone-cli \
  --machineir-json -o /tmp/opencode/answer-region.json
```

This JSON is the C++/D MachineIR exchange, carrying more than textual instructions.
It retains value identities, dependencies, final order, and the available machine
envelope. Inspect it as a structured format, and use its checked readers for
programmatic edits. Chapter 11 describes the admitted edit boundary.

## 3.4 Record metatracing information

```sh
printf '%s\n' '(if (lt 1 2) (add 20 22) 0)' | build/limestone-cli \
  --trace-execution -o /tmp/opencode/answer-trace.mir
```

The machine listing goes to the output file. Execution events are printed to
standard error. Unlike constant-result lowering, trace lowering retains executed
operations and a guard for the selected branch. The guard is meaningful: compiling
it to executable code requires a backend contract for a failed guard, including
continuation or deoptimization behavior.

The CLI's `--trace-execution` and Tunah's `--trace` refer to different facilities.
The former records MetaKrivine execution; the latter reports admitted rewrite
matches during term optimization.

## 3.5 Compile an explicit UMD graph

The repository contains a small include-based arithmetic example:

```sh
build/limestone-cli --compile-umd tests/fixtures/arithmetic-includes.umd
build/limestone-cli --compile-umd --selector burs \
  tests/fixtures/arithmetic-includes.umd
```

The file loader resolves the shared scalar machine and source graph relative to
the including file. The graph contains two constants and an addition. Both graph
selection and BURS use the explicit target patterns, while downstream scheduling
uses the machine's instruction models.

A complete small UMD document can also be supplied directly:

```text
machine scalar {
  regclass G = [$r0, $r1];
  operator const(0);
  operator add(2);
  instruction CONST { latency = 0; }
  instruction ADD { latency = 1; }
  pattern constant: const():i64 -> CONST;
  pattern sum: add(?lhs:i64, ?rhs:i64):i64 -> ADD;
  default_register_class = G;
}
program main {
  node %1 = const(20):i64;
  node %2 = const(22):i64;
  node %3 = add(%1, %2):i64;
  output %3;
}
```

Save it to `/tmp/opencode/sum.umd` and compile:

```sh
build/limestone-cli --compile-umd --allocate --allocator color \
  /tmp/opencode/sum.umd
```

The register class supplies two physical choices, and the instruction models
supply latency. There is no encoding contract in this document; the useful result
is allocated, inspectable IR. Chapter 5 explains the document and graph invariants.

## 3.6 Observe BURS selection and rejected alternatives

```sh
build/limestone-cli --select-burs tests/fixtures/selection.limeburg
build/limestone-cli --analyze-burs tests/fixtures/selection.limeburg
```

The fixture's general addition requires two register-producing children. A
cheaper immediate rule can cover the right constant when it lies in `[-8..7]`.
The left constant `100` is still materialized by a constant rule, while the right
constant `7` is consumed inside `ADDI`. The total cost combines the chosen rule
with the derivation cost of its remaining boundary child.

Analysis shows every reachable nonterminal state and attempted rule. Changing
the right constant to `8` makes the immediate rule inapplicable. The analysis
then explains the range rejection, and selection can use the general form if its
child derivations exist. This is more informative than inspecting an opcode list
alone.

For an Infobank-derived example:

```sh
build/limestone-cli --select-burs \
  limeburg/specs/examples/riscv64-addi.limeburg
```

This selects a qualified RISC-V inventory operation using the generated
specification. Its source adapter contract is different from claiming that every
generic `add` graph can be natively emitted for RISC-V.

## 3.7 Run standalone scheduling and allocation

```sh
build/limestone-cli --schedule-il tests/fixtures/scheduling.schedrow
build/limestone-cli --schedule-il tests/fixtures/grouping.schedrow
build/limestone-cli --allocate-il --allocator constraint \
  tests/fixtures/allocation.regtl
```

The scheduling examples exercise explicit resources, result latency, and grouping.
The printed issue assignments are checked against the region and machine model.
Resource names and optional slots explain how simultaneous issue was made legal.

The allocation example contains a CFG, transfers, and a parallel physical swap.
Each function is analyzed as its own allocation problem. A reported spill is a
decision about storage; the standalone allocator does not invent target reloads
or a calling convention. Chapters 8 and 9 explain how the pipeline materializes
those decisions when a target provides the necessary contracts.

## 3.8 Optimize an S-expression

The supplied pure arithmetic tuner files can be loaded after their vocabulary:

```sh
printf '%s\n' '(iadd (iadd 12 30) (imul x 0))' | build/limestone-cli \
  --optimize-term \
  --rules tunah/tuners/instr-level-tune/vocabulary.tuner \
  --rules tunah/tuners/instr-level-tune/alg-sim.tuner \
  --rules tunah/tuners/instr-level-tune/const-folding.tuner \
  --trace
```

The extracted expression is `42`. Standard error contains named rule matches and
statistics. The host semantic model behind these rules is wrapping arithmetic
at declared widths; a generic term spelling alone does not establish TraceML's
checked-overflow semantics. Use only rules justified for the IL you are optimizing.

You can configure local extraction costs with `--op-cost`, literal cost with
`--literal-cost`, and iteration/node/class/time budgets. A bounded run may return
a valid equivalent expression without reaching saturation. Chapter 10 explains
this distinction and the concrete typed graph adapter.

## 3.9 Translate a tiny encoded bytecode

The bytecode fixtures use different byte values for the same counter increment:

```sh
printf '\001\001' > /tmp/opencode/counter-source.bin
build/limestone-cli --disassemble tests/fixtures/byte-source.isa \
  /tmp/opencode/counter-source.bin
build/limestone-cli --decompile tests/fixtures/byte-source.isa \
  /tmp/opencode/counter-source.bin
build/limestone-cli --translate tests/fixtures/byte-source.isa \
  tests/fixtures/byte-target.isa /tmp/opencode/counter-source.bin \
  -o /tmp/opencode/counter-target.bin
```

The source contains two `0x01` instructions. The target contains two `0x09`
instructions. Translation follows the explicit semantics and shared state model,
not the differing mnemonic spellings. Decompilation displays semantic assembly;
it does not reconstruct a C source program.

The same source and target architecture may also be used for same-ISA translation.
Addressed branches and richer operands use the masked codec, discussed in
Chapter 12. Persistent cache configuration is covered in Chapter 14.

## 3.10 Emit and use a native object

On a compatible System V x86-64 host, the bounded native constant fixture provides
an explicit no-argument i64 return contract:

```sh
printf '%s\n' '((lambda x (add x 2)) 40)' | build/limestone-cli \
  --target-isa tests/fixtures/native-constant.isa \
  --selector burs --allocate --allocator color --encode \
  --object native_entry -o /tmp/opencode/native-answer.o
build/limestone-cli --inspect-object /tmp/opencode/native-answer.o
```

The frontend evaluates `42`. The target selects a signed-32-bit constant
materialization in RAX and a return that uses RAX. RegTL enforces the fixed
operands, the encoder verifies them, and the object layer packages the bytes
under the declared ELF identity.

A C consumer can be:

```c
#include <stdint.h>
#include <stdio.h>

extern int64_t native_entry(void);

int main(void) {
  int64_t value = native_entry();
  printf("%lld\n", (long long)value);
  return value == 42 ? 0 : 1;
}
```

Save it as `/tmp/opencode/native-main.c`, then use the compatible system toolchain:

```sh
cc /tmp/opencode/native-main.c /tmp/opencode/native-answer.o \
  -o /tmp/opencode/native-answer
/tmp/opencode/native-answer
```

This fixture's instruction slice handles closed values in the signed 32-bit
range, returned as i64. A larger value fails operand legality. General native
closures, arguments, stack frames, and arbitrary arithmetic instructions require
the corresponding target/runtime lowering contracts.

## 3.11 What to inspect next

Each workflow has an observable intermediate result. Keep the source, normalized
target, candidate/rule analysis, selected region, final order, allocation/frame,
encoded bytes, and object listing when investigating a problem. The later
chapters explain those artifacts in depth.

The most useful next step is to load an authoritative description and understand
which facts it supplies. That is the subject of Chapter 4.

[Previous: Building](02-building-installing-and-validating.md) · [Contents](README.md) · [Next: Metacode](04-metacode-and-the-infobank.md)
