# 9. The generic SSA object model

[Contents](README.md) · [Next: Textual syntax](10-textual-ir.md)

The generic representation in `mlvm/ir.hpp` is an owned hierarchy with explicit
SSA definition identity. A module has a body region, a value table, allocation
counters, and optional kernel declarations/application requests.

## Structural elements

| Element | Important data |
| --- | --- |
| `Region` | Ordered blocks |
| `Block` | Node ID, argument value IDs, ordered operations |
| `Operation` | Node ID, name, operand/result IDs, attributes, regions, location |
| `Value` | Value ID, type identifier, owner, argument flag, ordered use list |
| `Use` | Consumer operation ID and operand position |

Operation attributes are ordered string-to-string mappings. A type is a
context-interned identifier, not an independently owned structural type object.
Dialect verifiers establish the meaning of these spellings and attributes.

## Definitions and uses

An operation result and a block argument both define SSA values. Operands refer
to those values by ID. Explicit use lists make replacement and deletion
checkable: removing a definition that still has users is not a valid mutation.

Within a block, definitions must precede their uses. Nested regions may see
eligible enclosing definitions, but functions isolate their bodies from enclosing
SSA values. The current generic verifier does not provide CFG successor edges or
multi-block dominance. Having a vector of blocks is not equivalent to having a
complete control-flow representation.

## Identity is semantic information

Two operations computing identical expressions still have different result IDs.
This matters for repeated-variable kernel patterns: `(bitwise-and x x)` requires
the same SSA definition in both operand positions, not two merely equal-looking
computations.

Use `append`, `replace_uses`, and `erase` to maintain bookkeeping. Direct changes
to operand vectors require `rebuild_uses()` and subsequent verification.

Reference: [`ir.hpp`](../include/mlvm/ir.hpp).
