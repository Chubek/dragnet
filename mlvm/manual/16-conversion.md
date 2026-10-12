# 16. Dialect conversion

[Contents](README.md) · [Next: Pass pipelines](17-pass-pipelines.md)

`ConversionGraph` models directed rewrite edges between dialects. It finds
shortest routes by breadth-first search, using edge-registration order to break
equal-length ties. A route from `high` to `low` does not establish a reverse route.

## Declaring a route

Assume `high.op` and `low.op` are registered operations with the same empty shape
and intentionally equivalent semantics. A conversion fragment is:

```cpp
mlvm::ConversionGraph graph;
bool added = graph.add({"high", "low",
    [](mlvm::Operation& op, mlvm::Module&, mlvm::DiagnosticEngine&) {
        if (op.name != "high.op") return false;
        op.name = "low.op";
        return true;
    }});
bool converted = added && graph.convert(module, {"low"}, diagnostics);
```

Include `mlvm/conversion.hpp`. This is a deliberately small example; changing
names is a valid lowering only when both operation contracts justify it.

## Full legality

The `legal` set names dialects that may remain in the result. Every resulting
operation must belong to a legal registered dialect and pass verification.
Include retained builtin dialects such as `func` or `arith` if the module still
uses them. The implementation provides full conversion, not a partial-conversion
mode that leaves arbitrary illegal operations behind.

Conversion runs transactionally. A missing route, failing rewrite, emitted
error, or invalid result prevents publication; diagnostics remain available.
Each rewrite must produce the edge's target dialect.

## Traversal restrictions

A callback may edit the current operation, its operands/results, and children.
It must not structurally mutate sibling operations or ancestors while traversal
is active. For a lowering needing broader restructuring, design an appropriate
pass boundary rather than invalidating the conversion walk.

Reference: [conversion API](../include/mlvm/conversion.hpp),
[route and rollback examples](../tests/conversion/conversion.cpp).
