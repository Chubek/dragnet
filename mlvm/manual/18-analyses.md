# 18. Analysis caching and preservation

[Contents](README.md) · [Next: Builtin passes](19-builtin-passes.md)

`AnalysisManager` maps analysis names to producers and caches their `std::any`
results. A producer receives a const module and should compute self-contained
facts without mutating the module or external state.

For example, register an operation-count analysis before scheduling consumers:

```cpp
manager.analyses.register_analysis("operation-count",
    [](const mlvm::Module& module) -> std::any {
        std::size_t count = 0;
        mlvm::walk(module, [&](const mlvm::Operation&) { ++count; });
        return count;
    });
```

A pass can name `operation-count` in `requires_analyses`, call
`analyses.get("operation-count", module)`, and recover the value with a checked
`std::any_cast<std::size_t>`. Check both the returned pointer and the cast result
when using the pointer form of `any_cast`.

## Owning results

Store counts, IDs, sets, and independently owned structures. Do not cache
`Operation*`, vector references, or views into a pass candidate. Candidate copies
can be committed or discarded, and later mutations can invalidate storage even
while IDs remain useful.

## Preservation is a correctness claim

Passes list preserved analysis names in `preserves_analyses`. Everything else
is invalidated. An operation-count analysis cannot be preserved by a pass that
adds or deletes operations merely because the count was inexpensive to compute.
Start with conservative invalidation; optimize preservation only when the
invariant is demonstrated.

Outside `PassManager`, associate an `AnalysisManager` with one module and call
`invalidate()` after mutations. The cache is not a multi-module database keyed
by module identity. Analysis dependency tracking is not implemented, so producers
and preservation rules must not assume automatic transitive invalidation logic.

Reference: [`AnalysisManager`](../include/mlvm/pass.hpp), [analysis ownership](../docs/API.md).
