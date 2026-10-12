# 25. Kernel contracts, proofs, and validation

[Contents](README.md) · [Next: Host optimizers](26-host-optimizers.md)

Optilisp validation has several layers. The reader establishes valid bounded
syntax. The loader checks declarations and registry references. A backend checks
supported transformations and proves applicability to actual candidates.

## Reader and loader

The reader accepts lists, symbols, keywords, strings, decimal numbers, booleans,
`nil`, and semicolon comments. Numbers retain their source spelling. Quote syntax,
binary atoms, atom escapes, empty keywords, and raw NUL bytes are rejected.
Byte, node, and nesting limits are checked before AST publication.

Loader validation covers metadata, documentation, duplicate fields/IDs,
stage compatibility, capability/analysis references, contradictory conditions,
analysis contracts, transformation shape, and prerequisite cycles. A diagnostic
discards the loaded catalog; installation revalidates before invoking a backend.

`Loader::Profile::strict` is the supplemental default. The explicit `blackstone`
profile accepts the original six-topic documentation schema. When its analysis
contract is absent, declared analyses are consumed, all registered analyses are
invalidated, and none are preserved. It does not repair invalid references.

## Generic integer execution domain

Supported constructors are `add`, `sub`, `mul`, `bitwise-and`, `bitwise-or`, and
`bitwise-xor`. Patterns can bind operands and `(integer LITERAL-OR-VARIABLE)`
constants. A fold must compute the same operator from the same literal bindings;
a rewrite currently forwards bound operands and must satisfy an implemented
integer identity. Operations with extra attributes are conservatively untouched.

Recognized positive predicates concern defined compile-time semantics,
representability, compatible integer types, zero-addition identity, and
one-multiplication identity. Recognized forbidden predicates concern observable
effects and trapping signed overflow. Unknown contracts do not become true by
default. The precise identifier list is in the kernel reference.

## Proof obligations

In general rewrite backends, preconditions need `proven_true`; forbidden
conditions need `proven_false`. Unknown or invalidated evidence blocks rewriting.
Matching syntax is not a proof of semantic equivalence. Tests demonstrate the
implemented adapter's integer behavior, not correctness of every catalog rule
for every possible IR.

Reference: [exact supported contracts](../docs/KERNELS.md#execution-and-validation-contracts),
[upstream findings](../tools/blackstone/UPSTREAM-ISSUES.md).
