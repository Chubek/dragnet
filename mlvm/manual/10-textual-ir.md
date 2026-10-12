# 10. Textual IR syntax

[Contents](README.md) · [Next: Builtin operations](11-builtin-dialects.md)

Generic text starts with `mlvm 1`, followed by a braced module. Directives precede
blocks; operations live in blocks. This compact grammar summarizes the layout:

```ebnf
module    = "mlvm", "1", "{", { directive }, { block }, "}" ;
directive = ( "kernel", string | "apply", string ), ";" ;
block     = "block", "(", [ argument, { ",", argument } ], ")",
            "{", { operation }, "}" ;
argument  = value, ":", string ;
operation = [ argument, { ",", argument }, "=" ], string,
            "(", [ value, { ",", value } ], ")",
            "[", [ attribute, { ",", attribute } ], "]",
            { "region", "{", { block }, "}" }, ";" ;
attribute = string, "=", string ;
value     = "%", name ;
```

## Names, strings, and ordering

Operation names, types, attribute keys, and attribute values are quoted strings.
Strings support `\\`, `\"`, `\n`, `\r`, and `\t`. `//` introduces a comment
outside a string. Whitespace is otherwise insignificant.

Every textual SSA name is unique across the entire module. Operands refer to
previous definitions; the verifier additionally checks visibility and dominance.
Use explicit `()` and `[]` even when both lists are empty. A region-containing
operation still needs its final semicolon after the region closes.

## Canonical printing

The printer renames values sequentially in traversal order, sorts attribute keys,
and emits explicit empty lists. Compare canonical output when testing round trips
rather than expecting preservation of comments or author-selected SSA names.
Nesting is bounded at 256.

Kernel directives are retained data at parse time. `kernel` embeds complete
Optilisp source; `apply` records a stable kernel ID. Their execution belongs to
the optional optimizer integration, not the text parser.

Printing malformed in-memory IR can help diagnose it, but is not verification.
Use `parse_ir` or `verify` to establish validity.

Reference: [version-1 grammar](../docs/IR.md).
