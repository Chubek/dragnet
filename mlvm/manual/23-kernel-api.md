# 23. Applying kernels through C++

[Contents](README.md) · [Next: Authoring kernels](24-authoring-kernels.md)

Link to `MLVM::OptilispPass` and include `mlvm/optilisp/pass.hpp`. An existing
verified module can schedule catalog or embedded user-defined kernels with the
same stable IDs used in text.

```cpp
// context must outlive module; diagnostics is supplied by the caller.
module.kernel_applications.push_back(
    "blackstone.hir.constant-folding.integer-mul");
std::filesystem::path root = "libstone/blackstone";
bool ok = mlvm::optilisp::run_kernels(
    module, root / "optilisp/stdlib", root / "kernels", diagnostics);
```

Include `<filesystem>` when naming the path type directly. Relative paths in
this fragment assume the MLVM source directory as the process working directory.
For installed data, pass `root / "stdlib"` as the registry directory.

## Embedding authored definitions

Read your Optilisp file into an owned `std::string`, append it to
`module.kernel_sources`, and append its declared ID to `kernel_applications`.
One source string may declare multiple kernels. Source declarations are validated
even when the application list is empty.

`print_ir(module)` serializes both declarations and pending requests. A host can
therefore assemble a portable textual optimization input through the API and
execute it later through the CLI with the appropriate registry available.

## Batch atomicity

`run_kernels` resolves and executes the whole request batch on a private module
copy. It commits only when every requested pass succeeds and verifies. On
failure, original IR and application requests remain intact; diagnostics persist.
On success, application requests are consumed and declarations remain.

This batch-wide guarantee is stronger than ordinary `PassManager` per-pass
commit behavior. If you use `kernel_pass` directly in a custom pipeline, its
transaction boundary is the pass manager's boundary unless you add an outer
candidate copy yourself.

Reference: [`run_kernels`](../include/mlvm/optilisp/pass.hpp), [execution rules](../docs/KERNELS.md).
