# 15. Defining custom dialects

[Contents](README.md) · [Next: Conversion](16-conversion.md)

A dialect gives meaning to operation names, type spellings, attributes, and
verification rules. Register the dialect before its operations. Duplicate
explicit definitions fail diagnostically rather than silently replacing an
existing contract.

For an existing `Context` and `DiagnosticEngine`, this fragment registers a pure
zero-operand marker operation:

```cpp
if (!context.register_dialect("demo", diagnostics))
    return false;
mlvm::OperationDefinition marker;
marker.name = "demo.marker";
marker.effect = mlvm::Effect::pure;
if (!context.register_operation(std::move(marker), diagnostics))
    return false;
```

The fragment belongs in a function returning `bool`; include `<utility>` for
`std::move`. Default arity bounds are zero, so this definition permits no
operands, results, or regions. Text using it is `"demo.marker"() [];` after
registration. The stock CLI does not discover application-defined registrations
automatically; embed the parser in the registering host.

## Specifying semantics

Set minimum and maximum operand, result, and region counts. Mark terminators
explicitly. Effects are `pure`, `read`, `write`, or `unknown`; DCE relies on that
classification. An operation incorrectly marked pure can become removable even
when it should have observable behavior.

Use the custom `verify` callback for relationships not expressible as counts:
type compatibility, required attributes, region shape, or enclosing constraints.
The callback receives the operation, module, and diagnostic engine and must
handle malformed input safely.

Custom type names are interned strings. Registering the spelling does not give
the compiler layout, arithmetic, ABI, or conversion rules. Supply those in
explicit dialect and backend implementations before using them in lowering.

Reference: [`OperationDefinition`](../include/mlvm/core.hpp), [API rules](../docs/API.md).
