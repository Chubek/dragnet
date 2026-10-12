# 12. Verification and safe mutation

[Contents](README.md) · [Next: C++ IR](13-cpp-ir.md)

Verification is the boundary between a merely constructible module and a valid
module. Call `mlvm::verify(module, diagnostics)` after manual construction or
mutation. `parse_ir` returns no module when syntax or verification fails.

## What verification checks

Generic checks reject reused IDs, orphan definitions, unavailable operands,
stale use lists, and invalid registered operation arity. Dialect callbacks add
semantic checks such as integer type equality and function return compatibility.
The verifier does not infer unknown dialect semantics or supply absent CFG
dominance machinery.

## A safe replacement sequence

1. Identify the old result and proposed replacement by value ID.
2. Establish semantic equivalence for this candidate.
3. Call `replace_uses(old_id, replacement_id, diagnostics)`.
4. Erase the redundant operation once its results are unused.
5. Verify the resulting module, including dominance.

`replace_uses` checks type compatibility, but replacing a value with a later
definition can still violate dominance. Structural success is therefore not
enough to justify publication.

## Direct edits and transactions

If you edit operand vectors directly, call `rebuild_uses()` before verification.
That repairs bookkeeping; it does not repair illegal ownership or use-before-
definition. Avoid retaining vector references while inserting or erasing nodes.

For transformations that may fail partway through, use the pass manager or a
private module copy. A pass candidate is committed only after its callback and
verification succeed. Diagnostics survive rollback and should be reported.

Custom verifier callbacks must tolerate malformed arity. Even if a valid
operation normally has two operands, do not index `operands[1]` before checking
that the supplied object actually has it.

Reference: [verification contract](../docs/API.md), [IR regressions](../tests/core/ir.cpp).
