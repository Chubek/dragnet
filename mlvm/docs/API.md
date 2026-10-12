# MLVM API Contract

C++ APIs live in namespace `mlvm`; C symbols use `mlvm_` and opaque handles.
C headers must compile as C and C++. Every returned allocation or handle has
documented ownership, release operation, and failure behavior. Never expose
C++ standard-library types in the C ABI.

Use stable numeric IDs for externally visible entities. Avoid pointer-derived
IDs and nondeterministic iteration. Diagnostics are structured values and should
not be emitted directly by library internals; callers choose formatting and
color policy. APIs return explicit status/results for recoverable errors.

## Implemented headers and libraries

| Header | CMake target | Contract |
| --- | --- | --- |
| `mlvm/core.hpp` | `MLVM::Core` | Context, identifiers, source files, diagnostics, operation definitions |
| `mlvm/c_api.h` | `MLVM::Core` | Implemented C context create/destroy, version, string interning |
| `mlvm/ir.hpp` | `MLVM::IR` | Owned module/region/block/operation/value, verifier, parser/printer |
| `mlvm/pass.hpp` | `MLVM::IR` | Module passes, cached analyses, instrumentation, constant folding, canonicalization, DCE |
| `mlvm/optilisp/pass.hpp` | `MLVM::OptilispPass` | Optional Blackstone resolution, transactional integer kernel execution, custom host optimizer adapters |
| `mlvm/conversion.hpp` | `MLVM::IR` | Directed shortest-path full conversion with explicit legality |
| `mlvm/codegen.hpp` | `MLVM::Codegen` | Baseline x86 machine instruction encoder/decoder (`ret`, `nop`, `syscall`, `mov`, `add`/`sub` imm32, `push`/`pop`, near `call`/`jmp`) |
| `mlvm/object.hpp` | `MLVM::Object` | Object/target model, ELF/archive codecs, assembler/linker |
| `mlvm/binutils.hpp` | `MLVM::Binutils` | Optional owning ELF32/64 views, archive/index adapters, transactional edits, TOML relocation contracts, raw linking, string scanning |
| `cephyr/frontend.hpp` | `MLVM::Cephyr` | Optional preprocessing, Tree-sitter parsing, subset lowering/codegen |

Install with `cmake --install build --prefix PREFIX`; consumers use
`find_package(MLVM CONFIG REQUIRED)` and the targets above. Static Cephyr consumers
also require the configured Tree-sitter runtime when the system fallback was used.
`MLVM::Binutils` is available when `MLVM_ENABLE_BINUTILS` resolves to `ON`; its
static dependency archives are installed/exported with the package. Consumers
need no libstone, klib or tomlc99 headers. `BinaryObject`, `BinaryArchive` and
`BinaryImage` own all their data; section/symbol references are zero-based indexes.
Symbol sections `-1`/`-2` mean undefined/absolute. Copies are independent.
`transform_binary_object` verifies before and after and commits only on success.
`write_binary_archive` canonically serializes ELF members; `write_indexed_archive`
instead preserves raw `ArchiveMember` payload bytes exactly while creating a GNU
ELF symbol index. Reading an ELF archive applies cumulative expansion/count budgets.
Contract and image APIs never infer target encodings. `scan_binary_strings`
callback views are valid only during each callback; the callback may copy them.
Errors use the supplied `DiagnosticEngine` and optional/bool results. The
binary API currently has a C++ embedding surface; it adds no C ABI handles.

Context IDs are monotonically allocated, nonzero, context-local; interned spelling
references remain valid until context destruction. SourceManager owns source text
and assigns nonzero file IDs. Line/column coordinates are one-based byte positions.
Unknown/invalid locations are all-zero; EOF is a valid location. Diagnostics contain
severity, code, primary location, related notes and fix-it ranges. Formatting is
caller-controlled; libraries never print diagnostics.

Modules borrow their Context, which must outlive every module copy. Nodes and their
children are owned by value; copies are independent except for the shared Context.
Value and node IDs are module-local and stable through append/erase. Vector growth
or erasure invalidates C++ references into that vector; retain IDs across mutation.
`append`, `replace_uses`, and `erase` maintain explicit ordered use lists.
Manual operand edits require `rebuild_uses()`; verification rejects stale lists,
orphan definitions, reused IDs, unavailable operands and invalid operation arity.
`replace_uses` checks type compatibility; the caller must verify dominance afterward.

OperationDefinition declares operand/result/region bounds, effect, terminator and
an optional custom verifier. Custom type names are context-interned strings;
their semantics are the registering dialect's responsibility. Builtins are
independently named `builtin`, `arith`, `func`; `register_builtin_dialects` is
idempotent. Registration of duplicate explicit definitions fails diagnostically.
Custom verifier callbacks must handle malformed arity without unchecked indexing.

PassManager verifies its input and runs each module pass on an independent copy.
Failure, emitted error, exception, context change or verification error discards
that pass's edits; previous successful passes remain committed. Instrumentation
receives success, elapsed time, and opt-in deterministic before/after dumps.
Required analyses must be registered. Cached analyses are invalidated unless
explicitly preserved. Analysis values must be self-contained (IDs/data, never
pointers into a candidate module); producers are pure. Outside PassManager, use
one module per AnalysisManager and invalidate on mutation. Function/region/block
pass scheduling and analysis dependency tracking remain pending.

ConversionGraph uses breadth-first directed paths (edge-registration order breaks
equal-length route ties). Full conversion commits only if every resulting operation
belongs to a legal registered dialect and verifies. Each rewrite must produce its
edge's target dialect. Rewrites may edit the current operation, including its
operands/results/children, but must not structurally mutate sibling operations or
ancestors during traversal. No implicit reinterpretation or partial-conversion
mode is provided. Diagnostics persist after transactional failure.

C ABI functions catch exceptions, use explicit status codes, and reset output IDs
or handles on failure. `mlvm_context_destroy(NULL)` is valid. Interned strings are
borrowed, NUL-terminated; invalid IDs return NULL. API objects are externally
synchronized; concurrent mutation is not supported.
