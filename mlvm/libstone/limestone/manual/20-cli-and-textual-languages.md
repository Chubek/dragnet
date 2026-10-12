# Chapter 20. CLI and Textual Languages

[Previous: Python](19-python-bindings.md) · [Contents](README.md) · [Next: Targets and adapters](21-targets-backends-and-adapters.md)

## 20.1 One tool, explicit operation modes

`limestone-cli` exposes offline compilation, IL experiments, metadata inspection,
term optimization, binary translation, and object operations. Its authoritative
option behavior is in [limestone/main.cpp](../limestone/main.cpp).

```sh
build/limestone-cli --help
build/limestone-cli --version
```

Only one operation mode can be selected. With no mode, input is TraceML source
compiled to portable machine-oriented output. Most modes accept one positional
input file or `-`; omitted input means standard input. `-o`/`--output` selects a
file, with omitted output or `-` meaning standard output.

Success returns exit zero. Errors return nonzero and a `limestone:` diagnostic on
standard error. Text and binary output use the same output path mechanism; choose
files when producing raw code or ELF data.

## 20.2 Compilation controls

| Option | Meaning |
| --- | --- |
| `--evaluate` | Evaluate a closed integer source program |
| `--compile-umd` | Compile the graph in a UMD machine/program document |
| `--target-isa FILE` | Use explicit ISA target metadata for source/UMD compilation |
| `--selector global\|greedy\|burs` | Choose instruction selection |
| `--allocator linear\|greedy\|color\|constraint` | Choose allocation strategy |
| `--allocate` | Request physical allocation |
| `--encode` | Request backend bytes |
| `--object SYMBOL` | Package encoded compilation as ELF64 under the ISA object contract |
| `--machineir-json` | Emit the versioned region exchange |
| `--trace-execution` | Preserve source execution events and trace lowering |
| `--no-optimize` | Skip the configured graph optimization stage |
| `--no-schedule` | Skip cycle scheduling while preserving required order/contracts |

`--evaluate` is its own operation, not a pipeline stage modifier. Source tracing
requires TraceML compilation and is rejected for UMD graph compilation. Encoding
and exchange JSON are alternative output requests. `--object` requires compilation
with `--target-isa` and `--encode`.

```sh
build/limestone-cli --compile-umd --selector burs --allocate --allocator color \
  tests/fixtures/arithmetic-includes.umd
```

Skipping a stage does not infer its missing target facts. A compatible backend
and final allocation may still be required by encoding.

## 20.3 Metadata and standalone IL modes

| Mode | Input/output |
| --- | --- |
| `--isa FILE` | Parse/summarize the named ISA; no separate positional input |
| `--emit-umd FILE` | Normalize the named ISA to UMD machine text |
| `--select-burs` | Load rules/trees and emit selected Scheduler IR plus cost |
| `--analyze-burs` | Load rules/trees and emit states/rejection attempts |
| `--schedule-il` | Load Schedrow text, schedule, verify, and print issues |
| `--allocate-il` | Load RegTL units/functions and print assignments/spills |

`--modulo N` applies only to scheduling IL and requires a positive 32-bit initiation
interval. Allocation mode accepts its allocator choice. Pipeline-specific selectors
and flags are not general modifiers for all standalone modes.

UMD and BURS positional file paths use semantic file loaders with relative includes.
The corresponding stdin paths use text loaders, so implicit filesystem resolution
is not enabled. A rules-only BURS specification needs a tree document for the CLI
selection/analysis modes.

## 20.4 Tunah command options

`--optimize-term` consumes one S-expression and repeatable rule loads:

```sh
build/limestone-cli --optimize-term --rules vocabulary.tuner --rules rules.tuner \
  --op-cost multiply 20 --literal-cost 1 \
  --iterations 20 --node-limit 10000 --class-limit 10000 \
  --time-limit 100 --trace expression.term
```

This example assumes caller-provided declarations/rules/expression files.
`--op-cost OP COST` sets local nonnegative extraction cost; repeated declarations
of the same cost name are rejected. Limits configure iterations, nodes, classes,
and cooperative milliseconds. `--trace` writes admitted matches/statistics to
standard error, leaving the extracted expression on the selected output path.

Tunah options apply only to term mode. The CLI supplies no host proof predicates,
so guarded rule files requiring such analyses need an embedding application.
Zero iteration is extraction-only; positive node/class budgets remain required.

## 20.5 Binary and object modes

| Mode | Purpose |
| --- | --- |
| `--disassemble ISA` | Decode raw bytes to a listing |
| `--decompile ISA` | Lift raw bytes to semantic assembly |
| `--translate SOURCE TARGET` | Translate equivalent raw instruction forms |
| `--inspect-object` | Load/inspect an ELF64 relocatable object |
| `--wrap-object ISA SYMBOL` | Package raw code bytes as an object |
| `--link-objects ISA` | Resolve objects into an addressed raw image |

`--cache DIRECTORY` applies only to translation. CLI raw translation uses default
addresses; explicit source/target addresses are available through embedding APIs.
It does not consume linking's base-address option.

Linking accepts repeatable `--input-object FILE`, optional final positional object,
`--base-address N`, and repeatable `--external-symbol NAME N`. Duplicate external
names fail. Numeric CLI address arguments are nonnegative decimal integers.

```sh
build/limestone-cli --link-objects target.isa --input-object entry.o \
  --input-object helper.o --base-address 4096 -o image.bin
```

An addressed image is bytes plus a declared layout, not an executable mapping.
The command's target file supplies ELF identity and relocation formulas.

## 20.6 The specification generator CLI

The separate tool is:

```sh
build/limeburg-generate-specs metacode/infobank limeburg/specs
build/limeburg-generate-specs --check metacode/infobank limeburg/specs
```

It validates the manifest, generates every architecture specification, checks GLR
loading/canonical round trips, and produces `coverage.json`. `--check` is read-only.
Unexpected `.lburg` files outside the manifest and unrelated handwritten files
at generated destinations are diagnosed. The normal CMake regeneration target
invokes the same tool.

## 20.7 Ten textual syntax boundaries

Every grammar in `parsers/` has a matching `.absyn` schema:

| Grammar | Role | Example |
| --- | --- | --- |
| `isa.g` | ISA declarations and nested metadata | [isa.isa](../parsers/examples/isa.isa) |
| `traceml.g` | S-expression source forms | [traceml.trace](../parsers/examples/traceml.trace) |
| `tuner.g` | Post-preprocessing terms/rules | [tuner.tuner](../parsers/examples/tuner.tuner) |
| `limeburg.g` | Rules, categories, typed patterns, trees | [limeburg.limeburg](../parsers/examples/limeburg.limeburg) |
| `unisel.g` | UMD machines and source graphs | [unisel.umd](../parsers/examples/unisel.umd) |
| `schedrow.g` | Regions, dependencies, models, groups | [schedrow.schedrow](../parsers/examples/schedrow.schedrow) |
| `regtl.g` | Ranges, functions, constraints, transfers | [regtl.regtl](../parsers/examples/regtl.regtl) |
| `bin2bin.g` | Translation-description syntax | [bin2bin.bin2bin](../parsers/examples/bin2bin.bin2bin) |
| `limestone.g` | Pipeline/module description syntax | [limestone.limestone](../parsers/examples/limestone.limestone) |
| `machine-ir.g` | C++ machine-module syntax | [machine-ir.mir](../parsers/examples/machine-ir.mir) |

Parsing a language establishes a syntax tree. It does not promise a complete
semantic runtime or a CLI mode for every parsed construct. In particular,
Bin2Bin/pipeline/machine-module syntax APIs are distinct from the current raw
translation/compiler entry points. Tunah's semantic ingestion continues through
EkippX/SExprTk/DSLtk rather than using a generated syntax tree as its optimizer.

## 20.8 Lexical conventions and comments

Identifiers generally support underscores, dots, and hyphens. `%name` denotes a
virtual value/register or scheduling identity; `$name` denotes physical storage
in register-oriented languages. Pattern variables use `?name`, with Unisel also
accepting `$name`. Machine-module immediates use `#7` and symbols/CFG references
use `@name`.

ISA comments use `#`; TraceML/tuner comments use `;`. Brace-oriented grammars
accept `//` and non-nested `/* ... */`; several also accept `#`. RegTL reserves
`#` for immediate operands. Limestone/MachineIR use `;` line comments and do not
require semicolons on metadata assignments/instructions.

Quoted syntax tokens retain their spelling and escapes. Semantic loaders decode
them according to their language contract. ISA/JSON metadata uses checked JSON
escapes, including valid Unicode surrogate pairs; invalid UTF-8, malformed escapes,
and unescaped control characters are rejected by the corresponding loaders.

## 20.9 Owning syntax trees and visitors

Link `Limestone::parsers` and include `parsers/<grammar>_ast.hpp`. Namespaces are
`limestone::syntax::<grammar>`, with `machine-ir` normalized to `machine_ir`.

```cpp
#include <parsers/tuner_ast.hpp>

auto parsed = limestone::syntax::tuner::parse(
    "(operator add 2)(rule zero (add ?x 0) ?x)", "example.tuner");
if (!parsed) return report(parsed.error());
auto document = std::move(parsed.value());
```

Trees own token spellings, source names, and children. Every node has a source
span with exclusive-end byte positions, one-based lines/columns, and byte-counted
columns. Invalid syntax and unresolved ambiguity return source-located parse
errors. Grammar/schema cardinality mismatches return internal errors rather than
dropping syntax children.

Ordinary visitors dispatch one node without automatic recursion. Recursive
visitors traverse owning children in schema/source order. `enter` can prune a
subtree; `leave` provides postorder handling. Const walkers preserve child
constness. Mutable token edits do not rewrite original source spans.

## 20.10 Generation from `.g` and `.absyn`

`scripts/syngen.pl` generates C++ ASTs and visitors from the `.absyn` schema.
DParser generates the C parsing tables from `.g`. These sources remain the
authoritative pair; generated output lives under `build/generated/parsers/`.

```text
namespace example::syntax;
parser example;
root Document;
node Document { Declaration* declarations; }
sum Declaration = Definition | Import;
node Import { String path; }
token String;
```

This is a schema fragment, not a complete grammar/schema pair. Composite nodes,
sum variants, optional/repeated fields, and indexed captures map grammar
children to owning C++ fields. `--grammar` validates root and production coverage
before output replacement.

```sh
perl scripts/syngen.pl --grammar parsers/tuner.g --check parsers/tuner.absyn
cmake --build build --target limestone-generate-parsers
```

Generated code is reproducible and contains no timestamps or absolute input paths.
Update the grammar/schema and regenerate rather than editing output.

## 20.11 Parse and include budgets

`ParseLimits` defaults to 16 MiB input, one million parse-tree nodes, and depth
512. Byte/NUL checks precede DParser; tree node/depth checks occur after its tree
is complete and before owning AST construction. These bounds do not preempt
time or allocation inside the GLR engine.

UMD/BURS semantic includes have separate cumulative defaults: 16 MiB, 128 document
occurrences, and 32 levels. `ResolvedSource` owns stable identity and text;
`IncludeOptions::resolver` controls virtual or filesystem resolution. Retain
source identity for cycles and provenance rather than using pointer addresses.

Tunah terms and MachineIR exchange have their own 4 MiB/256-level boundaries.
Choose the limit at the actual input interface, rather than assuming one parser
setting governs every subsystem.

## 20.12 Reproducible textual workflows

Keep canonical target/rule/IL output alongside the original source when
investigating a failure. Verify semantic reloads, not just syntax acceptance.
Keep text listings distinct from complete owning exchanges and final byte output.

The CLI is a convenient driver for the public APIs. Embedded applications gain
explicit callbacks, addresses, inspection, and target adapters where a command
mode intentionally exposes a smaller offline workflow.

[Previous: Python](19-python-bindings.md) · [Contents](README.md) · [Next: Targets and adapters](21-targets-backends-and-adapters.md)
