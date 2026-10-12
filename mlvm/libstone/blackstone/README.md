# Blackstone

A declarative database of compiler and interpreter optimization kernels.

Blackstone catalogs individual optimization techniques as declarative, machine-readable
kernel specifications written in **Optilisp**, Blackstone's S-expression language. Each
kernel describes one optimization, its applicability conditions, required analyses and
capabilities, its correctness obligations, and its intended transformation.

Blackstone is a member of the `libstone` project family. It is **not** a compiler,
interpreter, or optimization executor — it is the kernel database and its tooling.

## Repository layout

```text
blackstone/
├── AGENTS.md                  # Normative specification and implementation guide
├── README.md                  # This file
├── docs/                      # Architecture, schema, and language documentation
├── schemas/                   # JSON Schemas for kernel, capability, analysis, monoid
├── kernels/                   # 150 kernel definitions, organized by stage → category
│   ├── hir/                   #   High-level IR kernels (44)
│   ├── mir/                   #   Mid-level IR kernels (60)
│   └── lir/                   #   Low-level IR kernels (46)
├── optilisp/                  # Optilisp language implementation (Python 3, stdlib only)
│   ├── src/optilisp/          #   Reader, semantic analyzer, validator, query engine
│   ├── stdlib/                #   Capability, analysis, and monoid registries
│   └── examples/              #   Example Optilisp definitions
├── tools/                     # Command-line tools
│   ├── validate-kernels       #   Run all six validation layers
│   ├── inspect-kernel         #   Display one kernel's metadata
│   ├── query-kernels          #   Query the kernel database
│   └── check-monoid-laws      #   Verify monoid laws for executable carriers
└── tests/                     # Test suite (parser, schema, semantics, validation)
```

## Kernel organization

Kernels are organized by **attributes** in directories:

- **First attribute: IR stage** — `hir/`, `mir/`, `lir/`
- **Second attribute: optimization category** — e.g., `constant-folding/`, `loop-optimization/`

Each kernel file contains one `define-kernel` form with a stable identifier following the
convention `blackstone.<stage>.<family>.<operation>`.

## Quick start

```sh
# Validate the entire kernel database (all six layers)
./tools/validate-kernels

# Query the database
./tools/query-kernels --stage mir --category loop-optimization
./tools/query-kernels --summary

# Inspect a single kernel
./tools/inspect-kernel blackstone.hir.constant-folding.integer-add

# Verify monoid laws
./tools/check-monoid-laws

# Run the test suite
make test
```

## Optilisp language

Optilisp is a declarative S-expression language. Its surface grammar:

```text
program  := form*
form     := list | atom
list     := '(' form* ')'
atom     := symbol | keyword | string | number | boolean | nil
```

The reader is generic and knows nothing about semantic forms. Domain knowledge lives in
the semantic analyzer and validator, keeping syntax and semantics separate.

### Kernel form

```lisp
(define-kernel
  :id "blackstone.hir.constant-folding.integer-add"
  :name "Integer Addition Constant Folding"
  :stage hir

  (category constant-evaluation)
  (paradigms algebraic-rewriting rule-based)
  (objective "Evaluate an integer addition whose operands are both known constants.")

  (requires
    (capability typed-expressions)
    (analysis constant-value-analysis))

  (preconditions
    (operation-has-defined-compile-time-semantics))

  (transform
    (fold (add (integer a) (integer b)) (integer-result (integer-add a b))))

  (correctness (preserve-observable-behavior))
  (expected-effects "Replaces a constant integer addition with its compile-time value.")
  (limitations "Requires both operands to be known constants.")

  (documentation
    (purpose "Fold a constant integer addition into its compile-time result.")))
```

## Validation layers

Blackstone validates in six distinct layers (spec section 17):

1. **Syntax** — balanced parens, well-formed forms, valid grammar
2. **Structural** — required fields, correct types, recognized stage
3. **References** — unique IDs, resolvable cross-references
4. **Semantic** — stage compatibility, contradictory contracts
5. **Transformation** — verb vocabulary, stage-appropriate transforms
6. **Database** — deterministic load, indexes, global policy

## Tests

```sh
make test
```

The test suite covers:
- Parser (Layer 1): valid/invalid syntax, escapes, locations
- Semantic analysis: form extraction, registry records
- Validation (Layers 2–6): structural, reference, semantic, transformation, database
- Query: filtering by stage, category, capability, analysis, verb, text
- Schema: JSON Schema validation
- Monoid laws: executable law verification

## Design principles

- **One kernel, one optimization** — each kernel has one primary transformation objective
- **Declarative descriptions** — kernels describe transformations and contracts, not imperative code
- **Explicit applicability** — kernels declare when they are valid
- **Explicit IR stage** — every kernel identifies its target NodeIR stage
- **Correctness first** — transformations preserve observable behavior under stated assumptions
- **Machine-readable metadata** — kernels are parseable, validated, indexed, and queryable
- **Separation of concerns** — database, validator, and executor remain distinct

## Dependencies

- Python 3.8+ (standard library only)
- No external dependencies

## License

See `AGENTS.md` for the full specification. Blackstone is part of the `libstone` project.
