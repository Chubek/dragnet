# Optilisp Language Reference

Optilisp is Blackstone's declarative S-expression language for expressing optimization
kernels, metadata, and related specifications.

## Surface grammar

```text
program  := form*
form     := list | atom
list     := '(' form* ')'
atom     := symbol | keyword | string | number | boolean | nil
```

- **Symbol**: `integer-add`, `blackstone.hir.constant-folding.integer-add`
- **Keyword**: `:id`, `:stage`, `:name`
- **String**: `"..."` with escapes `\"`, `\\`, `\n`, `\t`, `\r`, `\0`
- **Number**: `42`, `-7`, `3.14`
- **Boolean**: `true`, `false`
- **Nil**: `nil`
- **Comment**: `;` to end of line

## Top-level forms

### `define-kernel`

Defines an optimization kernel.

```lisp
(define-kernel
  :id "blackstone.<stage>.<family>.<operation>"
  :name "Human-Readable Name"
  :stage hir | mir | lir

  (category <category> ...)
  (paradigms <paradigm> ...)
  (objective "...")

  (requires
    (capability <name>) ...
    (analysis <name>) ...)

  (preconditions <condition> ...)
  (forbids <condition> ...)

  (prerequisites
    (optimization-applied "<kernel-id>") ...)

  (forbidden-prior-transformations "<kernel-id>" ...)

  (analysis-contract
    (requires <name> ...)
    (invalidates <name> ...)
    (preserves <name> ...))

  (transform (<verb> <args> ...))

  (correctness <obligation> ...)
  (expected-effects "...")
  (limitations "...")

  (documentation
    (purpose "...")
    (motivation "...")
    ...))
```

### `define-monoid`

Defines a standard-library monoid.

```lisp
(define-monoid
  :id "blackstone.monoid.<name>"
  :name "Monoid Name"
  :carrier <carrier>
  :operation <operation>
  :identity <identity>
  (laws
    (associative <operation>)
    (left-identity <operation> <identity>)
    (right-identity <operation> <identity>))
  (description "..."))
```

### `define-capability`

Defines a NodeIR capability.

```lisp
(define-capability
  :id "<name>"
  :name "Capability Name"
  :stages (hir mir lir)
  (description "..."))
```

### `define-analysis`

Defines a program analysis.

```lisp
(define-analysis
  :id "<name>"
  :name "Analysis Name"
  (description "..."))
```

## Transformation verbs

Each kernel uses exactly one primary transformation verb:

| Verb | Description |
|------|-------------|
| `rewrite` | General pattern rewrite |
| `fold` | Constant folding |
| `propagate` | Constant/value propagation |
| `eliminate` | Remove dead code or stores |
| `hoist` | Move computation to an earlier point |
| `sink` | Move computation to a later point |
| `interchange` | Exchange loop nesting order |
| `unroll` | Replicate loop body |
| `peel` | Extract first/last loop iteration |
| `fuse` | Merge adjacent loops |
| `distribute` | Split loop into multiple loops |
| `tile` | Tile a loop nest |
| `skew` | Skew a loop nest |
| `inline` | Inline a function call |
| `specialize` | Create a specialized copy |
| `devirtualize` | Replace virtual call with direct call |
| `lower` | Lower to a lower-level representation |
| `select` | Select target instructions |
| `combine` | Combine multiple operations into one |
| `promote` | Promote to a higher-level representation |
| `demote` | Demote to a lower-level representation |
| `coalesce` | Merge registers or slots |
| `rematerialize` | Recompute instead of spilling |
| `forward` | Forward a stored value to a load |
| `canonicalize` | Rewrite to canonical form |
| `simplify` | Simplify an expression |
| `split` | Split a live range or block |
| `merge` | Merge blocks or functions |
| `rotate` | Rotate a loop |
| `vectorize` | Transform to vector operations |
| `parallelize` | Transform to parallel execution |
| `if-convert` | Convert branch to select/predication |
| `construct` | Construct SSA form |
| `outline` | Extract a function |
| `place` | Order basic blocks |
| `schedule` | Schedule instructions |
| `predict` | Predict branches |
| `deduplicate` | Merge identical constants or blocks |
| `remove` | Remove redundant operations |
| `strength-reduce` | Replace expensive operation with cheaper one |

## Stage-appropriate verbs

Not all verbs are valid at all stages:

- **HIR**: `rewrite`, `fold`, `propagate`, `eliminate`, `hoist`, `sink`, `interchange`, `unroll`, `peel`, `fuse`, `distribute`, `tile`, `inline`, `specialize`, `canonicalize`, `simplify`, `split`, `merge`, `deduplicate`, `remove`, `strength-reduce`
- **MIR**: all HIR verbs plus `skew`, `devirtualize`, `promote`, `forward`, `vectorize`, `parallelize`, `if-convert`, `construct`, `outline`, `coalesce`
- **LIR**: `rewrite`, `fold`, `eliminate`, `hoist`, `lower`, `select`, `combine`, `promote`, `demote`, `coalesce`, `rematerialize`, `canonicalize`, `simplify`, `split`, `merge`, `place`, `schedule`, `deduplicate`, `remove`, `strength-reduce`, `sink`

## Comments

```lisp
; This is a comment to end of line
```
