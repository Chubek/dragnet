# MLVM Manual

A 40-chapter user and developer manual for MLVM **0.1.0**. It covers the
implemented compiler infrastructure, its command-line tools, and extension points
for dialects, passes, and user-authored Optilisp optimization kernels.

## How to use this manual

Run shell examples from the `mlvm/` project directory unless a chapter says
otherwise. `build/` means a standalone CMake build of that directory. Replace
placeholder input names with your own files. Commands requiring an optional
component identify it explicitly. Native executable examples require Linux on
x86-64. C++ fragments without `main` illustrate integration into an existing
program; complete programs are identified as such.

The manual distinguishes current behavior from architectural goals. In
particular, generic SSA IR, NodeIR graphs, binary object support, and target
code generation have different capabilities. Support in one does not imply
support in another. Header declarations and the focused documents in
[`../docs`](../docs/README.md) provide the detailed contracts behind this guide.

## Contents

### Part I — Orientation and setup

1. [Introduction and supported scope](01-introduction.md)
2. [Architecture and repository guide](02-architecture.md)
3. [Configuring and building](03-building.md)
4. [Installation and embedding setup](04-installation.md)
5. [First IR workflow](05-first-ir.md)
6. [Command-line conventions](06-command-line.md)

### Part II — Core IR and embedding

7. [Contexts, identifiers, and ownership](07-contexts.md)
8. [Sources and diagnostics](08-diagnostics.md)
9. [The generic SSA object model](09-ssa-model.md)
10. [Textual IR syntax](10-textual-ir.md)
11. [Builtin functions and integer operations](11-builtin-dialects.md)
12. [Verification and safe mutation](12-verification.md)
13. [Building and processing IR with C++](13-cpp-ir.md)
14. [The C embedding API](14-c-api.md)
15. [Defining custom dialects](15-custom-dialects.md)
16. [Dialect conversion](16-conversion.md)

### Part III — Optimization

17. [Pass pipelines and transactions](17-pass-pipelines.md)
18. [Analysis caching and preservation](18-analyses.md)
19. [Builtin optimization passes](19-builtin-passes.md)
20. [Writing and observing passes](20-custom-passes.md)
21. [Blackstone and Optilisp](21-blackstone.md)
22. [Applying kernels from textual IR and the CLI](22-kernel-cli.md)
23. [Applying kernels through C++](23-kernel-api.md)
24. [Authoring an Optilisp kernel](24-authoring-kernels.md)
25. [Kernel contracts, proofs, and validation](25-kernel-contracts.md)
26. [Host optimizers and tree rewriting](26-host-optimizers.md)
27. [NodeIR graphs and translation](27-nodeir.md)

### Part IV — Targets and binary tools

28. [Targets and machine encoding](28-targets.md)
29. [Assembly programming](29-assembler.md)
30. [Object files and relocations](30-objects.md)
31. [Static and relocatable linking](31-linking.md)
32. [Archives and symbol extraction](32-archives.md)
33. [Inspecting objects and strings](33-inspection.md)
34. [Copying and stripping objects](34-object-transforms.md)
35. [Target contracts and raw images](35-raw-linking.md)

### Part V — Frontend and development workflows

36. [Cephyr preprocessing and parsing](36-cephyr-frontend.md)
37. [Cephyr semantics, lowering, and code generation](37-cephyr-codegen.md)
38. [An end-to-end executable](38-end-to-end.md)
39. [Testing and troubleshooting](39-testing.md)
40. [Extending MLVM and planning future work](40-extending.md)

## Suggested reading paths

- **IR user:** 3, 5–6, 10–12, 17, 19, 22.
- **Embedding developer:** 4, 7–9, 12–18, 20, 23.
- **Kernel author:** 11, 21–27, 39.
- **Toolchain user:** 3, 6, 28–38.
- **Contributor:** 2, 12, 20, 25–27, 39–40.

Each chapter links to its implementation or reference material. These links are
also useful when checking behavior against a newer checkout.
