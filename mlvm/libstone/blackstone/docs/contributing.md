# Contributing to Blackstone

## Adding a new kernel

1. **Define the kernel contract first**: objective, stage, input patterns, semantic
   assumptions, capabilities, analyses, preconditions, forbidden conditions,
   transformation, correctness obligations, expected effects, limitations, tests.

2. **Inspect existing patterns**: find the closest existing kernel and reuse its
   conventions.

3. **Choose the directory**: organize by stage → category.

4. **Write the kernel**: one `define-kernel` form with a stable ID following
   `blackstone.<stage>.<family>.<operation>`.

5. **Validate**: run `./tools/validate-kernels` and `make test`.

6. **Document**: ensure first-class documentation covers purpose, motivation,
   applicability, preconditions, semantic assumptions, correctness, limitations,
   and testing strategy.

## Adding a capability or analysis

1. Add a `define-capability` or `define-analysis` form to the appropriate file in
   `optilisp/stdlib/capabilities/` or `optilisp/stdlib/analyses/`.
2. Run `./tools/validate-kernels` to verify.

## Adding a monoid

1. Add a `define-monoid` form to `optilisp/stdlib/monoids/`.
2. Run `./tools/check-monoid-laws` to verify the laws.

## Code style

- Follow the existing Optilisp form structure
- Keep kernels focused on one optimization
- Use explicit, machine-readable metadata
- Write precise documentation

## Validation

Before submitting changes:

```sh
make test
make validate
make monoids
```
