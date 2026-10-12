# The SymAS Binary Format

SymAS binary format, with the magic `0xCEEB09` and extension `.symas`, is a
binary databank used by assembler software and frameworks to obtain information
about how a particular architecture's assembly language works. If an assembly
dialect has multiple syntaxes, as is the case with x86-64 (Intel and
GAS/AT&T), the SymAS format can provide alternative syntaxes in the same
encoded database.

SymAS databanks are compressed to reduce their size. A databank may contain a
large amount of information, especially when it describes several instruction
sets, syntaxes, object-file formats, and relocation models.

The SymAs specs are in `compilers/stdspec`.

A SymAS databank can contain:

- Mnemonics
- Opcodes
- Instruction forms
- Operand definitions
- Encoding functions written in an Algol-like language, parsed using `domlibs/domdsl`'s combinatory parser
- Binary target information
- Linking information
- Section information
- Relocation information
- Syntax information
- Register and register-class information
- Feature and architecture requirements
- Instruction-selection records
- Scheduling records
- Functional-unit (pipeline resource) records
- Per-model machine descriptions
- Per-model optimization hints
- Instruction-pair fusion records
- Diagnostic and display metadata

The format is intended to describe an assembler dialect rather than to impose
one. Most assembly dialects are unofficial, project-specific, or internally
defined. A producer may therefore define its own instruction names, syntax
rules, encoding functions, sections, relocation types, and linking conventions.

The format also separates *encoding* from *optimization*. Everything listed
above describes how bytes are produced and parsed. The optimization sections
(`ISEL`, `SCHED`, `UNITS`, `MODELS`, `TUNING`, `FUSION`) describe how a
producer *prefers* to be used: which form an operation should lower to, what
that form costs, how long it takes, which pipeline resources it occupies, which
CPU models exist, what each model changes, and which instruction pairs may be
fused. A bank that carries none of those sections is still a complete
description of an assembler dialect; it simply says nothing about
optimization.

## General Structure

A SymAS file consists of a compressed payload preceded by a small file header.
The header identifies the file as a SymAS databank and provides enough
information for a reader to validate and decompress the payload.

A conceptual file has the following structure:
```text
+-------------------+
| File header       |
+-------------------+
| Compressed body   |
+-------------------+
```
The compressed body contains the databank sections. Readers should use the
lengths and offsets recorded by the format rather than assuming that sections
occur at fixed positions or that their order is permanently fixed.

Unless a field is explicitly documented otherwise, integer fields should be
treated as unsigned, and multi-byte values should be interpreted according to
the endianness recorded in the file header or container metadata.

## File Header

The file header identifies the container and describes the compressed body.

A typical header contains:

```text
magic
format version
header flags
compression identifier
uncompressed size
compressed size
optional checksum
```
### Magic

The SymAS magic value is:

```text
0xCEEB09
```
The magic is a file-identification value, not an instruction-set identifier.
Readers should reject a file whose magic does not match.

Because the magic occupies three bytes, implementations must define the byte
order in which it is stored. Readers should not search for the value as an
unaligned native integer without first accounting for the file's byte order.

### Format Version

The version identifies the layout and semantics of the databank. A reader
should distinguish between:

- versions it fully understands;
- versions that are compatible with the reader;
- versions that must be rejected.

Unknown mandatory fields, section types, or flags should cause a controlled
failure rather than being silently ignored.

### Header Flags

Header flags can describe properties such as:

- byte order;
- presence of an index;
- presence of checksums;
- presence of optional sections;
- whether offsets are absolute or body-relative;
- whether strings are stored in a shared table.

Unknown flags should be ignored only when the format explicitly marks them as
optional. Unknown flags that alter the interpretation of existing fields must
cause the file to be rejected.

### Compression

The body is compressed as a single payload or as a set of independently
compressed sections, depending on the format version and compression flags.

A reader must not infer the compression algorithm solely from the filename. The
compression method should be identified by the header or by the container
metadata.

The original implementation may use a ZLib-compatible stream. Implementations
that use another compressor, such as Zstandard, must identify it explicitly and
must not label the stream as ZLib. A library may support multiple compression
methods, but the file must remain unambiguous.

## Decompressed Databank

After decompression, the databank contains a section directory followed by
section data.

A conceptual layout is:

```text
+-------------------+
| Databank header   |
+-------------------+
| Section directory |
+-------------------+
| String table      |
+-------------------+
| Target sections   |
+-------------------+
| Instruction data  |
+-------------------+
| Syntax data       |
+-------------------+
| Linking data      |
+-------------------+
| Optional sections |
+-------------------+
```
The exact order may vary. A section directory allows readers to locate sections
without depending on a fixed order.

## Section Directory

The section directory maps section identifiers to their locations and sizes.

Each entry may contain:

```text
section identifier
section flags
file or body offset
compressed size
uncompressed size
alignment
optional checksum
```
Section identifiers should be stable numeric values or equivalent names. A
reader should skip unknown optional sections using their recorded size.

Typical section identifiers include:

```text
STRINGS
TARGET
REGISTERS
FEATURES
MNEMONICS
OPCODES
OPERANDS
ENCODERS
SYNTAX
SECTIONS
LINKING
RELOCATIONS
INDEX
DEBUG
ISEL
SCHED
UNITS
MODELS
TUNING
FUSION
```

Identifiers 15 through 20 are the optimization sections. They are marked
optional in the section directory, which means that a reader which does not
recognize an identifier may skip the section using its recorded size instead of
rejecting the file. A reader that does recognize them should use them; it must
not assume that they are present, because a bank is free to omit all of them.
The `INDEX` section, when present, provides fast lookup from names or numeric
identifiers to records. It is an optimization and should not change the
meaning of the underlying records.

## String Table

Names and textual values are generally stored in a shared string table to avoid
duplicating the same text throughout the databank.

A string reference may be represented by:

- a byte offset;
- a string-table index;
- a length-and-offset pair.

The empty string should have a stable representation. References outside the
string table are invalid and must not be treated as empty strings.

Strings may include:

- mnemonic names;
- register names;
- syntax keywords;
- section names;
- relocation names;
- feature names;
- diagnostic messages;
- encoding-function source text.

String data is normally UTF-8, although assembler identifiers are often
restricted to ASCII by the consuming tool.

## Target Information

The target section describes the binary target for which the databank is
intended.

It may include:

- architecture name;
- machine or subarchitecture;
- address size;
- default operand size;
- default address size;
- instruction alignment;
- byte order;
- object-file format;
- ABI identifier;
- calling convention;
- feature set;
- valid mode or execution level.

For example, a target could distinguish between:

```text
x86-64
i386
AArch64
ARM
RISC-V RV64
MIPS32
```
Target information is descriptive. It does not necessarily imply that the
SymAS library can emit a complete executable for that target.

## Features

Features represent optional architectural capabilities or execution modes.

Examples include:

- instruction-set extensions;
- privilege levels;
- vector extensions;
- floating-point support;
- compressed instruction support;
- vendor-specific extensions;
- operating-system-specific conventions.

Instruction forms may reference required or excluded features. A consumer can
use these references to reject an instruction that is unavailable for the
selected target.

Feature identifiers should be stable within a databank. Names should be used
for display and configuration, while numeric identifiers should be used for
references between binary records.

## Registers

The register section describes the registers accepted by the target's
instruction forms.

A register record can contain:

```text
register identifier
display name
register class
width
encoding number
parent register
subregister range
flags
```
Register classes may include:

- general-purpose registers;
- segment registers;
- control registers;
- debug registers;
- vector registers;
- mask registers;
- floating-point registers;
- status registers;
- special-purpose registers.

Subregister relationships allow a consumer to represent registers such as a
32-bit view of a 64-bit register without duplicating the entire register
definition.

A register record may also specify whether the register is:

- readable;
- writable;
- implicitly used;
- implicitly modified;
- valid only in a particular mode.

## Mnemonics

A mnemonic record maps a textual instruction name to one or more instruction
forms.

A mnemonic is not necessarily a complete instruction description. Several
forms may share the same mnemonic while differing in:

- operand count;
- operand types;
- operand sizes;
- encoding;
- required features;
- syntax;
- implicit operands;
- relocation behavior.

For example, a single mnemonic such as `mov` may refer to register-to-register,
register-to-memory, immediate-to-register, and immediate-to-memory forms.

A mnemonic record may contain:

```text
mnemonic identifier
canonical spelling
aliases
case-sensitivity flags
instruction-form references
```
Aliases allow a database to expose alternate names without duplicating the
instruction encoding.

## Opcodes and Instruction Forms

An opcode record describes one encodable instruction form. It connects the
human-readable mnemonic to its operands and encoding function.

An instruction form can contain:

```text
instruction-form identifier
mnemonic identifier
operand count
operand descriptors
required features
forbidden features
encoding-function reference
syntax-form references
instruction flags
```
The term “opcode” refers to the complete instruction form, not necessarily to
one literal byte sequence. Some forms have multiple encodings depending on
operand size, register number, addressing mode, or immediate value.

Instruction flags may indicate:

- privileged instruction;
- branch or call;
- conditional branch;
- return;
- terminator;
- may trap;
- reads memory;
- writes memory;
- has a delayed effect;
- has implicit operands;
- supports relocation;
- supports aliases.

These flags are useful to disassemblers, syntax highlighters, analyzers, and
linkers.

## Operand Definitions

Operand descriptors define the operands accepted by an instruction form.

An operand descriptor may specify:

```text
operand position
operand kind
operand width
allowed register classes
immediate range
memory-address constraints
signedness
encoding role
read/write access
```
Common operand kinds include:

- register;
- immediate;
- displacement;
- memory;
- absolute address;
- relative address;
- symbol;
- condition code;
- register list;
- implicit operand.

Operands can also be constrained by size relationships. For example, an
instruction may require two operands to have the same width, or require an
immediate value to fit within a signed eight-bit field.

Implicit operands are represented in the same model as explicit operands but are
marked as hidden from the source syntax.

## Encoding Functions

Encoding functions describe how an instruction form is converted into machine
code. They are written in an Algol-like language defined by the SymAS
implementation. This language is parsed using `domlibs/domdsl/DomDSL.hpp`'s combinatory parser.

An encoding function may:

- emit literal bytes;
- append bit fields;
- select prefixes;
- encode registers;
- encode addressing modes;
- emit immediates;
- emit displacements;
- calculate instruction-relative values;
- create relocation records;
- select a short or long form;
- validate operand constraints.

Conceptually, an encoding function may look like:

```text
emit8(0x48);
emit8(0x89);
emit_modrm(dst, src);
emit_relocation(symbol, REL_ABS32);
```
The exact syntax and available operations are defined by the encoding-language
runtime, not by the container itself.

Encoding functions should be treated as data with a well-defined execution
environment. A consumer must not execute arbitrary encoding source without
applying suitable limits to:

- execution time;
- memory use;
- recursion depth;
- output size;
- number of generated relocations.

An encoding function should report invalid operands as an encoding error rather
than emitting partially valid machine code.

## Syntax Information

The syntax section describes how instruction forms are parsed and printed.

A single instruction form may have several syntax descriptions. This supports
dialects such as:

```text
Intel:
mov rax, rbx

GAS/AT&T:
movq %rbx, %rax
```
A syntax description may include:

- syntax identifier;
- operand order;
- mnemonic aliases;
- register prefixes;
- immediate prefixes;
- comment delimiters;
- memory-expression grammar;
- separator rules;
- size suffix rules;
- decoration rules;
- implicit operand visibility;
- parser precedence;
- printer templates.

Syntax information should be separate from encoding information. Two syntaxes
may encode the same instruction form while differing entirely in spelling,
operand order, decorators, or punctuation.

A syntax record may also define whether names are:

- case-sensitive;
- case-insensitive;
- reserved;
- accepted as aliases.

## Sections Information

The sections section describes the logical sections available to an assembler or
object writer.

A section record may contain:

```text
section identifier
name
flags
default alignment
address space
content type
ordering constraints
```
Common section properties include:

- executable;
- writable;
- allocatable;
- read-only;
- zero-initialized;
- mergeable;
- discardable;
- TLS-related;
- debug-only;
- code;
- data;
- metadata.

Examples of section names include:

```text
.text
.rodata
.data
.bss
.tdata
.tbss
.debug_info
```
Section information is target- and object-format-dependent. A name such as
`.text` may have different flags or alignment requirements under different
object formats.

## Linking Information

Linking information describes how symbols and sections participate in object
file generation and linking.

It may include:

- symbol binding;
- symbol visibility;
- symbol type;
- local and global symbol rules;
- weak symbols;
- common symbols;
- undefined symbols;
- section-relative symbols;
- symbol aliases;
- linkage names;
- procedure or object metadata.

A linker-facing record may distinguish between:

```text
local
global
weak
undefined
absolute
common
thread-local
hidden
protected
```
The SymAS databank describes the target's conventions. It does not replace an
object-file format such as ELF, COFF, Mach-O, or another platform-specific
format.

## Relocation Information

Relocation records describe values that cannot be finalized until symbols or
section addresses are known.

A relocation type may specify:

```text
relocation identifier
name
field width
field offset
signedness
base value
PC-relative behavior
symbol requirements
overflow rules
addend behavior
```
Common relocation expressions include:

```text
S + A
S + A - P
GOT(S) + A
PLT(S) + A - P
TLS(S) + A
section(S) + A
```
Where:

```text
S = symbol value
A = addend
P = place being relocated
```
A relocation record should also state whether the addend is:

- stored explicitly in the relocation entry;
- stored in the instruction or data field;
- implied by the relocation type.

Relocation overflow must be diagnosed. Truncating a value that does not fit
the relocation field can produce a binary that assembles successfully but fails
at link time or at runtime.

## Instruction Selection

The `ISEL` section records how a producer wants a target operation to be
lowered. A record answers one question: under these conditions, use this
instruction form.

An instruction-selection record can contain:

```text
record identifier
record kind
operation name (the target node or pseudo-instruction name)
selected form identifier
cost
added complexity
priority
flags
required features
forbidden features
alternative form identifiers
source pattern text
```

The record kind distinguishes a single-instruction mapping from a tree
pattern, a pseudo instruction, a post-isel expansion, a conditional-select
preference, a register-copy class, and an aggressive rewrite. Kinds are hints
about *how* the mapping was produced; a consumer may treat them all as
"operation to form".

Cost and added complexity are separate on purpose. Cost expresses how expensive
the form is on the selected model; added complexity expresses how strongly the
form should be preferred when several forms match. Priority is a producer
ordering hint and breaks remaining ties. A consumer that ignores all three and
simply takes the first applicable record in producer order is still correct as
long as the producer lists alternatives best-first.

Required and forbidden features gate a record exactly as they gate a form. This
lets a producer express "use the AVX-512 form when AVX-512 is available and the
short form otherwise" as two records for the same operation.

The optional pattern text is the producer's source pattern in whatever notation
it uses. It is documentation, not an input to any required interpretation.

## Scheduling

The `SCHED` section records what one instruction form costs on a machine. A
record is attached to a form, and describes:

```text
record identifier
form identifier
schedule-class name
write latency in cycles
micro-ops issued
resource cycles occupied
flags
write resource identifiers
read resource identifiers
```

Latency is the number of cycles until the produced value can be consumed. A
latency of `0xFFFFFFFF` means the latency is variadic, that is, it depends on
the operands (a divide, or a load with a folded address computation). A
producer that cannot determine a latency should use zero rather than
guessing; a consumer that receives zero should fall back to the model's load
latency for a memory form and to one otherwise.

Flags distinguish variadic latency, multi-cycle occupancy, serializing
operations, scheduling barriers, faulting operations, operations with side
effects, forms that have a fused counterpart, and forms that execute as a nop
when their predicate is false.

At most one scheduling record may exist for a form. A consumer that needs
per-model numbers should resolve a schedule for a model, which applies the
`TUNING` overrides described below to the base record.

## Functional Units

The `UNITS` section describes the resources that scheduling records refer to.
A unit record contains:

```text
record identifier
owning model identifier, or none for a target-wide unit
unit name
unit kind
unit count
default latency
reciprocal throughput
flags
```

A unit is an execution port, a functional unit, or a pipeline of the kind
found in machine descriptions: integer, floating point, load, store, branch,
vector, mask, divider, multiply, complex, or pipe. The count is how many
identical units the model has, which is what a throughput calculation needs
when a form may issue on more than one of them.

Units with no owning model apply to every model of the target. A consumer
looking up the resources of one model should consider both the target-wide
units and the units that name that model.

## Per-Model Machine Descriptions

The `MODELS` section describes the CPUs, cores, or configuration points that a
producer distinguishes. A model record contains:

```text
record identifier
model name
vendor or family name
issue width
micro-op (reorder) buffer size
loop micro-op buffer size
load latency
branch mispredict penalty
high-latency threshold
high micro-op-count threshold
flags
enabled features
tuning-only features
description
```

The issue width is how many instructions, or micro-ops, the model dispatches
per cycle. For a VLIW or a packet machine this is the number of slots. A buffer
size of zero conventionally means the model is in order and has no reorder
buffer. Load latency assumes a first-level cache hit. The mispredict penalty is
the number of extra cycles a mispredicted branch costs.

Flags describe execution style: in-order, out-of-order, post-register-allocation
scheduling, a complete model (every form is described), prefer-post-RA, no
itineraries, VLIW, predication, a morphing core, known stalls, operand
forwarding, bundle-width issue, and the flag that marks the model a consumer
should assume when nothing else is specified.

The feature list is the architectural feature set the model provides. The
tuning-feature list is the set of tuning knobs that the model enables, such as
whether macro-fusion is available. Both reference features by identifier, so a
consumer can compare a model against the features a program actually uses.

## Per-Model Optimization Hints

The `TUNING` section records how a model changes a decision. A hint contains:

```text
record identifier
model identifier
hint kind
hint name
subject form identifier
second form identifier
numeric value
flags
required features
description
```

The hint kind gives the value its meaning: a latency override, a micro-op-count
override, a selection-cost override, a preference increase, an avoidance, a
threshold such as an unroll or partial-register-merge limit, a vector width, a
pair preference, a scheduling strategy, a partial unroll, or a split. A hint
with no form applies to the model as a whole.

Preferences and avoidance act on instruction selection: an avoidance removes the
form from the candidate set for that model, and a preference raises its
priority. Latency, micro-op-count, and cost hints override the corresponding
value in a base scheduling or selection record. When several hints of the same
kind apply to the same subject, the last record wins.

Flags distinguish a hint that only applies when its feature list holds, a hint
that replaces rather than adjusts, an experimental hint, a disabling hint, and
a negative value.

## Instruction-Pair Fusion

The `FUSION` section records pairs of forms that a producer may combine into a
single operation. A fusion record contains:

```text
record identifier
pair name
first form identifier
second form identifier
pair kind
fused latency
flags
required features
predicate summary
```

The pair kind distinguishes a generic macro-fusion pair from the specific
shapes that processors implement: compare and conditional branch, load and
branch, memory and arithmetic, multiply and add, a shift pair, a move and an
ALU operation, a load and an operation, a compare and a select, a vector
configuration and a user operation, and a target-specific pair.

Flags record whether the fusion is profitable, whether it requires a flag
dependency, whether it requires the same register, whether the two operations
must be adjacent, whether it is lock eligible, whether the pair must not be
unfused again, and whether either order is valid.

A consumer that does not implement fusion should ignore this section entirely.
Fusion never changes the meaning of the encoded instructions: a fused pair still
encodes as the two forms it names.

## Optional Sections and Versioning

The six optimization sections were added after the initial layout. The payload
layout of every pre-existing section is unchanged, so the databank version
number is unchanged; a bank containing the new sections requires a reader that
knows section identifiers 15 through 20.

Unknown section identifiers must be skipped, not rejected, when the directory
marks the section optional. A section that a reader does not understand and
that is not marked optional is a hard error, because ignoring it could change
the meaning of the data.

## Indexing and Lookup

A databank may include indexes for efficient lookup by:

- mnemonic;
- opcode identifier;
- register name;
- feature name;
- syntax name;
- relocation name;
- section name;
- machine-model name;
- instruction-selection operation name;
- functional-unit name;
- schedule-class name;
- fusion-pair name.

Indexes are optional. A reader that does not understand an index must still be
able to scan the underlying records, provided the required sections are
present.

When duplicate names exist, lookup should return all matching records rather
than silently selecting one. The caller can then resolve the instruction using
operand count, operand types, target features, and syntax.

## Validation

A reader should validate a databank before exposing its records.

At minimum, validation should check:

- magic and version;
- compression method;
- compressed and decompressed sizes;
- checksums, when present;
- section bounds;
- integer overflow in offset calculations;
- string references;
- record references;
- instruction-form references;
- encoding-function references;
- feature references;
- relocation field widths;
- alignment requirements;
- instruction-selection form, feature and alternative references;
- scheduling form and resource references, and that no form has two
  scheduling records;
- functional-unit model references and kind values;
- machine-model issue widths and feature references;
- tuning model, form and feature references;
- fusion form and feature references.

Malformed input must not result in out-of-bounds reads, unbounded allocation, or
execution of an invalid encoding function.

Validation may be performed in two stages:

1. structural validation of the container and section directory;
2. semantic validation of cross-references and target-specific records.

## Compatibility

New optional sections and records should be added in a way that allows older
readers to skip them. Existing section identifiers and record meanings should
not be repurposed.

A format version should be increased when a change affects the interpretation
of existing bytes, changes required fields, or changes the behavior of an
existing encoding or relocation definition.

Producers should include a minimum reader version when a databank depends on
features unavailable in older implementations.

## DomSymAs

The DomSymAs library provides a substrate for reading, validating, querying,
and using SymAS databanks.

It is implemented in C++ and exposes a C ABI. Python bindings are provided
through Nanobind in:

```text
third_party/nanobind
```
The library can be used to:

- open and validate a databank;
- enumerate targets and syntaxes;
- look up mnemonics;
- resolve instruction forms;
- inspect operand constraints;
- evaluate encoding functions;
- generate machine-code bytes;
- produce relocation records;
- inspect sections and linking metadata;
- select a form for a target operation given the enabled features and a model;
- report the latency, micro-op count and pipeline resources of a form for a
  given model, with that model's overrides applied;
- look up machine models, functional units, tuning hints and fusion pairs;
- provide assembler and disassembler front ends.

The C ABI should use explicit ownership rules. Objects returned by the library
must document whether they are:

- owned by the caller;
- borrowed from the databank;
- valid only until the next library call;
- valid until the databank is closed.

Errors should be reported through stable error codes and, where appropriate,
an explanatory error string.

## Archiving and Compression

For archiving, the implementation relies on:

```text
domlibs/domarchive/zstd.hpp
```
This dependency should be distinguished from the compression method used by the
`.symas` file format itself. If the archive layer uses Zstandard while the
SymAS container uses ZLib, the two layers have separate responsibilities:

```text
archive/container layer -> Zstandard
SymAS payload layer      -> format-defined compression
```
If the `.symas` format is changed to use Zstandard directly, the file header
must identify Zstandard explicitly and the format documentation must no longer
describe the payload as ZLib-compressed.

## Minimal Reader Workflow

A minimal reader performs the following operations:

```text
1. Read and validate the file header.
2. Identify the compression method.
3. Decompress the payload.
4. Read the databank header.
5. Read the section directory.
6. Validate section bounds and references.
7. Load the string table.
8. Load target and feature information.
9. Load mnemonics and instruction forms.
10. Select a syntax.
11. Resolve operands.
12. Evaluate the encoding function.
13. Emit bytes and relocation records.
```
A disassembler follows the reverse path:

```text
1. Identify candidate instruction forms.
2. Decode the instruction bytes.
3. Resolve registers, immediates, and memory operands.
4. Select a syntax.
5. Apply syntax-specific formatting.
6. Render the mnemonic and operands.
```
## Extensibility

SymAS is intentionally extensible. A producer may add:

- vendor-specific instructions;
- custom syntax dialects;
- target-specific sections;
- nonstandard relocations;
- assembler directives;
- macro metadata;
- instruction semantics;
- disassembly hints;
- optimization or scheduling information.

Extensions should use new identifiers and should document their binary layout,
ownership rules, validation requirements, and compatibility behavior.

A consumer that does not understand an optional extension should skip it and
continue when possible. A consumer must reject an extension when ignoring it
would cause incorrect instruction encoding, relocation, linking, or parsing.

## Summary

A SymAS databank is a compressed, extensible description of an assembler
target. It separates the following concerns:

```text
target architecture
instruction forms
operand constraints
machine-code encoding
source syntax
sections
symbols and linking
relocations
machine models
instruction selection
scheduling and pipeline resources
per-model optimization hints
```
This separation allows several assembly syntaxes to share the same instruction
and encoding definitions. It also allows a single library to support multiple
architectures, object formats, and unofficial or project-specific assembly
dialects without requiring every dialect to conform to a universal assembler
standard.

