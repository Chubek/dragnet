# 1. Introduction and supported scope

[Contents](README.md) · [Next: Architecture](02-architecture.md)

MLVM is a multi-level, multi-dialect compiler infrastructure. It supplies reusable
representations and transformations, a small native toolchain, and an optional C
frontend named Cephyr. Applications can embed the libraries or use command-line
tools to inspect and transform artifacts.

## What is implemented

The 0.1.0 baseline includes contexts and structured diagnostics, a value-owned
generic SSA representation, versioned text, operation registration, verification,
transactional module passes, and full dialect conversion. A narrow x86-64 toolchain
assembles, archives, links, and inspects ELF relocatable objects. Optional
library-backed binary tools broaden object handling; optional Optilisp support
executes a checked integer subset of Blackstone kernels.

Cephyr accepts constant-return C functions in a documented C17 freestanding
subset. It is useful for demonstrating a complete source-to-executable path,
but is not a general-purpose or ISO-conforming C implementation.

## Three useful starting points

| Goal | Starting artifact | Primary interface |
| --- | --- | --- |
| Transform SSA | `.mlvm` text or `mlvm::Module` | `mlvm`, `MLVM::IR` |
| Author optimizations | `.olisp` kernel source | Optilisp loader and pass adapter |
| Build native artifacts | supported assembly or Cephyr C | `mlvm-as`, `cephyr`, `mlvm-ld` |

The architecture anticipates richer high-, mid-, low-, and machine-level
representations. Those goals do not imply that generic IR currently supports
memory operations, CFG successor edges, general function calls, or lowering every
custom dialect to machine code.

## Reading support claims

An accepted object-machine identifier does not promise instruction selection for
that machine. A parsed C construct does not promise semantic lowering. Likewise,
a validated Optilisp declaration does not promise that a selected backend can
execute its transformation. Treat each boundary as an explicit contract.

Reference: [implementation status](../docs/ROADMAP.md).
