# Dragscript Agent Guide (`dragscript/AGENTS.md`)

This file is the working contract for humans and LLM agents implementing **Dragscript**, an embedded-first TypeScript/JavaScript engine for Dragworks, the headless browser in the Dragnet infrastructure.

Dragscript uses **MLVM** (the sibling `mlvm/` project) as compiler/interpreter infrastructure and **Tree-sitter TypeScript** as its syntax frontend. The intended grammar/runtime sources are in `third_party/tree-sitter` and `third_party/tree-sitter-typescript` (the repository's actual spelling is `third_party`; do not copy the occasional misspelling `third_patry`/`third_paty` into paths).

The most important principle is: **build a small, semantically explicit, embeddable language engine in vertical slices. Do not mistake parsing, IR verification, or successful lowering for JavaScript/TypeScript conformance.**

---

## 1. Mission, priorities, and non-goals

### Product goal

Dragscript is an implementation of the TypeScript language family intended primarily to execute page scripts inside Dragworks. It should be usable as a library embedded in a host process, not merely as a standalone command-line program. Dragworks should be able to create an engine/runtime instance, compile or evaluate source, provide host capabilities, execute code under resource limits, receive structured diagnostics, and destroy the instance without leaking process-global state.

Prioritize these qualities in this order:

1. **Correctness and safety of the supported subset.**
2. **A stable, small embedding API and clean host/runtime boundary.**
3. **Predictable resource use and deterministic behavior where practical.**
4. **Useful source diagnostics and debuggability.**
5. **Performance measured on realistic Dragworks workloads.**
6. **Breadth of language compatibility, expanded deliberately.**

### Initial scope

Build the engine around a clearly documented subset that grows over time. The first vertical slice should be capable of:

- Parsing `.js`, `.jsx` only if deliberately enabled, `.ts`, and `.tsx` with the correct Tree-sitter grammar entry points.
- Reporting syntax errors with file/line/column and ranges.
- Lowering a small, explicit subset of expressions and statements into a Dragscript-owned semantic representation and then MLVM IR.
- Executing a minimal set of values, arithmetic, local variables, conditionals, function calls, and returns.
- Calling host-provided functions through a controlled embedding API.
- Enforcing execution limits and reporting runtime errors.
- Running tests without requiring a browser, GUI, network, or system-installed TypeScript toolchain.

Do not advertise general TypeScript compatibility until the corresponding semantics are implemented and tested. A Tree-sitter parse tree is syntax recognition, not type checking, JavaScript semantics, or executable code.

### Explicit non-goals unless separately approved

- Implementing all of TypeScript's compile-time type checker.
- Treating TypeScript type annotations as runtime values.
- Running arbitrary native code generated from untrusted input without a sandbox boundary.
- Depending on Node.js, V8, Deno, Bun, or an external `tsc` binary at runtime.
- Assuming browser DOM APIs exist inside the core engine.
- Implementing the entire web platform in Dragscript. Dragworks should provide browser/DOM/Fetch-like host APIs through separate components.
- Using MLVM's machine-code/object pipeline as a substitute for defining JavaScript value and execution semantics.
- Claiming full ECMAScript conformance from a limited interpreter or a handful of passing tests.

If a product requirement conflicts with these principles, document the trade-off and get an explicit design decision rather than silently expanding scope.

---

## 2. Repository and dependency orientation

Before changing code, inspect the repository rather than assuming its layout. Find the real source roots, CMake targets, public headers, tests, and existing conventions. In particular, inspect:

- `mlvm/manual/README.md` and the relevant chapters of `mlvm/manual/`.
- MLVM headers and tests referenced by those chapters.
- `third_party/tree-sitter` and `third_party/tree-sitter-typescript`, including their license, version, generated parser sources, headers, and build instructions.
- Existing top-level CMake conventions and third-party target names.
- Existing Dragnet/Dragworks host interfaces, if present.

Do not create a second copy of a dependency that already exists in `third_party/`. Do not regenerate or vendor generated grammar files without a concrete reason. Prefer using the checked-in Tree-sitter grammar sources and runtime version already pinned by the repository.

Useful dependencies already listed in the repository include:

- `tree-sitter`, `tree-sitter-typescript`: parser runtime and TypeScript/TSX grammars.
- `quickjs`: useful as a **reference implementation and differential-test oracle** for JavaScript behavior where available; do not accidentally make it Dragscript's runtime if the objective is to implement Dragscript on MLVM.
- `re2`: a possible regex engine if its supported syntax and semantics are appropriate; JavaScript RegExp compatibility is not automatic.
- `simdjson`: useful only if a JSON integration requires it; do not confuse JSON parsing with JavaScript object semantics.
- `libuv`, `libev`, `uvwasi`, `wasmtime-cpp`: potentially useful for host integration or optional execution environments, but do not make the core runtime depend on an event loop or WASM unless the architecture calls for it.
- `fmt`, `spdlog`: possible diagnostics/logging dependencies if consistent with repository conventions.
- `lmdbxx`, Tokyo Cabinet, zstd, and other storage/utility libraries are not core language-runtime dependencies without a demonstrated need.

Keep the minimum viable Dragscript dependency graph small. Do not pull in a library merely because it is available.

### Path spelling and source of truth

Use the actual repository path spelling `third_party/tree-sitter-typescript`. If the checkout uses a different directory name, verify it and follow the checkout. Never infer that a dependency exists based only on this document.

Treat MLVM's installed/manual documentation and headers in the current checkout as authoritative. If a manual example disagrees with the API, verify the declaration and tests, then update Dragscript code to the real API. Do not assume APIs described as planned or future work are implemented.

---

## 3. Architecture at a glance

Keep these layers separate. The exact directory names can evolve, but the responsibilities should not be collapsed into one large source file.

1. **Embedding/API layer** — stable public C++ API and, if needed, a narrow C ABI.
2. **Engine/session layer** — per-engine configuration, host bindings, limits, module cache, and lifecycle.
3. **Source/diagnostics layer** — owned source buffers, source maps, structured errors and spans.
4. **Frontend layer** — Tree-sitter parser setup, grammar selection, syntax-tree traversal, syntax validation.
5. **TypeScript erasure / static validation layer** — remove or interpret type-only syntax, validate unsupported constructs, and optionally implement a deliberately bounded subset of type checking.
6. **Dragscript semantic IR (DSIR)** — a frontend-owned, typed or tagged representation for language semantics that MLVM's generic IR cannot express directly.
7. **Lowering layer** — DSIR to MLVM IR with explicit operation/type contracts.
8. **Execution layer** — an interpreter/VM or other execution backend that defines runtime behavior. Initially favor a debuggable interpreter over premature machine-code generation.
9. **Host capability layer** — controlled imports/calls to Dragworks services such as DOM, timers, networking, console, and events.
10. **Module/resolution layer** — source modules, imports/exports, cache identity, and policy-controlled resolution.
11. **Testing/conformance layer** — parser, semantic, runtime, embedding, security/resource, and integration tests.

A typical compile/evaluate flow should look like:

```text
source bytes
  -> owned source record + source ID
  -> Tree-sitter parse (TS or TSX grammar)
  -> syntax/error-node validation
  -> TypeScript-only syntax handling / erase type-only nodes
  -> supported-subset validation
  -> DSIR construction and semantic checks
  -> MLVM IR lowering + dialect verification
  -> executable artifact or DSIR/IR interpreter input
  -> execution with per-instance environment and limits
  -> structured result / exception / diagnostics
```

Compilation and execution should be independently callable. A host should be able to compile once and execute more than once when semantics permit, and should be able to inspect diagnostics without starting execution.

### Why DSIR is necessary

MLVM is general compiler infrastructure, not a prebuilt JavaScript engine. Its generic SSA IR has interned string type identifiers, operation names, string attributes, explicit result IDs, regions/blocks, and use lists. That representation does not by itself supply JavaScript's dynamic values, object identity, prototypes, property access rules, coercions, exceptions, closures, or async scheduling.

Use DSIR as a semantic bridge. It can preserve source-level constructs and provenance while the implementation chooses how to execute them. Do not force every source construct into an MLVM operation before the necessary semantics are defined. DSIR may be a tree/graph with explicit lexical scopes and control-flow nodes; MLVM IR can represent the lower-level subset that has a valid mapping.

---

## 4. MLVM integration rules

Read the applicable MLVM manual chapters before using an API: architecture, contexts/ownership, diagnostics, SSA model, textual IR, builtin dialects, verification, C++ IR, custom dialects, conversion, passes, NodeIR, and testing/extending.

### 4.1 Context and ownership

- `mlvm::Context` owns interned spellings and dialect/operation registrations. It must outlive every `mlvm::Module` that borrows it.
- Declare the context before modules in a scope so destruction order is safe.
- Context identifiers are local to a context. Never serialize or compare raw IDs across contexts as if they were globally meaningful.
- Module/node/value IDs are a separate identity space from context intern IDs.
- MLVM API objects are externally synchronized. Do not mutate one module or shared registry concurrently without explicit synchronization.
- Avoid retaining references or pointers into block/operation vectors across append/erase operations; vector growth may invalidate them. Save stable IDs and reacquire references.
- Analyses and caches should own the data they need or retain stable IDs, not pointers into a candidate module that may be discarded by a transaction.
- Do not expose MLVM-private structures as Dragscript's public ABI unless that coupling is a deliberate, documented choice.

### 4.2 Dialect registration

Register required dialects before parsing or constructing IR. The builtin registration entry point documented by MLVM is:

```cpp
mlvm::register_builtin_dialects(context, diagnostics);
```

This covers the builtin dialect families `builtin`, `arith`, and `func`. Check its return value and render diagnostics on failure. Registration is documented as idempotent.

Create a Dragscript-owned dialect namespace for language-specific operations, for example `ds.*` or `dragscript.*`; choose one spelling and keep it stable. Register the dialect and every operation definition explicitly. Do not assume that registering a type spelling gives it layout, arithmetic, ABI, or conversion semantics.

For each operation definition, specify:

- Operand/result/region cardinality.
- Whether it is a terminator.
- Effect classification (`pure`, `read`, `write`, or `unknown`) justified by semantics.
- Required attributes and type relationships in a verifier callback.
- Which invariants must hold before execution or lowering.
- Whether the operation can trap, throw, allocate, mutate state, invoke host code, or suspend.

Never mark an operation pure merely to make dead-code elimination convenient. An incorrect effect classification can cause observable work to be deleted. Dynamic property access, calls, coercions that can invoke user code, host calls, and allocation should be conservative until their effects are modeled.

### 4.3 SSA constraints and control flow

MLVM's generic SSA model has explicit value identity and use lists. Its current generic verifier does **not** provide CFG successor edges or general multi-block dominance. A module with multiple blocks is not automatically a complete control-flow graph.

Therefore:

- Do not model JavaScript `if`, loops, `break`, `continue`, `switch`, `try/catch/finally`, or exception edges as if MLVM already had a complete CFG/dominance model.
- For an early interpreter, represent control flow in DSIR or in an explicit, verified Dragscript control-flow structure.
- If lowering to MLVM blocks, define and test the exact control-flow encoding and verifier guarantees. Do not claim general dominance correctness without implementing or using a representation that checks it.
- Use explicit terminators and well-defined return behavior.
- Ensure all operand IDs refer to valid definitions, uses are rebuilt after direct operand-vector edits, and the module verifies after each lowering stage.
- Prefer MLVM's append/replace/erase APIs where applicable; if direct edits are unavoidable, call `rebuild_uses()` and verify before consuming the result.
- A textual IR round trip is useful for debugging but does not preserve all source provenance and is not a semantic equivalence proof.

### 4.4 Type representation

MLVM's generic type is a context-interned spelling, not a rich structural type object. Start with a small explicit mapping, e.g.:

- `i1` only for a deliberately defined low-level boolean representation.
- `i32`/`i64` only for proven integer-specific operations.
- A Dragscript tagged `Value` or a runtime-value reference for dynamic JavaScript values.
- Explicit `void`/undefined representation only after deciding whether absence, `undefined`, and `null` are distinct.

Do not lower all JavaScript numbers to `i32` or `i64`. JavaScript `number` is IEEE-754 binary64 with observable rounding and special values; `bigint` is a distinct domain; bitwise operators use specific integer conversions; string concatenation and coercion are semantic operations. A tagged dynamic value is usually the safer initial execution representation.

Use distinct type spellings for genuinely distinct runtime representations. Define how each representation is created, copied, compared, traced, and destroyed. If a type is only a pointer/handle into a runtime heap, document the ownership and lifetime rules and keep those handles from escaping their owning engine instance.

### 4.5 Operation semantics and effects

Before adding an operation, write down its semantic contract and tests. For example, `ds.add` must not be ambiguously used for numeric addition and string concatenation unless it implements the full specified coercion and evaluation rules. Prefer semantic names such as `ds.to_primitive`, `ds.to_number`, `ds.add_dynamic`, `ds.get_property`, `ds.set_property`, and `ds.call` if those are the actual defined operations.

The builtin `arith` dialect's integer behavior is not JavaScript arithmetic. Its documented integer operations use IR type widths; division/remainder edge cases may be undefined and folding skips undefined cases. Never apply those rules to JavaScript values unless the source operation has already been proven to have the corresponding semantics.

MLVM's constant folding, canonicalization, DCE, and kernel optimizations are only safe when operation contracts and effect classifications accurately describe Dragscript semantics. Initially optimize only operations with straightforward, tested semantics. Do not run generic integer rewrites over dynamic operations by giving them familiar-looking names.

### 4.6 Passes and transactions

MLVM's `PassManager` works at module scope and executes passes in insertion order. Each pass runs on a candidate module copy; a failed pass is rolled back, but earlier successful passes remain committed. If Dragscript needs the entire pipeline to be all-or-nothing, run it against an outer module copy and publish only on complete success.

- Verify the input and output of every custom pass.
- Declare required and preserved analyses honestly.
- Invalidate analyses after mutation unless explicitly preserved.
- Do not assume a pipeline repeats to a fixed point.
- Test failure after partial edits and verify rollback.
- Use `ConversionGraph` for explicit dialect conversion routes; the legal-dialect set must include every dialect intended to remain, including builtin dialects such as `func` or `arith`.
- Conversion is transactional and full: missing routes, failing rewrites, diagnostics, or invalid output should fail rather than silently leave arbitrary illegal operations.
- Do not mutate siblings/ancestors structurally inside a conversion callback while its traversal is active.

Optilisp kernels are optional optimization infrastructure, not a shortcut around semantics. Its generic integer backend supports only a subset of operators and contracts. Never assume an identity is valid for dynamic JavaScript arithmetic, floating point, objects, proxies, coercions, or side-effecting expressions just because it is valid for integers.

### 4.7 Diagnostics

Use MLVM's `SourceManager` and `DiagnosticEngine` or provide a compatible Dragscript diagnostic abstraction that can be translated to them. Diagnostics should be structured data, not printed from deep library functions.

- Preserve source spans from Tree-sitter nodes (byte offsets) through DSIR and into MLVM `SourceLocation` where possible.
- MLVM locations use one-based byte positions, not Unicode display columns.
- EOF can be a valid location; all-zero denotes unknown/invalid provenance.
- Keep original source buffers alive while diagnostics refer to them.
- Tree-sitter byte offsets are a natural interchange format. Convert to line/column using the exact owned source buffer and document whether columns are bytes, code points, or display cells.
- Do not discard diagnostics from a failed transaction before the embedding caller can inspect them.
- Error messages should include a stable code, source span, and actionable explanation where possible.
- Never rely on canonical printed MLVM IR to retain original comments or exact user-written SSA names; the canonical printer intentionally normalizes them.

---

## 5. Tree-sitter TypeScript frontend

### 5.1 Grammar selection

The Tree-sitter TypeScript repository provides distinct TypeScript and TSX grammar entry points in common versions. Inspect the checked-in package and use the actual generated parser headers/symbols from this checkout; do not hardcode a guessed function name or assume both grammars are built by default.

Select grammar based on an explicit source mode or file extension policy:

- `.ts` → TypeScript grammar.
- `.tsx` → TSX grammar.
- `.js` → only if Dragscript deliberately supports JavaScript input; choose whether it uses the TypeScript grammar or another configured grammar.
- `.jsx` → only if JSX is an enabled language mode.
- Unknown extension → require an explicit language mode or produce a clear diagnostic.

Do not infer JSX mode from a few tokens in the source. The host should be able to provide an explicit source kind when extensions are unavailable (e.g. eval strings).

### 5.2 Parser lifetime and error handling

- Create or reuse a Tree-sitter parser according to the documented runtime API and threading rules of the pinned version.
- Set the language for each parse and check the return status.
- Do not share a mutable parser concurrently unless the API and code prove it is safe; a parser pool or per-session parser is a reasonable default.
- Own the source bytes for at least as long as any syntax tree, node text access, diagnostic mapping, or incremental edit relies on them.
- Free/delete syntax trees and parsers according to the C API used by the checked-in version.
- Check `ts_node_is_error`, missing nodes, and root-level parse error state. Walk error/missing nodes to produce useful diagnostics rather than only reporting “parse failed.”
- Bound source size, parser work where supported, recursion in subsequent tree walks, and total diagnostic count.
- Treat malformed or adversarial input as normal input to validate, not as an impossible condition.
- A successful parse only means the grammar recognized the syntax. It does not mean the syntax is supported or semantically valid.

### 5.3 AST traversal

Tree-sitter nodes are lightweight views into a tree. Avoid storing raw node handles in long-lived compiled artifacts unless the tree and source ownership are explicit and stable. Prefer translating syntax nodes to DSIR nodes containing:

- A stable internal node ID.
- A semantic kind.
- Child IDs or owned child nodes.
- Source byte range and source ID.
- Literal payload, identifier spelling, and relevant syntax flags.
- A normalized representation of operators and declarations.
- Any original syntax metadata needed for diagnostics/debugging.

Do not rely on child position alone when a grammar exposes named fields. Prefer field names and named children; verify the grammar's actual node and field names against `node-types.json` and parser tests. Add focused tests for each syntax node you depend on. Grammar upgrades can change node shapes.

### 5.4 TypeScript-specific syntax

TypeScript has syntax that is erased, syntax that influences compile-time checking, and syntax that changes emitted JavaScript. These are different categories. Define a table of supported constructs and their treatment. Examples include:

- Type annotations, type aliases, interfaces, and type-only imports/exports: generally erased at runtime, but still require syntax validation and sometimes module handling.
- `as` assertions and non-null assertions: generally erased, but only if the surrounding expression is supported.
- Enums, namespaces, parameter properties, decorators, and JSX: may require transforms or may be rejected in the initial subset; do not silently erase them without implementing their specified runtime behavior.
- `satisfies`: typically a compile-time construct, but still must be parsed and handled correctly.
- `implements`, generic parameters, and type arguments: compile-time syntax, but grammar disambiguation and expression parsing must be correct.
- `import type` and `export type`: must not accidentally become runtime imports/exports.
- TSX angle-bracket ambiguities: use the TSX grammar and proper syntax nodes.

Unsupported constructs must fail at compile time with a diagnostic that names the construct and location. Do not silently turn an unsupported statement into a no-op or skip its subtree.

### 5.5 Incremental parsing

Incremental Tree-sitter parsing is a potential later optimization for DevTools/editor use, not a prerequisite for the first executable slice. If implemented:

- Model edits explicitly in byte offsets and apply them to the old tree before reparsing.
- Keep source-buffer versions associated with tree versions.
- Never reuse a tree against a different source version without the required edit operations.
- Test edits at UTF-8 boundaries, line boundaries, insertions/deletions, and edits that change grammar mode.
- Do not make incremental parsing a hidden correctness dependency; full parse should remain a reliable fallback.

---

## 6. Language semantics: define before lowering

The JavaScript runtime model is the product. A compiler pipeline is only correct if it preserves this model.

### 6.1 Runtime values

Create an explicit tagged value representation and document every tag. At minimum, the design will need to address:

- `undefined`
- `null`
- boolean
- number (IEEE-754 binary64)
- string (define encoding and Unicode behavior)
- bigint, if supported
- symbol, if supported
- object/reference
- function/closure
- host object/host function
- internal completion/exception state, if represented as a value

Do not conflate `undefined` and `null`, missing properties and present properties whose value is `undefined`, or falsy values and absent values. Specify equality variants (`===`, `==`, SameValue, SameValueZero) separately when implementing them. Decide how NaN, negative zero, and infinities behave and test them.

Strings need an explicit policy. JavaScript string indexing and length are defined in terms of UTF-16 code units, while source locations from Tree-sitter are byte-based. Avoid accidentally using UTF-8 byte length as JS string length. Keep source text encoding and runtime string representation separate.

### 6.2 Evaluation order and side effects

Specify left-to-right evaluation order and test it with side-effecting expressions. Avoid optimizations that reorder, duplicate, or drop potentially observable evaluation. Important examples:

- Function and argument evaluation.
- Computed property keys.
- Assignment target evaluation.
- Short-circuit operators (`&&`, `||`, `??`).
- Optional chaining.
- Template interpolation.
- Destructuring and default values.
- Spread syntax and iterators.
- `await`, promise callbacks, and host event-loop turns if supported.

An expression returning the same apparent value is not equivalent if it changes the number or order of calls, getters, setters, proxy traps, exceptions, allocations visible to user code, or host interactions.

### 6.3 Scope, bindings, and closures

Do not implement all declarations as one dictionary without a semantic model. Decide and test:

- `var` function scope and hoisting.
- `let`/`const` block scope and temporal dead zone.
- Parameter binding and default parameters.
- Function declarations and expression names.
- Lexical closures and captured mutable bindings.
- Shadowing and duplicate-declaration errors.
- `this`, strict mode, and call-site behavior.
- Global bindings and per-engine isolation.

Closures need cells/locations or an equivalent model when captured variables can be mutated. Capturing a copied value is not correct for a mutable lexical binding. Ensure closures and objects belong to a runtime/engine lifetime and cannot accidentally reference a destroyed engine.

### 6.4 Objects and property access

JavaScript objects are not just string-to-value maps. Before claiming object support, define the supported behavior for:

- Own properties and prototypes.
- Property descriptors and enumerability.
- Getters/setters.
- Property-key conversion (string and symbol).
- Arrays, holes, length behavior, and indexed properties.
- Function objects and their properties.
- `in`, `delete`, `Object.keys`, and iteration.
- Property lookup and mutation side effects.
- Proxy behavior, if supported.

If the first version uses a simplified object model, state the limitations explicitly and reject unsupported reflective behavior. Host objects should be represented by explicit handles and controlled operations, not raw pointers exposed to script.

### 6.5 Exceptions and abrupt completion

Model normal return, throw, break, continue, and other abrupt completions distinctly. `try/finally` must preserve the correct completion unless the finalizer itself changes it. A host exception must be translated into a defined Dragscript exception/error boundary; C++ exceptions must not leak through a C ABI.

If exception semantics are not implemented, reject `throw`/`try` at compile time rather than pretending they work. Runtime errors should include a stack or source trace where feasible.

### 6.6 Functions and calls

Implement a narrow function model first and grow it carefully. Define arity, missing arguments, extra arguments, `arguments` if supported, rest parameters, arrow functions, lexical `this`, constructor calls, and recursion limits. Distinguish ordinary call from construction (`new`). Validate callable values at runtime and report non-callable calls deterministically.

### 6.7 Async behavior and host scheduling

Dragworks is a headless browser, so asynchronous behavior may become essential, but do not smuggle an event loop into the language core without an API contract. Decide which host owns task scheduling, timers, promise jobs, I/O completion, and cancellation. Keep a synchronous evaluation API useful even if async execution is later added.

If supporting `async`/`await`, define microtask/job ordering, promise rejection handling, suspension/resumption state, and host shutdown behavior. Do not treat `await` as an ordinary function call. Add deterministic fake-scheduler tests before integrating real browser/network services.

### 6.8 Modules

Module loading is a security and determinism boundary. Define:

- How module specifiers are resolved.
- Which schemes and roots are permitted.
- Whether bare specifiers, relative paths, URLs, and built-ins are allowed.
- How circular dependencies are initialized.
- How live bindings behave.
- Whether modules are cached by canonical URL/path/content hash.
- Whether cache is per engine, per browser context, or global.
- How failures are cached or retried.
- How imports are exposed to Dragworks.

Never allow an untrusted page script to read arbitrary local files merely because it can import a path. Module resolution should be delegated to a host policy callback or a narrowly scoped resolver.

---

## 7. Embedded-first API and lifecycle

Treat the public API as a product feature. Avoid requiring subprocesses or global singleton state.

### 7.1 Suggested conceptual API

Use repository naming conventions, but aim for a shape equivalent to:

```cpp
namespace dragscript {
  struct EngineOptions;
  struct Source;
  struct Diagnostic;
  struct CompileResult;
  struct RunOptions;
  struct RunResult;
  class Engine;
  class CompiledScript;
}
```

The public API should make ownership and failure explicit. A possible conceptual flow is:

```cpp
Engine engine(options);
engine.register_host_function("console.log", ...);
auto compiled = engine.compile(source);
if (!compiled.ok()) {
    // inspect structured diagnostics
}
auto result = engine.run(compiled.script(), run_options);
```

This is illustrative, not a claim that these exact types already exist. Match existing project conventions and settle names before exposing a stable ABI.

### 7.2 API design rules

- Avoid process-global mutable state. Independent Engine instances should have independent globals, heaps, module caches, host bindings, and limits unless sharing is explicit.
- Make Engine and compiled-artifact ownership visible in types or documented lifetime rules.
- Provide a compile-only operation separate from execute.
- Support source names and optional origin URLs for useful diagnostics and module resolution.
- Return structured compile/runtime errors. Do not make callers parse log strings.
- Keep logging optional and route it through callbacks or the host logger.
- Avoid requiring a terminal, GUI, filesystem current working directory, or network in the core library.
- Avoid exposing Tree-sitter node pointers or MLVM IDs in the stable public API.
- If a C API is needed, keep it opaque-handle based, versioned, null-safe, and exception-safe.
- Validate all C ABI arguments and return explicit status codes.
- Ensure no C++ exception crosses a C ABI boundary.
- Define thread-safety: either an Engine is single-thread-affine, externally synchronized, or internally synchronized. State which.
- Provide cancellation and execution-budget controls before running potentially untrusted scripts.
- Make host callback reentrancy behavior explicit; callbacks may re-enter the engine only if this is deliberately supported and tested.

### 7.3 Resource controls

The engine should be designed for untrusted or semi-trusted page scripts. Add configurable limits for:

- Source bytes.
- Parse/AST/DSIR node count.
- Nesting depth and compile recursion.
- Runtime instruction/step count.
- Call stack depth.
- Heap/object allocation and string size.
- Module count and module source bytes.
- Diagnostic count.
- Execution wall time where the host can enforce it.
- Host callback duration/cancellation where practical.

A wall-clock timeout alone is insufficient if the engine cannot interrupt a tight loop. A step budget or safe-point budget is required for an interpreter. For native/JIT execution, define how interruption and memory safety work before enabling it for untrusted code.

All limits must fail with a defined diagnostic/error rather than integer overflow, process abort, or unbounded allocation. Test exact-limit and one-over-limit cases.

---

## 8. Execution strategy

### 8.1 Start with an interpreter

Prefer a simple, correct DSIR interpreter or verified low-level interpreter as the first end-to-end path. This provides an executable semantics baseline and makes it easier to compare future MLVM lowering/optimization results.

An initial supported vertical slice could include numeric/string/boolean/nullish literals, local declarations, simple expressions, blocks, `if`, function declarations/calls, and return. Pick the exact subset in a feature matrix and reject everything else.

The interpreter must use the same runtime-value and host-call contracts intended for later backends. Avoid having a “fast path” with subtly different coercion or exception semantics.

### 8.2 MLVM's role

Use MLVM for the aspects it actually provides:

- Owned generic IR and SSA bookkeeping.
- Registered operation/dialect contracts and verification.
- Pass pipelines and transactional transformations.
- Explicit dialect conversion.
- Diagnostics and source locations.
- Optional optimization and backend integrations where a supported semantic mapping exists.

Do not assume MLVM automatically provides a JavaScript interpreter, GC, JS object model, module loader, JIT, deoptimization, or browser event loop. Verify each capability in the checked-in source/manual before relying on it.

A practical first lowering may use a tagged dynamic-value representation with explicit runtime operations. Calls, dynamic property access, and coercions should lower to operations with precise semantics and conservative effects. A host runtime can implement those operations. If an operation cannot be lowered without losing observable behavior, keep it in DSIR or reject it.

### 8.3 Native code and optimization

Native code generation is a later milestone, not a prerequisite for the engine to be useful. Before adding it, establish:

- A stable semantic baseline and differential test suite.
- A defined calling convention for runtime values and host calls.
- Exception/unwind and stack-trace policy.
- GC/rooting and object lifetime strategy.
- Deoptimization or a clear no-deoptimization model.
- Safe-point/interrupt policy.
- ABI and target support matrix.
- A secure policy for executing generated code.

Use optimization only where equivalence is justified. Disable or avoid passes that can erase or reorder operations with unknown effects. For each optimization, test both a valid match and a near miss, including side effects, exceptions, overflow, NaN, negative zero, and object coercion where relevant.

---

## 9. Garbage collection and memory management

Choose a memory model deliberately before implementing closures and objects. Possible approaches include tracing GC, reference counting with cycle handling, or an arena model restricted to short-lived evaluations. Document the choice and its limitations.

Requirements:

- Every object/function/closure has a clear owning runtime or heap.
- Cycles must not leak indefinitely if scripts can create them.
- Host handles must not outlive their host context unless explicitly reference-counted.
- Root all values held in interpreter stacks, registers, globals, module environments, pending jobs, and host callbacks.
- Define when a compiled artifact can be reused across engine instances; do not embed instance-specific heap pointers into a reusable artifact.
- Avoid C++ raw owning pointers in runtime values.
- If using reference counting, test cycles and reentrant finalization.
- If using a tracing collector, test roots across host callbacks and suspension points.
- Finalizers/destructors must not run arbitrary script at unsafe times unless semantics and reentrancy are designed.
- Make shutdown deterministic and prevent callbacks into a partially destroyed Engine.

`jemalloc` may be useful for allocation performance but does not solve object lifetime or garbage collection. Select an allocator only after measuring.

---

## 10. Dragworks host integration and security

Treat scripts as untrusted input. Dragscript's core should not grant capabilities merely because a C++ function exists in the process.

### 10.1 Capability-based host bindings

Expose host functionality through an explicit per-engine or per-realm registry. Examples may include console, DOM, timers, URL/fetch, storage, and event APIs, but these should be implemented by Dragworks and injected intentionally.

- Use narrow host interfaces rather than exposing arbitrary C++ objects.
- Validate argument counts and types at the boundary.
- Convert errors/exceptions at the boundary.
- Keep browser-context identity and origin/security policy in Dragworks, not hidden in global Dragscript state.
- Make permission-denied and unsupported operations observable and deterministic.
- Avoid exposing filesystem, process, native-library loading, environment variables, or unrestricted networking by default.
- Do not let host functions bypass the runtime's execution budget without accounting.
- Avoid deadlocks when a host callback re-enters the runtime or waits on an event loop.

### 10.2 Isolation

At minimum, distinguish engine instances and, if browser semantics require it, realms/contexts within an engine. Decide which values can cross those boundaries. Never share mutable globals or prototypes across unrelated page contexts by accident.

Native memory safety is critical. If a page can cause a runtime operation to dereference arbitrary host memory, the engine is not safe for untrusted scripts. Prefer handles, checked indexes, validated tags, and bounded memory access.

### 10.3 Supply chain and dependencies

- Keep third-party changes minimal and reviewable.
- Do not fetch or regenerate dependencies during ordinary builds.
- Preserve upstream license notices.
- Avoid passing untrusted source into shell commands or temporary-file-based compiler subprocesses.
- Pin and document grammar/runtime versions.
- Build tests with sanitizers where supported.
- Fuzz parser-to-DSIR conversion and runtime operations.
- Do not log page secrets, cookies, tokens, or full source text by default.

---

## 11. Build system and repository conventions

Inspect the existing build before editing. Integrate Dragscript as a library target with public include paths and explicit dependency links. Follow the repository's naming and install/export conventions.

Recommended build principles:

- A core target should build without GUI libraries and without requiring Dragworks itself.
- Tree-sitter and the TypeScript grammar should be wired through existing vendored targets or a small dedicated target; avoid duplicate runtime symbols if another component already links Tree-sitter.
- Do not regenerate the grammar in a normal build.
- Make optional tools/tests opt-in if they add heavy dependencies.
- Keep public headers independent of third-party internal headers where possible.
- Use CMake target dependencies rather than global include/link flags.
- Support static/shared builds only if the repository expects both; test the configurations actually supported.
- Keep warnings enabled and fix warnings introduced by Dragscript.
- Add install/export rules only in the repository's established style.
- Avoid requiring network access at configure/build/test time.
- Ensure tests can run headlessly in CI.

Do not assume the MLVM target names or optional components from memory. Inspect `mlvm/CMakeLists.txt`, the manual, and actual exported targets. MLVM's C API is documented as intentionally small and does not currently expose module/pass/kernel functionality; use the C++ IR API internally unless a C-facing API is specifically required.

---

## 12. Testing strategy

Every feature needs positive tests, negative tests, and tests for semantic edge cases. Test behavior, not only AST shapes or printed IR.

### 12.1 Test layers

1. **Frontend tests** — grammar selection, syntax errors, missing nodes, TS/TSX distinctions, source spans, Unicode offsets.
2. **TypeScript erasure tests** — annotations/type aliases/interfaces/type-only imports are handled as specified; runtime constructs are preserved or rejected.
3. **DSIR tests** — node shape, source provenance, scope representation, unsupported-node diagnostics.
4. **MLVM lowering tests** — operation names/types/attributes, use-list integrity, verifier acceptance, rejection of malformed IR.
5. **Runtime unit tests** — tagged values, coercion, scope, calls, objects, exceptions, budget enforcement.
6. **Embedding tests** — independent engines, lifecycle, callbacks, error ownership, cancellation, reentrancy policy.
7. **Module tests** — resolution policy, cache, cycles, live bindings if supported, denied paths/schemes.
8. **Security/resource tests** — infinite loops, deep nesting, huge strings, allocation storms, malformed input, invalid host handles.
9. **Differential tests** — compare supported JavaScript behavior with a known reference engine such as QuickJS, while documenting differences and avoiding treating the reference as the spec.
10. **Dragworks integration tests** — realistic page scripts against fake or test host APIs, no GUI/network dependency by default.
11. **Fuzz tests** — parser, syntax-tree lowering, runtime value operations, module resolver, and public API inputs.
12. **Performance tests** — compile time, execution time, allocation count, peak memory, and startup overhead on representative workloads.

### 12.2 Required negative cases

For every syntax or operation feature, include at least:

- Malformed input.
- A syntactically valid but unsupported construct.
- Incorrect operand/value type.
- Missing identifier/property/call target as applicable.
- Side-effecting operands to validate evaluation order.
- A runtime exception/error path.
- A limit-exceeded path.
- A test that a failed compile/run does not corrupt a reusable Engine.
- A regression test for any discovered bug.

Never let unsupported syntax fall through a default visitor branch that silently ignores it. Default behavior for an unknown node kind should be a diagnostic.

### 12.3 Differential and conformance testing

QuickJS is available in `third_party` and may be useful as a local oracle. Keep differential tests restricted to the documented common subset. Record expected divergences such as unsupported TS transforms, host APIs, or deliberate compatibility gaps. Do not run external network tests in the normal suite.

For each semantic feature, cite or summarize the relevant ECMAScript/TypeScript behavior in the test or design note. A reference engine is evidence, not the normative specification. Avoid using TypeScript's compiler as a runtime dependency; it can optionally be used in development to compare type erasure or emitted JavaScript if available, but tests must not require it unless explicitly configured.

### 12.4 MLVM regression tests

Use MLVM's own tests and API contracts as a guide:

- Test `Context` lifetime and module ownership.
- Test registered dialect and operation verification.
- Test malformed IR and failed lowering.
- Test SSA uses after replacement/erasure and after any direct operand edits.
- Test each pass on valid input, a near miss, and invalid input.
- Test transactional rollback after partial mutation.
- Test deterministic canonical IR output where relevant.
- Test full dialect conversion with explicit legal dialect sets.
- Do not assume Optilisp integer identities apply to Dragscript dynamic operations.

---

## 13. Diagnostics and observability

A useful embedded engine should be debuggable without attaching a terminal.

Diagnostics should include:

- Stable error code/category.
- Human-readable message.
- Source name or URL.
- Start/end byte offsets and line/column.
- Related locations for notes when useful.
- Compile-time versus runtime phase.
- Optional stack/source trace.
- Whether the error is recoverable.
- Resource-limit category when applicable.

Keep source spans through the frontend and DSIR. MLVM's canonical IR printer does not preserve source provenance in serialized text, so do not use a print/parse round trip as the sole source of truth for source maps.

Offer optional tracing hooks for parse, lowering, optimization, and execution. Avoid writing directly to stderr from library internals. Make tracing opt-in and avoid logging sensitive page content by default.

When debugging, provide a mode that can dump:
- Tree-sitter S-expressions or a readable node outline.
- DSIR with source spans.
- MLVM IR before and after passes.
- Pass names and verification outcomes.
- Runtime stack and budget counters.

Keep these tools optional and stable enough for regression tests; do not make debug formatting the public ABI.

---

## 14. Feature matrix and milestone discipline

Maintain a checked-in feature matrix, for example `docs/feature-matrix.md`, with columns for feature, parser support, semantic validation, runtime support, host integration, tests, and status. Use statuses such as **planned**, **parsed**, **validated**, **executed**, and **tested**. A feature is not “supported” merely because Tree-sitter parses it.

Suggested milestones:

### M0 — Repository reconnaissance and architecture
- Inspect MLVM APIs/build targets and Tree-sitter versions.
- Establish Dragscript target, source layout, diagnostics, and test harness.
- Write feature matrix and runtime-value design.
- No language-support claims yet.

### M1 — Parse and diagnose
- Parse TS and TSX with explicit mode selection.
- Own source buffers and report syntax errors/missing nodes with spans.
- Add parser error and resource-bound tests.
- No execution required.

### M2 — DSIR and unsupported syntax
- Convert a small expression/statement subset into DSIR.
- Preserve source ranges.
- Reject unsupported syntax reliably.
- Add DSIR dump and tests.

### M3 — Runtime values and interpreter
- Implement tagged values and basic expression/statement evaluation.
- Define scope, truthiness, number behavior, and error propagation for the chosen subset.
- Add step/call-depth limits.
- Test with no MLVM optimization.

### M4 — MLVM IR integration
- Register Dragscript dialect operations and contracts.
- Lower a narrow subset into MLVM IR.
- Verify every module and preserve source locations.
- Add IR dump and verifier-negative tests.

### M5 — Embedded API
- Add Engine lifecycle, compile/run results, host function registration, per-engine isolation, and cancellation/budgets.
- Test multiple independent engines and destruction/error paths.
- Keep API headers stable and minimal.

### M6 — Browser host bridge
- Connect a minimal console or test host API through capability-controlled bindings.
- Add Dragworks integration tests with a fake host.
- Define origin/realm policy before exposing browser capabilities.

### M7 — Semantic expansion
- Add functions/closures, arrays/objects, exceptions, modules, and async features in independently testable increments.
- Each increment must update the feature matrix and compatibility tests.

### M8 — Optimization / alternative backend
- Establish differential baseline first.
- Add only proven transformations.
- Benchmark realistic scripts.
- Add native execution only after runtime safety and ABI contracts are clear.

Do not skip directly from M1 parsing to broad language claims.

---

## 15. Coding conventions for agents

- Read nearby code before editing and match established naming/style.
- Keep changes small enough to review and test.
- Prefer explicit enums and tagged structures over magic strings for internal semantic kinds.
- Use string spellings only where required by MLVM's current API or serialized formats.
- Keep the public API minimal; internal structures may evolve.
- Use RAII for Tree-sitter parser/tree and runtime resources.
- Avoid `new`/`delete` ownership spread through code; use unique ownership or the chosen GC model.
- Check every C API return status and every optional/result before dereferencing.
- Do not suppress diagnostics or ignore failed verification to make a test pass.
- Do not catch and discard all exceptions. Translate failures at deliberate boundaries and retain useful context.
- Never make unsupported operations silently return `undefined` or a default value.
- Avoid undefined behavior and signed-overflow assumptions in C++.
- Do not use host C++ arithmetic as a substitute for JavaScript number semantics.
- Do not add optimizations before the baseline behavior is specified.
- Add comments explaining semantic invariants, not comments that merely restate syntax.
- Keep generated files generated only by the repository's documented process.
- Do not edit vendored MLVM internals to add Dragscript-specific behavior; prefer Dragscript-owned dialects, adapters, and passes.
- Do not modify the upstream grammar to accept syntax without understanding the language consequences.
- Avoid committing build directories, generated test outputs, caches, or temporary source files.

### Handling uncertainty

When an API, grammar node name, MLVM feature, or semantic rule is uncertain:

1. Search the checked-in headers, docs, and tests.
2. Write a small probe or focused test.
3. Confirm the actual version and target configuration.
4. Implement only what the evidence supports.
5. Document any remaining limitation.

Do not fabricate function names, assume an undocumented backend exists, or implement an approximation without labeling it.

---

## 16. Suggested source layout

Adapt to repository conventions, but preserve the layer boundaries:

```text
dragscript/
  AGENTS.md
  CMakeLists.txt
  include/dragscript/
    engine.hpp
    source.hpp
    diagnostic.hpp
    value.hpp              # expose only if intended to be public
    host_api.hpp
  src/
    engine.cpp
    source.cpp
    diagnostics.cpp
    frontend/
      tree_sitter_frontend.cpp
      grammar_select.cpp
      syntax_diagnostics.cpp
      ts_erasure.cpp
    semantic/
      dsir.hpp
      dsir.cpp
      validator.cpp
      scope.cpp
    mlvm/
      dialect.cpp
      lowering.cpp
      passes.cpp
    runtime/
      value.cpp
      interpreter.cpp
      environment.cpp
      object.cpp
      limits.cpp
    modules/
      resolver.cpp
      module_cache.cpp
  tests/
    frontend/
    semantic/
    lowering/
    runtime/
    embedding/
    security/
    integration/
  docs/
    architecture.md
    feature-matrix.md
    runtime-semantics.md
```

This is a suggestion, not a demand to create empty directories in advance. Add files when a real vertical slice needs them.

---

## 17. Agent workflow checklist

Before coding:

- [ ] Read this guide and the relevant MLVM manual/API chapters.
- [ ] Inspect actual dependency paths and generated grammar headers.
- [ ] Identify the exact supported language subset affected by the task.
- [ ] Define semantics and failure behavior before lowering.
- [ ] Find the closest existing test and API pattern.

During coding:

- [ ] Preserve source provenance.
- [ ] Keep Tree-sitter, DSIR, MLVM, runtime, and host boundaries explicit.
- [ ] Register and verify MLVM dialect operations before using them.
- [ ] Treat unknown syntax and unknown operation effects conservatively.
- [ ] Respect ownership, context lifetime, use lists, and transaction boundaries.
- [ ] Add positive, negative, and edge-case tests.
- [ ] Enforce resource limits on attacker-controlled input.

Before finishing:

- [ ] Build the narrowest relevant target.
- [ ] Run focused tests, then the broader suite available in the configured build.
- [ ] Check that unsupported syntax fails loudly.
- [ ] Check diagnostics and source ranges.
- [ ] Verify MLVM IR after lowering and transformations.
- [ ] Confirm failure does not publish partial artifacts or poison the Engine.
- [ ] Update feature matrix/docs and summarize limitations.
- [ ] Report commands run and any tests not run; never claim tests passed if they were not executed.

---

## 18. Definition of done

A Dragscript change is done only when:

1. The feature's intended semantics and limitations are documented.
2. Syntax recognition and semantic support are distinguished.
3. The implementation respects the runtime/host security boundary.
4. Source locations and useful diagnostics survive the relevant stages.
5. MLVM IR is registered, verified, and transformed under honest contracts where applicable.
6. Runtime behavior is covered by tests, including negative and side-effecting cases.
7. Resource limits and lifecycle behavior remain correct.
8. The change builds through the repository's supported build system.
9. Tests and known gaps are reported accurately.
10. No accidental claim of full TypeScript/ECMAScript conformance is introduced.

**Default decision rule:** if correctness depends on semantics that are not yet implemented or proven, preserve the higher-level representation or reject the construct with a clear diagnostic. Never silently compile it into something merely similar.
