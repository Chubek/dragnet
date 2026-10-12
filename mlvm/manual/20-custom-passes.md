# 20. Writing and observing passes

[Contents](README.md) · [Next: Blackstone](21-blackstone.md)

A custom pass provides a name and a callback with the signature
`bool(Module&, AnalysisManager&, DiagnosticEngine&)`. The callback must establish
the semantic validity of its changes; post-pass verification checks representational
invariants rather than general equivalence.

This fragment installs a read-only audit pass that rejects empty modules:

```cpp
mlvm::Pass audit;
audit.name = "require-body";
audit.run = [](mlvm::Module& module, mlvm::AnalysisManager&,
               mlvm::DiagnosticEngine& diagnostics) {
    if (!module.body.blocks.empty()) return true;
    diagnostics.error("client.empty", "Expected at least one body block");
    return false;
};
manager.add(std::move(audit));
```

Include `mlvm/pass.hpp` and `<utility>`. For mutating passes, prefer module
mutation helpers and IDs over references held across vector changes. Declare
required and preserved analyses according to the actual transformation.

## Instrumentation

`PassManager::instrumentation` receives `PassEvent` values with pass name,
success, elapsed time, and optional before/after dumps. Enable `dump_ir` to
request canonical snapshots:

```cpp
manager.dump_ir = true;
manager.instrumentation = [](const mlvm::PassEvent& event) {
    std::cerr << event.name << ": " << (event.success ? "ok" : "failed")
              << " (" << event.elapsed.count() << " ns)\n";
};
```

This fragment additionally needs `<iostream>`. Keep callbacks observational;
external side effects are not part of module rollback. Timing is useful for
measurement but not a deterministic golden-test value. Canonical dumps are
better for identifying the first pass that changed IR unexpectedly.

Test a pass with a successful candidate, a semantic nonmatch, and a failure after
partial edits. Confirm that the last case leaves the pre-pass module intact.

Reference: [events and callbacks](../include/mlvm/pass.hpp), [pass tests](../tests/pass/pass.cpp).
