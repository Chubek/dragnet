# Exolayer Development Instructions

## Scope

This directory contains the ExolangTk integration for Limestone’s **Exolayer** subsystem.

Exolayer is responsible for:

- Foreign-function interface support
- Native extension integration
- C and C++ interoperability
- Calling-convention and ABI adaptation
- Native symbol export and mangling
- Data layout and marshaling
- Debugging, stack inspection, and symbol integration

The primary implementation dependency is ExolangTk under:

```text
third_party/exolangtk
```

Exolayer must preserve a strict separation between:

-olayer must preserve a strict separation between:

- Limestone compiler semantics and intermediate representations
- Target-specific ABI, platform, and debugger behavior

Do not place Limestone instruction-selection or scheduling logic in ExolangTk integration headers.

## Repository Layout

Relevant directories and files include:

```text
third_party/exolangtk/
├── include/
│   ├── FFItk/          # FFI types, call frames, CIFs, closures, loaders
│   ├── InteropTk/      # ABI, layout, marshaling, mangling, platform APIs
│   ├── ExtensionTk/    # Native extension loading and registration
│   └── DebugTk/        # Stack, breakpoint, unwind, and symbol APIs
├── manifests/          # Module manifests and exported-symbol declarations
├── docs/manual/        # Design and implementation documentation
├── tests/               # Build and integration tests
├── CMakeLists.txt       # Build-system integration
├── GUIDE.md             # Project development guide
├── README.md            # Project overview
└── AGENTS.md            # Local development instructions
```

The existing ExolangTk headers, documentation,.

## and build files are authoritative for the behavior of wrapped APIs.

## Naming Conventions

All Exolayer-owned public symbols must use the `exl_` prefix.

Use these conventions:

| Symbol category | Convention |
|---|---|
| Functions and variables | `exl_...` |
| Types | `exl_..._t` where appropriate |
| Constants and macros | `EXL_...` |
| Public implementation qualifier | `EXL_DEF` |

Do not introduce unprefixed public symbols into Exolayer or ExolangTk integration headers.

Internal helpers must use a module-specific prefix and must not leak into the public namespace.

## Header Structure

Exolayer modules use a header-plus-implementation pattern.

```c
#ifndef EXL_MODULE_H
#define EXL_MODULE_H

/* Public declarations. */

#ifdef EXL_MODULE_IMPLEMENTATION

/* Implementation definitions. */

#endif /* EXL_MODULE_IMPLEMENTATION */

#endif /* EXL_MODULE_H */
```

Rules:

- Use an include guard named `EXL_<MODULE_NAME>_H`.
- Use an implementation guard named `EXL_<MODULE_NAME>_IMPLEMENTATION`.
- Define the implementation macro in exactly one implementation translation unit.
- Do not define implementation macros globally unless explicitly required.
- Use `EXL_DEF` for non-trivial header-defined functions.
- Avoid static global state unless ownership, initialization, and teardown are explicit.
- Preserve the ownership and lifetime rules of the underlying ExolangTk API.

Public headers must be usable from both C and C++ unless explicitly documented as C++-only.

## Module Manifest

Every Exolayer module must be registered in:

```text
third_party/exolangtk/manifests/Exolayer-Modules.yaml
```

Each module entry must define:

```yaml
name:
header:
brief:
stability:
depends_on:
provides:
```

The manifest is the source of truth for module discovery and exported symbols.

Manifest rules:

- Every `provides` symbol must be declared by the referenced header.
- Every intentionally exported public symbol must appear in `provides`.
- Header and manifest changes must be made together.
- Treat manifest changes as API changes.
- Use `experimental` for new APIs unless a stability commitment exists.

## Dependency Direction

The preferred dependency direction is:

```text
Exolayer
  -> InteropTk
  -> FFItk
  -> ExtensionTk
  -> DebugTk
```

Use the smallest dependency set possible:

- `InteropTk` provides ABI, platform, layout, andTk` provides ABI, platform, layout, and interoperability foundations.
- `FF CIFs, closures, and trampolines.
- `ExtensionTk` is required only for native extension loading or registration.
- `DebugTk` is optional and should be used only by debugging modules.

Avoid circular dependencies.

In particular:

- ExolangTk must not depend on Limestone compiler internals.
- Low-level FFI modules must not depend on debugging modules.
- Debugging adapters must not become prerequisites for ordinary FFI calls.
- Do not add `DebugTk` solely for diagnostic convenience.

All dependencies must be declared in both the module manifest and the relevant CMake target.

## ABI and Platform Rules

Exolayer must not assume that the host compiler ABI is the target ABI.

Use ExolangTk abstractions for:

- Calling conventions
- Argument and return-value classification
- Type layout
- Alignment and size
- Function invocation
- Dynamic loading
- Symbol mangling
- Platform-specific behavior

Do not duplicate ABI logic in Exolayer when ExolangTk already provides the required abstraction.

Target-dependent behavior must be explicit. Do not silently fall back from one calling convention, layout, or platform implementation to another.

ABI-sensitive code must account for:

- Aggregate and vector arguments
- Variadic calls
- Pointer and function-pointer representation
- 32-bit and 64-bit targets
- Alignment requirements
- Error propagation across the C boundary
- Symbol visibility and export behavior

## FFI and Marshaling

FFI adapters must document boundary ownership and lifetime.

Each adapter should specify:

- Who owns input buffers
- Who owns returned buffers
- Whether values are borrowed or copied
- How null pointers are represented
- How errors are reported
- Whether callbacks may outlive the initiating call
- Which thread or execution context may invoke callbacks
- Whether reentrancy is supported

Do not cast arbitrary Limestone values directly to native pointers or function pointers.

Use ExolangTk type, layout, and marshaling APIs to construct arguments and decode results. Reject unsupported layouts and calling conventions explicitly.

Avoid hidden allocation in low-level call paths unless the API documents it.

## Native Extensions

Native extension loading must use `ExtensionTk`.

Extension modules must:

- Validate the extension ABI or version before registration.
- Check required symbols before invoking them.
- Keep loader handles alive while registered function pointers remain valid.
- Provide deterministic cleanup.
- Report platform loader errors through the Exolayer error path.
- Avoid running extension initialization before validation completes.
- Define behavior for duplicate registrations and conflicting names.

Do not expose raw dynamic-loader handles as the primary Exolayer API unless explicitly required.

## Debugging Integration

Debugging functionality must remain separate from ordinary FFI and extension functionality.

Debugger-facing modules may use:

- Stack and unwind APIs
- Breakpoint APIs
- Symbol APIs
- Platform-specific debug adapters

Handle these cases explicitly:

- Missing symbols
-:

- Missing symbols
- Stripped binaries
- Incomplete unwind information
- Foreign frames
- Unsupported platforms
- Invalid or stale debug handles

Debugger callbacks must not execute arbitrary compiler or runtime code while holding internal locks.

## Error Handling

Preserve ExolangTk error conventions and context across the Exolayer boundary.

Errors should identify, where applicable:

- The failed operation
- The target platform or ABI
- The symbol or extension involved
- The calling convention
- The expected and actual type or layout
- The underlying platform error

Do not terminate the process for recoverable FFI, extension, or debugging failures.

Do not silently ignore:

- Loader failures
- Symbol-resolution failures
- Marshaling failures
- Unsupported ABI cases
- Unwind failures

## C and C++ Compatibility

Public Exolayer headers must be valid in both C and C++ compilation modes unless explicitly marked C++-only.

For C-compatible headers:

- Use only C-compatible declarations.
- Wrap C declarations with `extern "C"` when compiling as C++.
- Do not use templates, namespaces, references, exceptions, or overloads in the C API.
- Use opaque handles for implementation-defined objects.
- Make ownership and destruction explicit.
- Do not allow C++ exceptions to cross a C ABI boundary.

C++ wrappers may provide RAII and type-safe helpers, but they must remain thin wrappers over the stable C ABI.

## Build Integration

When adding or changing an Exolayer module, update all relevant build files:

- `third_party/exolangtk/CMakeLists.txt`
- Component-level `CMakeLists.txt` files
- Header installation rules
- Documentation targets
- `manifests/Exolayer-Modules.yaml`
- Tests and test registration

Declare module dependencies explicitly in CMake. Do not rely on include-order accidents or undeclared transitive dependencies.

The build must continue to support the C and C++ consumer configurations documented by ExolangTk.

## Documentation

Public APIs require Doxygen-compatible comments.

Document:

- Purpose
- Parameters and return values
-Document:

- Purpose
- Parameters and return values
- Ownership and lifetime
- Error ABI and platform restrictions
- API stability

Add Exolayer documentation under:

```text
third_party/exolangtk/docs/manual/
```

Documentation must explain the boundary between Exolayer and the underlying ExolangTk module. Avoid duplicating implementation details that are not part of the public contract.

## Testing Requirements

Each new Exolayer module must include focused tests for:

- C compilation
- C++ compilation
- Multiple translation units including the same header
- Implementation-guard behavior
- Manifest and symbol consistency
- Error paths
- Ownership and lifetime rules
- Relevant ABI and platform cases
- Extension loading, where applicable
- Debugging behavior, where applicable

Before completing a change, verify that:

- Every manifest header exists.
- Every manifest-provided symbol is declared.
- Public symbols follow the `exl_` and `EXL_` naming rules.
- No dependency cycle was introduced.
- Public headers compile as C and C++.
- Implementation definitions do not create duplicate symbols.
- CMake installation and test registration are complete.

## Change Discipline

Keep changes localized to the Exolayer module being added or modified.

Do not:

- Rewrite unrelated ExolangTk modules.
- Change ABI behavior without documenting compatibility impact.
- Add speculative platform behavior.
- Add target-specific scheduling or resource data to ExolangTk.
- Add a `DebugTk` dependency merely for convenience.
- Modify generated or vendor-managed files without confirming the generation process.
- Duplicate functionality already provided by ExolangTk.

When wrapping an ExolangTk API, preserve its semantics instead of creating a superficially similar abstraction.

Before submitting a change, review the diff for:

- Public symbol naming
- Manifest accuracy
- Header guard correctness
- C/C++ compatibility
- Dependency direction
- Error and lifetime handling
- CMake installation and test coverage
- Documentation completeness