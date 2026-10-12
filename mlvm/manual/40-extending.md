# 40. Extending MLVM and planning future work

[Contents](README.md) · [Previous: Testing](39-testing.md)

MLVM extensions should identify an explicit representation, semantic contract,
and execution boundary. A new feature is complete when implementation, tests,
documentation, and unsupported-case diagnostics agree.

## Choose the extension point

| Requirement | Starting point |
| --- | --- |
| New operation family | Dialect registration and custom verification |
| New lowering route | `ConversionGraph` with explicit legal dialects |
| Module optimization | `Pass` and analysis contracts |
| Reusable declarative optimization | Authored Optilisp kernel plus a supported backend |
| Custom kernel semantics | `ModuleOptimizer` or graph materialization/proof callbacks |
| Foreign graph integration | NodeIR import/export callbacks |
| New object behavior | MLVM-owned binary adapter and owning public types |
| New machine target | Encodings, relocations, ABI decisions, and vertical-slice tests |

Keep `libstone/` read-only and implement adapters in MLVM-owned code. Do not
leak dependency-private types into public interfaces. Preserve stable ID rules,
structured diagnostics, deterministic ordering, and transactional publication.

## A practical development sequence

1. Specify accepted inputs, invariants, and explicit failure cases.
2. Implement validation before transformations depend on the new semantics.
3. Add a minimal positive example and meaningful negative/rollback tests.
4. Connect the API, then the relevant textual or CLI surface.
5. Update the nearest reference document and the affected manual chapter.

For kernel work, test both API and textual application. Include a nonmatch and
an unknown-evidence case so a broad syntactic pattern cannot silently become an
unjustified rewrite. For machine work, test overflow and independent object
inspection as well as a successful executable.

## Planned architectural growth

The roadmap includes generic CFG successors and dominance, richer type/attribute
schemas, nested pass scheduling, broader C semantics, staged lowering, general
machine selection/scheduling/register allocation, more object formats/targets,
and hosted runtime support. These are goals, not existing compatibility promises.

Use the tested baseline as the foundation for a small complete path through any
new feature. A recognized command name, target identifier, or parsed declaration
does not by itself complete that path.

Reference: [roadmap](../docs/ROADMAP.md), [API contract](../docs/API.md),
[architecture](../docs/ARCHITECTURE.md).
