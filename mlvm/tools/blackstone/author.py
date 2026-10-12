"""Reproducible authoring table for 50 supplemental declarative kernels.

Run with python -m tools.blackstone.author; --check checks generated source drift.
The checked-in .olisp files are the consumer-facing canonical definitions.
"""

import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent

# stage, domain, operation, input, output, proof obligation, semantic boundary
ROWS = [
    ("hir", "bitwise", "and-all-ones", "(and x mask)", "x", "mask-is-width-specific-all-ones", "The mask is 2^width - 1, not an untyped negative literal."),
    ("hir", "bitwise", "and-self", "(and x x)", "x", "operands-are-the-same-defined-value", "The operand is a defined fixed-width bitvector, excluding poison and undef."),
    ("hir", "bitwise", "or-self", "(or x x)", "x", "operands-are-the-same-defined-value", "Both uses denote one already evaluated value."),
    ("hir", "bitwise", "or-zero", "(or x zero)", "x", "zero-has-operand-width", "Zero and the result have exactly the operand width."),
    ("hir", "bitwise", "or-all-ones", "(or x mask)", "mask", "mask-is-width-specific-all-ones", "Removing this operation preserves evaluation of x and excludes poison."),
    ("hir", "bitwise", "and-zero", "(and x zero)", "zero", "zero-has-operand-width", "Removing this operation preserves evaluation of x and excludes poison."),
    ("hir", "bitwise", "double-complement", "(not (not x))", "x", "complements-have-identical-width", "Bitwise complement is total over fixed-width bitvectors."),
    ("hir", "bitwise", "xor-all-ones", "(xor x mask)", "(not x)", "mask-is-width-specific-all-ones", "The replacement uses a complement at the same width."),
    ("hir", "bitwise", "and-complement", "(and x (not x))", "zero", "complements-have-identical-width", "The result is a zero bitvector of the original width."),
    ("hir", "bitwise", "or-complement", "(or x (not x))", "mask", "complements-have-identical-width", "The result is an all-ones bitvector of the original width."),
    ("hir", "bitwise", "absorb-and-or", "(and x (or x y))", "x", "operands-have-identical-width", "This is bitvector absorption; operand evaluation is not discarded."),
    ("hir", "bitwise", "absorb-or-and", "(or x (and x y))", "x", "operands-have-identical-width", "This is bitvector absorption, not overloaded operator reasoning."),
    ("hir", "bitwise", "de-morgan-and", "(not (and x y))", "(or (not x) (not y))", "operands-have-identical-width", "Both complements and the result use the original bitvector width."),
    ("hir", "bitwise", "de-morgan-or", "(not (or x y))", "(and (not x) (not y))", "operands-have-identical-width", "The rewrite has a one-way canonical orientation; it need not be profitable."),
    ("hir", "shifts", "left-zero", "(shl x zero)", "x", "shift-count-is-zero", "No masking or out-of-range shift convention is inferred."),
    ("hir", "shifts", "logical-right-zero", "(lshr x zero)", "x", "shift-count-is-zero", "The operator is logical right shift, not an overloaded shift."),
    ("hir", "shifts", "arithmetic-right-zero", "(ashr x zero)", "x", "shift-count-is-zero", "The operator is arithmetic right shift at a fixed width."),
    ("hir", "unsigned", "divide-power-of-two", "(udiv x power)", "(lshr x exponent)", "divisor-is-representable-power-of-two", "Unsigned division uses 2^exponent with 0 <= exponent < width."),
    ("hir", "unsigned", "remainder-power-of-two", "(urem x power)", "(and x low-mask)", "divisor-is-representable-power-of-two", "The low mask is 2^exponent - 1; division by zero is excluded."),
    ("hir", "unsigned", "remainder-one", "(urem x one)", "zero", "divisor-is-one-at-operand-width", "Only unsigned remainder is covered; x remains evaluated and defined."),
    ("mir", "memory", "load-after-memset-zero", "(load-after-memset address bytes)", "(zero-value bytes)", "complete-byte-coverage-and-no-clobber-proven", "Exact byte coverage, endianness, object lifetime, nonvolatile non-atomic loads, and no intervening clobber are required."),
    ("mir", "memory", "readonly-global-load", "(load global)", "(initializer-value global)", "immutable-initializer-and-no-interposition-proven", "Reject externally mutable, interposable, atomic, volatile, and lifetime-ambiguous globals."),
    ("mir", "memory", "non-escaping-allocation-elision", "(unused-allocation allocation)", "(removed-allocation allocation)", "allocation-has-no-observable-failure-or-identity", "Non-escape alone is insufficient: failure, finalizers, allocation tracing, and identity must be unobservable."),
    ("mir", "memory", "adjacent-memcpy-coalescing", "(adjacent-copies first second)", "(merged-copy first second)", "contiguous-disjoint-ranges-and-order-equivalence-proven", "Check source/destination contiguity, overlap, alignment, bounds, and absence of intervening effects."),
    ("mir", "memory", "zero-length-memcpy-elision", "(memcpy dst src zero)", "(removed-copy)", "zero-length-copy-has-no-pointer-validation-effects", "The IR must permit zero-length copy removal even for invalid pointers; otherwise retain it."),
    ("mir", "memory", "self-memmove-elision", "(memmove address address length)", "(removed-move)", "identical-ranges-and-no-observable-access-proven", "Bounds checks, pointer validation, atomic accesses, and volatile accesses must remain preserved."),
    ("mir", "control-flow", "equal-successor-branch", "(branch condition target target)", "(jump target)", "parallel-edge-phi-values-are-identical", "Retain condition-producing effects and merge edge-indexed phi values only when identical."),
    ("mir", "control-flow", "single-case-switch", "(switch value case default)", "(compare-and-branch value case default)", "switch-has-exactly-one-case", "Preserve default reachability, edge-indexed phi values, and the integer comparison width."),
    ("mir", "control-flow", "empty-switch", "(switch value default)", "(jump default)", "switch-has-no-cases", "Retain computation of the selector and all of its effects."),
    ("mir", "control-flow", "same-destination-switch", "(switch value cases target)", "(jump target)", "all-case-and-default-edge-values-are-identical", "Destinations alone are insufficient: edge-indexed phi operands must also agree."),
    ("mir", "control-flow", "select-same-value", "(select condition value value)", "value", "select-condition-and-value-are-defined", "A poison condition can change semantics; require defined conditions and preserve prior evaluation."),
    ("mir", "control-flow", "select-constant-arm", "(select constant left right)", "(chosen-arm constant left right)", "condition-is-proven-defined-boolean-constant", "Only the selected SSA value replaces the select; do not eagerly evaluate lazy arms."),
    ("mir", "loops", "zero-trip-loop-elision", "(loop body bound)", "(bypass-loop body)", "zero-trip-count-and-entry-effects-preserved", "Retain bound evaluation, entry checks, exit phi values, and any effects before the first iteration."),
    ("mir", "loops", "single-trip-loop-elision", "(loop body bound)", "(single-iteration body)", "exactly-one-iteration-including-exits-proven", "Prove absence of early exceptions changing iteration count; update loop-carried and exit SSA values."),
    ("mir", "loops", "loop-deletion", "(effect-free-finite-loop body)", "(bypass-loop body)", "termination-and-unobservability-proven", "Deleting a nonterminating loop changes behavior: prove finite termination and absence of traps, effects, and live results."),
    ("mir", "loops", "loop-predication", "(loop-with-check body check)", "(guarded-loop body check)", "entry-guard-equivalent-to-all-iteration-checks", "Hoisted checks must not trap on a zero-trip path; retain deoptimization state and overflow semantics."),
    ("mir", "calls", "pure-call-cse", "(repeat-pure-call callee args)", "(previous-call-result callee args)", "deterministic-total-call-and-dominance-proven", "Purity alone does not imply determinism or absence of traps; require all three plus equivalent arguments."),
    ("mir", "calls", "unused-return-component", "(call-with-aggregate-return callee args)", "(call-with-pruned-return callee args)", "all-callers-and-callee-body-owned-and-updated", "Internal linkage, ABI isolation, exceptional exits, and all return paths must be accounted for."),
    ("mir", "vectors", "splat-extract", "(extract (splat value lanes) index)", "value", "index-proven-in-bounds-and-vector-defined", "Out-of-range indices, scalable lane uncertainty, and poison vectors are excluded."),
    ("mir", "vectors", "identity-shuffle", "(shuffle vector identity-mask)", "vector", "mask-is-complete-identity-with-no-undef-lanes", "Preserve vector length, element types, and defined lane semantics; scalable vectors require explicit proof."),
    ("lir", "target", "byte-swap-recognition", "(byte-swap-sequence value)", "(target-byte-swap value)", "target-byte-swap-equivalence-proven", "Require exact width, byte order, flags, clobbers, and target opcode availability."),
    ("lir", "target", "population-count-recognition", "(population-count-sequence value)", "(target-popcount value)", "target-popcount-equivalence-proven", "Require exact width, result extension, flags, and implicit clobber equivalence."),
    ("lir", "target", "count-leading-zeros-recognition", "(clz-sequence value)", "(target-clz value)", "target-clz-zero-input-semantics-match", "Some targets leave zero-input results undefined; preserve the source's specified zero result."),
    ("lir", "target", "count-trailing-zeros-recognition", "(ctz-sequence value)", "(target-ctz value)", "target-ctz-zero-input-semantics-match", "Require matching zero-input behavior, width, result extension, flags, and clobbers."),
    ("lir", "target", "bit-reverse-recognition", "(bit-reverse-sequence value)", "(target-bit-reverse value)", "target-bit-reverse-width-and-flags-match", "Bit order reversal is not byte order reversal; check exact width and register constraints."),
    ("lir", "target", "funnel-shift-recognition", "(funnel-shift-sequence left right count)", "(target-funnel-shift left right count)", "target-funnel-shift-count-semantics-match", "Check zero count, modulo behavior, operand ordering, width, flags, and poison semantics."),
    ("lir", "memory", "narrow-load-folding", "(truncate (wide-load address))", "(narrow-load address)", "narrow-load-access-and-trap-equivalence-proven", "Different access widths may change faults, MMIO, atomicity, and endian-selected bytes; prove equivalence."),
    ("lir", "registers", "redundant-reload-elision", "(reload spill-slot register)", "(existing-register-value register)", "spill-value-and-register-lane-validity-proven", "Check spill slot clobbers, partial registers, calling convention clobbers, and live ranges."),
    ("lir", "layout", "fallthrough-jump-elision", "(jump next-block)", "(fallthrough next-block)", "physical-adjacency-and-fallthrough-legality-proven", "Check section boundaries, alignment padding, delay slots, and unwind ranges before removing the jump."),
    ("lir", "registers", "redundant-register-move", "(move register register)", "(removed-move)", "move-has-no-lane-extension-flags-or-clobber-effects", "Same register names do not imply a no-op: reject moves that zero upper lanes, alter flags, or carry effects."),
]


def profile(stage, domain):
    if stage == "hir":
        return "algebraic-simplification", ["typed-expressions", "bitwise-semantics" if domain == "bitwise" else "shift-semantics" if domain == "shifts" else "division-semantics"], [], ["constant-value-analysis", "range-analysis"]
    if stage == "lir":
        return "instruction-combination", ["machine-operations", "target-description"], ["liveness-analysis"], ["liveness-analysis", "live-range-analysis"]
    caps = ["ssa-form"]
    analyses = ["dominator-analysis"]
    if domain == "memory":
        caps += ["memory-effects"]
        analyses += ["alias-analysis", "memory-dependence-analysis", "side-effect-analysis", "exception-analysis"]
    elif domain in {"control-flow", "loops"}:
        caps += ["explicit-control-flow"]
        if domain == "loops":
            caps += ["loop-information"]
            analyses += ["loop-nest-analysis", "induction-variable-analysis", "side-effect-analysis", "exception-analysis"]
    elif domain == "calls":
        caps += ["call-graph", "interprocedural-summaries"]
        analyses += ["call-graph-analysis", "side-effect-analysis", "exception-analysis"]
    else:
        caps += ["vector-operations"]
        analyses += ["range-analysis"]
    return {"memory": "memory-optimization", "control-flow": "control-flow-simplification", "loops": "loop-optimization", "calls": "interprocedural-optimization", "vectors": "vectorization"}[domain], caps, analyses, sorted(set(analyses + ["liveness-analysis", "constant-value-analysis"]))


def render(row):
    stage, domain, operation, before, after, guard, boundary = row
    category, caps, analyses, invalidates = profile(stage, domain)
    kid = f"blackstone.{stage}.{domain}.{operation}"
    title = operation.replace("-", " ").capitalize()
    conditions = [guard, "operand-and-result-types-compatible", "operand-values-defined",
                  "source-effects-traps-and-exceptions-preserved", "result-satisfies-stage-invariants"]
    if stage == "lir":
        conditions += ["target-flags-clobbers-and-register-constraints-preserved"]
    forbids = ["unmodeled-observable-effects", "invalidated-input-analysis"]
    if domain == "memory":
        forbids += ["volatile-or-atomic-access", "unknown-memory-dependence", "external-memory-observation-changes"]
    purpose = f"Replace {before} with {after} when the declared proof obligations hold."
    docs = {
        "purpose": purpose,
        "motivation": f"Expose the {title.lower()} opportunity for planners with explicit legality facts.",
        "stage": f"Node{stage.upper()}; all required capabilities must be supplied explicitly.",
        "applicability": f"Match {before}; require a proven true {guard} fact.",
        "preconditions": "Required true facts: " + ", ".join(conditions) + ".",
        "required-information": "Capabilities: " + ", ".join(caps) + "; analyses: " + (", ".join(analyses) or "none") + ". Results must be current.",
        "transformation": purpose + " Preserve source provenance and update uses and edges consistently.",
        "semantic-assumptions": boundary,
        "correctness": "Preserve required observable behavior under the declared execution model; unknown facts never justify the rewrite.",
        "known-limitations": "Declarative contract only; NodeIR matching and execution require an external adapter. " + boundary,
        "expected-effects": "Simplifies the matched representation; runtime benefit and code-size change depend on the surrounding program and target.",
        "testing-strategy": f"tests/test_catalog.py checks positive, rejected, unknown, invalidated, and stage-mismatch applicability for {kid}. Integer-model tests cover HIR identities; other transformations have no executable NodeIR evidence.",
        "references": "Blackstone AGENTS.md sections 4, 5, 11, 12, 17, and 18; tools/blackstone/README.md contract semantics.",
        "recommended-tools": "No external library recommended; execution integration remains optional.",
    }
    quote = lambda text: json.dumps(text, ensure_ascii=True)
    lines = ["; MLVM supplement: schema profile 1; declarative, not executable NodeIR.",
             "(define-kernel", f"  :id {quote(kid)}", f"  :name {quote(title)}", f"  :stage {stage}",
             f"  (category {category})", "  (paradigms graph-rewriting rule-based)",
             f"  (objective {quote(purpose)})", "  (requires"]
    lines += [f"    (capability {value})" for value in caps]
    lines += [f"    (analysis {value})" for value in analyses]
    lines[-1] += ")"
    lines += ["  (preconditions " + " ".join(conditions) + ")", "  (forbids " + " ".join(forbids) + ")", "  (analysis-contract",
              "    (requires " + " ".join(analyses) + ")", "    (invalidates " + " ".join(invalidates) + ")", "    (preserves))",
              f"  (transform (rewrite {before} {after}))",
              "  (correctness preserve-observable-behavior preserve-stage-invariants preserve-source-provenance)",
              f"  (expected-effects {quote(docs['expected-effects'])})", f"  (limitations {quote(docs['known-limitations'])})", "  (documentation"]
    lines += [f"    ({topic} {quote(value)})" for topic, value in docs.items()]
    lines[-1] += "))"
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    failed = False
    for row in ROWS:
        stage, domain, operation = row[:3]
        path = HERE / "kernels" / stage / domain / (operation + ".olisp")
        content = render(row)
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != content:
                print(f"source drift: {path}")
                failed = True
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
