# MLVM Blackstone extension

MLVM-owned hardening tooling and **50 supplemental optimization kernels**, using
the read-only Blackstone reader through `bridge.py`. The combined catalog has
**200 records**: 150 upstream plus 50 supplemental. Record count does not imply
that every upstream record satisfies the stricter validation profile.

## Commands

Run from the MLVM repository root; Python 3.9+ and the standard library suffice.
Imports do not modify `sys.path`, install dependencies, or write upstream bytecode.

```sh
python3 -B -m tools.blackstone validate
python3 -B -m tools.blackstone query --stage mir
python3 -B -m tools.blackstone query --requires-analysis alias-analysis --json
python3 -B -m tools.blackstone audit --json
python3 -B -m unittest discover -s tools/blackstone/tests -v
python3 -B -m tools.blackstone.author --check
```

`validate` and `query` default to the supplement; `audit` defaults to the combined
catalog. `--scope all` or `--scope supplement` selects explicitly. Auditing the
combined catalog currently exits **1** because upstream defects remain. Querying
a malformed catalog is rejected instead of presenting its entries as valid.

Exit status: 0 success, 1 invalid catalog, 2 usage error, 3 input/encoding error.
Errors go to stderr in text mode; JSON audit output includes structured issues.

## Kernel attributes and layout

Canonical definitions live at `kernels/<stage>/<domain>/<operation>.olisp`.
Stage and primary category remain explicit in metadata; paths do not supply
implicit applicability guarantees. Domain distinguishes the affected attribute.

| Stage | New kernels | Domains |
|---|---:|---|
| HIR | 20 | bitwise, shifts, unsigned |
| MIR | 20 | memory, control-flow, loops, calls, vectors |
| LIR | 10 | target, memory, registers, layout |

Combined stage counts: **64 HIR, 80 MIR, 56 LIR**.

The authoring table in `author.py` retains each kernel's own pattern, result,
specific proof obligation, and semantic boundary. It renders shared metadata
consistently into the checked-in Optilisp files. Consumers read those files;
tests reject authoring/source drift and unexpected supplemental files. Update
the table and run `python3 -B -m tools.blackstone.author` when authoring changes.
Existing upstream files are never regenerated.

## Strict profile 1

`catalog.py` validates original source forms before any lossy semantic extraction.
The grammar uses existing upstream forms rather than silently extending Optilisp.

- One kernel per file, one primary category, and one transformation objective.
- Canonical stage-qualified ID; stage agrees with the directory and ID.
- Unknown fields, duplicate singleton fields, malformed requirements, late
  keywords, empty mandatory values, and extra transform arguments rejected.
- Positive and negative conditions cannot contradict one another.
- Capabilities and analyses must resolve; capabilities must support the stage.
- Explicit analysis `requires`, `invalidates`, and `preserves` clauses. Consumed
  analyses match declarations; no analysis is both invalidated and preserved.
- Prerequisite references resolve; self-dependencies and dependency cycles fail.
- Identical stage/transform/precondition/forbidden-condition contracts are
  flagged as duplicates. This is structural detection, not equivalence proving.
- Fourteen documentation topics are required for each supplemental kernel.
- Reader input is bounded to 1 MiB and 128 nesting levels. Malformed escapes,
  unbalanced strings/lists, and non-finite numeric literals fail deterministically.
- Source-located diagnostics, sorted catalog traversal, and deterministic JSON.
- Chinese text is rejected in catalog source; supplemental code and documentation
  also have an English-only regression check. The upstream text scan found no
  Chinese text. Earlier conversation messages are not repository files.

The profile is deliberately stronger than upstream's implemented validator;
profile failures include missing contracts as well as actual reference defects.
See [UPSTREAM-ISSUES.md](UPSTREAM-ISSUES.md) for the distinction.

## Applicability and invalidation

`Fact` preserves five states: `TRUE`, `FALSE`, `UNKNOWN`, `NOT_COMPUTED`, and
`INVALIDATED`. Call `applicability(kernel, stage, facts)` only with records from a
catalog whose validation succeeded.

Fact keys are namespaced:

```text
capability:ssa-form
analysis:alias-analysis
condition:complete-byte-coverage-and-no-clobber-proven
history:blackstone.mir.some-family.some-transformation
```

Required facts must be `TRUE`. Forbidden facts must be **proven `FALSE`**.
Absent, unknown, not-computed, or invalidated facts never authorize a rewrite.
History requirements are checked separately from current IR properties.
Condition identifiers name proof obligations for the host adapter; this tooling
does not compute aliasing, prove non-trapping execution, or infer target opcodes.
Each kernel documents its specific obligations in first-class documentation.

`invalidate(kernel, facts)` returns a new map. Declared invalidations become
`INVALIDATED`; other analysis results are also invalidated unless explicitly
preserved. The input map is not mutated. Structural conditions and capabilities
must be refreshed by the host after any IR change; this function only models
analysis invalidation, not a complete planner or pass manager.

## Correctness versus profitability

Legality does not depend on a guessed performance improvement.
`profitability(kernel, facts)` reads the separate planner policy in
`profitability.json`. LIR selection requires both an available cost model and a
proven profitable replacement before the policy recommends applying it. Missing
cost facts do not make an otherwise legal kernel semantically incorrect.
HIR/MIR policy imposes no extra cost gate; this is not a claim of speedup.

## Evidence and limits

The regression suite covers malformed source, unresolved references, stage
mismatches, duplicate contracts, cycles, history, all five fact states, conservative
invalidation, CLI failure behavior, deterministic output, authoring drift, and
upstream content immutability. Every new kernel receives positive, negative,
unknown, invalidated, missing-fact, and stage-mismatch applicability checks.

The 20 HIR identities additionally evaluate their actual input/result expressions
against finite-width reference models: exhaustive defined bitvectors at widths
1–5 and boundary/seeded samples at widths 8, 16, 32, 64, and 128. Width-correct
masks, unsigned division, zero shift counts, and defined operands are essential.

MIR/LIR execution, NodeIR invariants, and full semantic equivalence are not
verified by this extension. Their tests validate contracts and applicability;
their documentation states the absent execution evidence explicitly. No formal
proof or universal profitability claim is made.

## CMake integration

When Python is available, `MLVM_BUILD_TESTS=ON` registers
`mlvm-blackstone-tests` and `mlvm-blackstone-catalog`. `MLVM_BUILD_TOOLS=ON`
provides the opt-in `mlvm-blackstone-validate` target. Configuration remains
offline and Python is optional for the C/C++ build.

```sh
cmake -S . -B build -DMLVM_BUILD_TESTS=ON
cmake --build build --target mlvm-blackstone-validate
ctest --test-dir build -R mlvm-blackstone --output-on-failure
```
