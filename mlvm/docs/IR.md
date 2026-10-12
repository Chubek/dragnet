# Generic IR, version 1

```ebnf
module    = "mlvm", "1", "{", { directive }, { block }, "}" ;
directive = ( "kernel", string | "apply", string ), ";" ;
block     = "block", "(", [ argument, { ",", argument } ], ")", "{", { operation }, "}" ;
argument  = value, ":", string ;
operation = [ argument, { ",", argument }, "=" ], string,
            "(", [ value, { ",", value } ], ")",
            "[", [ attribute, { ",", attribute } ], "]",
            { "region", "{", { block }, "}" }, ";" ;
attribute = string, "=", string ;
value     = "%", name ;
```

Strings use quoted UTF-8 byte sequences with `\\`, `\"`, `\n`, `\r`, `\t`
escapes. Whitespace and `//` comments are ignored outside strings. Every SSA name
is unique across a textual module; operands name previous definitions. The
verifier additionally enforces block-local dominance and enclosing-region visibility.
Functions are isolated from enclosing SSA values. Nesting is bounded at 256.
The canonical printer assigns sequential SSA names in traversal order, sorts
attribute keys, and emits explicit empty operand/attribute lists. Text carries no
pointers, padding or unordered iteration; source locations refer to parsed text
and are not serialized. Printing malformed IR is diagnostic aid, not validation.

`kernel` embeds complete Optilisp source; `apply` schedules a stable kernel ID.
Parsing retains definitions and ordered requests without executing them. The
Optilisp-enabled driver executes requests before ordinary passes unless invoked
with `--print` or `--verify`. The printer retains definitions and pending requests.
See `KERNELS.md` for authoring, supported execution contracts, and C++ APIs.

## Builtin operations

| Operation | Operands | Results | Regions | Semantics |
| --- | --- | --- | --- | --- |
| `builtin.func` | 0 | 0 | 1 | Unique `sym`, one entry block, optional `result` type (default void) |
| `func.return` | 0 or 1 | 0 | 0 | Terminator inside function; matches enclosing result type |
| `arith.constant` | 0 | 1 | 0 | Integer bit pattern, decimal `value` |
| `arith.addi` | 2 | 1 | 0 | Same-width modular integer addition |
| `arith.subi` | 2 | 1 | 0 | Same-width modular integer subtraction |
| `arith.muli` | 2 | 1 | 0 | Same-width modular integer multiplication |
| `arith.divsi` | 2 | 1 | 0 | Same-width signed division; division by zero and signed overflow are undefined and never folded |
| `arith.remsi` | 2 | 1 | 0 | Same-width signed remainder; zero divisor is undefined and never folded |
| `arith.and` | 2 | 1 | 0 | Same-width bitwise AND |
| `arith.or` | 2 | 1 | 0 | Same-width bitwise OR |
| `arith.xor` | 2 | 1 | 0 | Same-width bitwise XOR |

Arithmetic accepts identical `i1` through `i64` types. Constant text accepts signed
decimal int64 values; for narrower types nonnegative bit patterns up to `2^width-1`
are also accepted. `constant-fold` uses unsigned modular arithmetic for
`addi`/`subi`/`muli`/`and`/`or`/`xor` and checked signed arithmetic for
`divsi`/`remsi` (skipping undefined cases), preserving source locations and
avoiding host signed overflow. `canonicalize` rewrites identity operations
(`add`/`or`/`xor` with zero, `mul` with one, `sub`/`xor` of a value with itself)
by forwarding the surviving operand or a zero constant; it performs no
reassociation or distribution. `dce` removes only unused
pure, non-terminator operations without regions; unknown/read/write effects remain.

This release does not implement CFG successor edges, multi-block dominance,
function-call semantics, memory/atomic/volatile operations, structured-control-flow
lowering, or builtin type/attribute schemas beyond the operations above. Custom
dialects can register definitions and verifiers; no absent semantics are inferred.

`examples/answer.mlvm` is a round-trip and optimization example. Round-trip tests
compare canonical text directly; no generated golden-file update is required.
