# Tunah implementation-round benchmarks

Measured on 2026-10-02 against commit `2ae124f8`, using GCC 16.2.1 with
`-std=c++20 -O2 -DNDEBUG` and the same supplied Equinox-NG/MetaTk headers.
Each result is the median of seven alternating before/after runs on the same
host. Pattern loading precedes the timed region; each call still constructs an
isolated e-graph. Tracing is enabled in both versions.

| Workload | Calls per sample | Before | After | Change |
|---|---:|---:|---:|---:|
| One `add-zero` rule | 10,000 | 54.377 ms | 55.732 ms | +2.49% |
| 156-rule wrapping-integer corpus | 200 | 232.208 ms | 208.485 ms | -10.22% |

## Reproduction method

For the one-rule workload, define `add` with arity 2, load
`(rule zero (add ?x 0) ?x)`, and repeatedly saturate
`(add (add x 0) 0)` with default limits/costs. Both versions extract `x` at cost 1
on every call, for cumulative cost 10,000.

For the corpus workload, define the shared scalar integer arities and load the
contents of `alg-sim.tuner`, `const-folding.tuner`, `codesize-reduce.tuner`,
`strength-reduct.tuner`, and `uarch-opt.tuner` through `load_rules`. Use anonymous
inline source names in both versions and retain the session across calls. Saturate:

```scheme
(iadd (imul (iadd x 0) 4)
      (iadd (imul (iadd x 0) 12) (imul y 0)))
```

Both versions saturate and extract `(ishl x 4)` at cost 5 on every call, for
cumulative cost 1,000. Neither reaches a resource limit.

Immutable prepared native patterns reduce repeated compilation work for the
representative corpus workload. Source-aware input validation and checked costs
add approximately 0.136 microseconds per call in the minimal workload. These
measurements characterize the two workloads, rather than all optimizer inputs;
extracted code quality is identical in both comparisons.
