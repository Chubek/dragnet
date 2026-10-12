# Kernel Schema

Every kernel must provide the following metadata (spec section 5.1):

## Required fields

| Field | Type | Description |
|-------|------|-------------|
| `:id` | string | Stable unique identifier (`blackstone.<stage>.<family>.<operation>`) |
| `:name` | string | Human-readable name |
| `:stage` | `hir` \| `mir` \| `lir` | Target NodeIR stage |
| `(category ...)` | list | One or more optimization categories |
| `(objective ...)` | string | Primary optimization objective |
| `(requires ...)` | list | Required capabilities and analyses |
| `(preconditions ...)` | list | Semantic preconditions |
| `(transform ...)` | form | Intended transformation |
| `(correctness ...)` | list | Correctness obligations |
| `(expected-effects ...)` | string | Expected effects |
| `(limitations ...)` | string | Known limitations |
| `(documentation ...)` | form | First-class documentation |

## Optional fields

| Field | Type | Description |
|-------|------|-------------|
| `(paradigms ...)` | list | Optimization paradigms |
| `(forbids ...)` | list | Forbidden conditions |
| `(prerequisites ...)` | list | Required prior transformations |
| `(forbidden-prior-transformations ...)` | list | Forbidden prior transformations |
| `(analysis-contract ...)` | form | Analysis requirements and invalidations |

## JSON Schema

The structural schema is defined in `schemas/kernel.schema.json`. It enforces:

- Required fields are present
- Field types are correct
- Stage is one of `hir`, `mir`, `lir`
- ID matches the `blackstone.<stage>.<family>.<operation>` pattern
- No additional properties are allowed

## Validation layers

See `docs/architecture.md` for the six-layer validation model.
