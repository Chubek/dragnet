# Monoids

Blackstone uses monoidal abstractions to describe composition and accumulation where
the semantics are appropriate (spec section 8).

## Requirements

Every standard-library monoid must define:

- A stable identifier
- A carrier type or value domain
- A binary operation
- An identity element
- The applicable equality or equivalence relation
- The conditions under which the monoid laws hold

## Standard library

The following monoids are defined in `optilisp/stdlib/monoids/`:

| Monoid | Carrier | Operation | Identity |
|--------|---------|-----------|----------|
| Integer Addition | `integer` | `integer-add` | `0` |
| Integer Multiplication | `integer` | `integer-mul` | `1` |
| Boolean Conjunction | `boolean` | `boolean-and` | `true` |
| Boolean Disjunction | `boolean` | `boolean-or` | `false` |
| Set Union | `set` | `set-union` | `empty-set` |
| Set Intersection | `set` | `set-intersection` | `universal-set` |
| Sequence Concatenation | `node-sequence` | `sequence-concatenate` | `empty-sequence` |
| Map Merge | `map` | `map-merge` | `empty-map` |
| Diagnostic Accumulation | `diagnostic-list` | `diagnostic-append` | `empty-diagnostic-list` |
| Transformation Composition | `transformation` | `transformation-compose` | `identity-transformation` |

## Law verification

Monoids with executable carriers are tested against their declared laws:

```sh
./tools/check-monoid-laws
```

This verifies associativity, left identity, and right identity over sampled values.

## Cautions

- A monoid must not be used merely to make a description look algebraic
- The chosen operation must satisfy its laws under the specified semantics
- Sequence concatenation is associative, but execution order may still be semantically significant
- Transformation composition requires compatible input/output contracts
