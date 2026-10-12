# Chapter 10. Tunah Equality Saturation

[Previous: Allocation](09-regtl-allocation-and-spilling.md) · [Contents](README.md) · [Next: MachineIR](11-machineir-and-cpp-d-exchange.md)

## 10.1 Equivalences before implementation choices

Tunah maintains alternative equivalent terms in an e-graph, applies justified
rewrite rules under explicit bounds, and extracts a low-cost representative.
The core engine is the supplied Equinox-NG implementation. Limestone provides
rule ingestion, source-located diagnostics, host predicates, bounded driving,
cost configuration, and checked adapters into concrete ILs.

An e-graph records equalities asserted by the rule system. It does not discover
an arbitrary language's overflow, memory, or ABI semantics from operator names.
The host must justify its rules for the exact IL vocabulary and value domains.
This is particularly important when the same spelling `add` could mean checked,
wrapping, floating-point, vector, or architectural-flag-setting arithmetic.

The APIs are [tunah/tunah.hpp](../tunah/tunah.hpp),
[unisel_adapter.hpp](../tunah/unisel_adapter.hpp), and
[binary_adapter.hpp](../tunah/binary_adapter.hpp).

## 10.2 Operators, terms, and rule syntax

```scheme
(operator iadd 2)
(operator imul 2)
(operator ishl 2)
(rule add-zero (iadd ?x 0) ?x)
(rule multiply-four (imul ?x 4) (ishl ?x 2))
```

Applications need declared operator arities, including nullary applications.
Bare symbols can represent opaque values. Generic integer literals are signed
64-bit decimal values; overflowing literals are rejected. Variables begin with
`?`, and replacement variables must already be bound by the left-hand pattern.

Declarations apply throughout one load, including before their textual position.
Identical declarations are idempotent; conflicting arities and duplicate rule
names fail. Loading is transactional: failed compilation adds neither partial
rules nor partial declarations to the session.

`parse_term` and `format_term` provide a structured term boundary. Source text is
bounded to 4 MiB and S-expression nesting to 256 levels. Parsed terms retain
locations independently of input buffers.

## 10.3 Rule ingestion and preprocessing

The ingestion path uses EkippX preprocessing, SExprTk event parsing, and DSLtk
syntax structures before compiling native Equinox-NG patterns. These vendor
types stay private to the library.

Supported per-load macros include:

```text
@define(ZERO, 0)
(operator iadd 2)
(rule add-zero (iadd ?x &ZERO()) ?x)
```

`@deflit` is also supported. Macro definitions do not leak into later loads,
comments cannot invoke them, and unknown preprocessing directives/functions fail.
Other preprocessing facilities need explicit adapters.

When preprocessing changes the stream, locations are marked as expanded-stream
coordinates. Diagnostics do not pretend to have an exact original-source map.
The generated `tuner.g` parser represents post-preprocessing syntax; it is a
separate syntax-tree boundary. A parsed `:cost` clause does not configure Tunah's
current semantic engine: extraction weights belong to `CostModel`.

## 10.4 A complete standalone session example

```cpp
#include <tunah/tunah.hpp>
#include <iostream>

int main() {
  limestone::tunah::Session session;
  auto loaded = session.load_rules(
      "(operator add 2)(rule zero (add ?x 0) ?x)", "zero.rules");
  if (!loaded) {
    std::cerr << loaded.error().message << '\n';
    return 1;
  }
  auto result = session.saturate("(add 42 0)");
  if (!result) {
    std::cerr << result.error().message << '\n';
    return 1;
  }
  std::cout << result.value().expression << '\n';
}
```

This returns `42` under the explicitly supplied equivalence. Link
`Limestone::tunah`. Each saturation run owns a fresh e-graph, while prepared rule
patterns are immutable and reusable.

The result includes structured and printed terms, extraction cost, node/class
counts, rewrite and iteration counts, saturation status, limit status, and optional
trace. Keep those statistics when comparing rule sets.

## 10.5 Analysis predicates as proof boundaries

Some rules need conditions:

```scheme
(rule widen-add
  (sext32to64 (iadd ?x ?y))
  (iadd (sext32to64 ?x) (sext32to64 ?y))
  :where (signed-add-fits32 ?x ?y))
```

Register the predicate before loading the rule. A predicate receives equivalent
structured representatives of matched e-classes. It returns true only when its
host analysis proves the required condition. False means unknown or inapplicable;
an error aborts the run with its code and rule location.

Conditions can use bound variables and integer literals, including conjunctions
with `(and ...)`. They do not create new bindings. Unknown predicates, incorrect
arity, and unbound arguments fail compilation.

A symbolic representative alone is not an overflow proof. Range/type/known-bit
analysis must establish the claim under the host semantic model. Do not replace a
missing analysis with a callback that always approves the rule.

## 10.6 Extraction costs

Default local cost is one for a literal and `1 + arity` for an operator, plus all
child costs. `CostModel::operators` overrides local operator cost;
`CostModel::literal` overrides literal cost. Costs are non-negative and may be zero.

```cpp
limestone::tunah::CostModel costs;
costs.operators = {{"imul", 20}, {"ishl", 1}};
auto result = session.saturate("(imul x 4)", {}, costs);
```

This fragment assumes the multiplication-to-shift equivalence was loaded and
is sound for the host IL. The weights illustrate configuration; they are not
universal measured target costs.

The maximum `size_t` value represents infinity. Additive overflow saturates to
infinity rather than wrapping into a cheap alternative. Extraction reports a
resource error if no finite-cost representative remains. Equal-cost alternatives
follow stable engine node order, and rules are applied in name order.

Target-aware weights belong to a host cost adapter. Tunah's scalar objective does
not itself account for final register allocation, code layout, or resources.

## 10.7 Resource bounds and partial saturation

`Limits` defaults to 20 iterations, 10,000 nodes, 10,000 classes, no time budget,
and trace collection enabled. Node/class budgets must be positive. An extraction-
only run can use zero iterations.

Node/class reservations conservatively cover instantiation of both sides of a
rewrite and can stop before the numeric budget is fully consumed. Time and
cancellation checks are cooperative between iterations and matches. Engine
matching/rebuilding/extraction and host callbacks are not preemptible operations.

A successful budget stop can still extract a valid equivalent expression.
`limit_reached` explains that saturation did not complete. This differs from a
malformed rule, failed legality proof, or internal error, which returns failure.
Keep host analyses bounded so they do not defeat the intended responsiveness.

## 10.8 Trace interpretation and the tuner corpus

Trace entries identify admitted matches with rule source locations and variable
bindings in stable name order. Redundant matches can appear. `rewrites` counts
actual e-class unions, not the number of trace lines. The trace is useful
provenance; it is not a complete formal derivation for every e-node.

The repository's instruction-level corpus has 13 tuner files and 239 rules. Load
`vocabulary.tuner` before selected files. The vocabulary declares syntax, while
the host adapter establishes widths, purity, memory ordering, vector shapes,
and reconstruction legality.

```sh
printf '%s\n' '(iadd (iadd 12 30) (imul x 0))' | build/limestone-cli \
  --optimize-term \
  --rules tunah/tuners/instr-level-tune/vocabulary.tuner \
  --rules tunah/tuners/instr-level-tune/alg-sim.tuner \
  --rules tunah/tuners/instr-level-tune/const-folding.tuner \
  --iterations 20 --node-limit 10000 --class-limit 10000 --trace
```

Guarded widening/vector rules need registered host proofs and cannot be loaded
by a standalone CLI that supplies no such analyses. Consult
[the component guide](../tunah/README.md) and
[benchmark notes](../tunah/BENCHMARKS.md) for corpus semantics and measurements.

## 10.9 Typed Unisel graph reconstruction

`optimize_graph` is a concrete bidirectional adapter. `GraphAdapterOptions`
registers each eligible source opcode with a term operator, concrete type, arity,
and optional explicitly justified commutativity. Only registered pure operations
are optimized.

The lifecycle is ingress validation, boundary construction, saturation,
extraction validation, transactional reconstruction, deterministic remapping,
and pure dead-node pruning. Shared inputs remain references; effects and semantic
dependency endpoints stay pinned. CFGs, outputs, external inputs, live-outs, and
source block/provenance are preserved.

`GraphOptimization` returns the new program, old-to-new value mapping, rewrite
count, and limit status. A pipeline optimizer callback can return its program.
The host must prove the rules for the concrete registered types; the adapter
does not infer width or overflow semantics from the opcode spelling.

## 10.10 Lifted binary semantic optimization

`binary_transform` snapshots a session into a Bin2Bin `SemanticTransform`.
`BinaryAdapterOptions::legality` is mandatory at ingress and after extraction.
It proves the candidate's types, widths, architectural effects, and operand
legality in the host semantic vocabulary. The registered rules establish
equivalence; legality establishes admission of the extracted form.

The host supplies a nonempty semantic/analysis context identity. Rule/operator
definitions, predicates, costs, and budgets enter the deterministic adapter
identity automatically. Callback addresses do not identify semantics.

Instruction boundaries, control classification, and direct branch targets stay
intact. Bin2Bin owns matching, relaxation, and encoding. CFG-changing optimization
needs a region adapter rather than a per-instruction term rewrite.

Time-limited or cancellable transforms bypass translated-byte caching and resident
translation reuse, so a cache hit cannot erase cancellation-sensitive behavior.
Runtime observations still retain heat and publish independent immutable views.

## 10.11 C/Python snapshots and callback ownership

The owning C interface is [limestone/optimization.h](../limestone/optimization.h),
exported by `Limestone::optimization`. An optimizer owns rules, costs, declarations,
and limits. Results own expressions/statistics/traces and can outlive the optimizer.
Attaching an optimizer to a target copies the full configuration; later source
mutation or destruction does not alter that attachment.

Predicate and cancellation userdata is adopted only after successful registration
when a release callback is supplied. Its nonthrowing release runs once after the
last session, target, transform, or active-run snapshot. Without a release callback,
userdata remains host-owned. Active snapshots allow callbacks to mutate their
source optimizer without invalidating the ongoing run.

Python convenience objects expose the same owned session/result model. Native
proof and cancellation callbacks use the raw C ABI from an extension. Keep the
extension code loaded while retained callback snapshots may execute it.

## 10.12 Building sound optimization integration

Start with a small pure typed vocabulary and identity round trips. Add one
equivalence at a time, including semantic boundary valuations and negative proof
cases. Verify extracted terms before reconstruction and verify the reconstructed
IL before publishing it.

Measure extraction quality and compilation cost separately. Record limits,
costs, rule identities, and analysis versions. This produces an optimizer that
is explainable, reproducible, and compatible with the downstream target contracts.

[Previous: Allocation](09-regtl-allocation-and-spilling.md) · [Contents](README.md) · [Next: MachineIR](11-machineir-and-cpp-d-exchange.md)
