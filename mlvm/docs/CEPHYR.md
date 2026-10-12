# Cephyr C Compiler

Cephyr is the C compiler distributed with MLVM. Keep preprocessing, lexing,
parsing, semantic analysis, constant evaluation, lowering, optimization, and
code generation as separate components with stable test seams.

The initial milestones are:
1. command-line parsing and deterministic diagnostics;
2. lexer and parser with source ranges;
3. `-E` and `-fsyntax-only`;
4. types, scopes, declarations, expressions, and constant evaluation;
5. lowering into MLVM's typed C/high-level dialect;
6. SSA lowering and optimization;
7. `-S` assembly output, `-c` object output, then integration with `mlvm-ld`.

Document the selected C standard and supported feature matrix. Do not describe
Cephyr as ISO C conforming until conformance tests justify that claim. Establish
a freestanding target/sysroot first; hosted mode requires explicit headers,
runtime, and ABI decisions.

## 0.1.0 supported feature matrix

Selected mode: C17 freestanding, x86-64 System V integer-return convention,
32-bit `int`; no hosted headers, sysroot, libc, or implicit host compiler invocation.

| Stage | Implemented | Explicit boundary |
| --- | --- | --- |
| Preprocess | Object macros, recursive suppression, escaped-newline splicing, comments, quoted tokens, `-D`/`-U`, quoted/angle includes, `-I`, forced includes, `#undef`, `#ifdef`/`#ifndef`, `#if 0/1`, `#else`/`#endif`, `#error` | Function macros, `#`/`##`, general conditional expressions, `#elif`, `#line`, pragmas rejected |
| Predefined macros | `__STDC__=1`, `__STDC_VERSION__=201710L`, `__STDC_HOSTED__=0` | Target builtin macros/types pending |
| Parsing | Vendored `tree-sitter-c/src/parser.c`, syntax errors with byte locations | Parser acceptance is not semantic/conformance acceptance |
| Semantics | Unique `int name(void)` definitions, one return statement, unsuffixed signed-int literals, parentheses, unary `+`/`-`, binary `+`/`-`/`*`/`/`/`%`/`&`/`\|`/`^`/`<<`/`>>`, checked constant overflow, division-by-zero and shift-range diagnostics | Declarations, parameters, locals, calls, pointers, qualifiers, aggregates, storage classes, varargs, atomics, control flow rejected |
| Lowering | Constant-return function into registered MLVM `builtin`/`arith`/`func` dialects | Typed-C/high/mid/low staged lowering pending |
| Codegen | `%eax` imm32 return, `ret`, global ELF function symbols and size | Selection/scheduling/regalloc/frame lowering for general functions pending |
| Driver | `-E`, `--parse-only`, `-fsyntax-only`, `--emit-ir`, `-S`, `-c`, `-o`, `-I`, `-D`, `-U`, `-include`, `-std=c17`, `-target`, `-ffreestanding`, `-O0`/`-O1`, response files | Dependency output, color policies beyond never, other standards/levels, hosted linking rejected |

`--parse-only` accepts the parser grammar without asserting semantic validity.
`-fsyntax-only` includes semantic validation and therefore accepts only the compiler
subset above. `-O0` and `-O1` have identical codegen for constant-return functions;
constant evaluation is required semantic work. `-c` requires explicit `-o`; `-S`,
`-E`, and `--emit-ir` default to stdout. Multiple input files and conflicting modes
are invalid usage. No shell subprocess compiler is used.

Preprocessing preserves line breaks within one source but includes/splices shift
coordinates; parser/lowering diagnostics identify a separate `<preprocessed>`
source buffer. Exact original-source macro/include provenance mapping remains
pending. Macro expansion is bounded at depth 128 and 16 MiB per line; include
depth at 64 and total output at 64 MiB. Include cycles are diagnosed, including
recursive includes that would otherwise depend on guard handling.

Build uses the checked-in grammar without regeneration. `MLVM_ENABLE_CEPHYR=AUTO`
finds the vendored runtime or a system runtime supporting grammar ABI 15; if absent,
Cephyr is disabled. Explicit `ON` fails clearly on absent dependencies. The
frontend remains a separately linkable library; its APIs do not depend on tool
executables. Regression tests cover positive/negative preprocessing, syntax,
overflow, unsupported semantics, duplicate definitions and native exit status 42.
