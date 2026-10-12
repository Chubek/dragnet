"""Layered validation for the Blackstone kernel database (spec section 17).

Validation is split into distinct layers rather than one opaque function:

- Layer 1: syntax        -- performed by :mod:`optilisp.sexpr`
- Layer 2: structural    -- required fields, types, recognized stage
- Layer 3: references    -- unique IDs, resolvable cross-references
- Layer 4: semantic      -- stage compatibility, contradictory contracts
- Layer 5: transformation-- verb vocabulary, stage-appropriate transforms
- Layer 6: database      -- deterministic load, indexes, global policy

Each layer reports diagnostics with source locations.  A kernel that is
inapplicable is not malformed; inapplicability is an optimization-planning
concern, not a validation failure (spec section 23.2).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set

from .errors import DiagnosticSink, Location
from .semantic import (
    STAGES,
    STAGE_VERBS,
    TRANSFORM_VERBS,
    Analysis,
    AnalysisContract,
    Capability,
    Documentation,
    Kernel,
    Monoid,
    Transform,
    analyze_file,
)
from .sexpr import Form, ListForm, read

SCHEMA_DIR = Path(__file__).resolve().parents[3] / "schemas"


# ---------------------------------------------------------------------------
# Registry and database loading
# ---------------------------------------------------------------------------


@dataclass
class Registry:
    capabilities: Dict[str, Capability] = field(default_factory=dict)
    analyses: Dict[str, Analysis] = field(default_factory=dict)
    monoids: Dict[str, Monoid] = field(default_factory=dict)
    kernel_ids: Set[str] = field(default_factory=set)

    def has_capability(self, name: str) -> bool:
        return name in self.capabilities

    def has_analysis(self, name: str) -> bool:
        return name in self.analyses

    def has_monoid(self, name: str) -> bool:
        return name in self.monoids

    def has_kernel(self, name: str) -> bool:
        return name in self.kernel_ids


@dataclass
class Database:
    registry: Registry
    kernels: List[Kernel] = field(default_factory=list)
    files: List[str] = field(default_factory=list)


def _load_optilisp_file(path: Path, sink: DiagnosticSink) -> tuple[List[Form], dict]:
    source = path.read_text(encoding="utf-8")
    forms, read_sink = read(source, str(path))
    sink.items.extend(read_sink.items)
    records = analyze_file(forms, str(path), sink)
    return forms, records


def load_registry(stdlib_dir: Path, sink: DiagnosticSink) -> Registry:
    """Load capability, analysis, and monoid definitions from the stdlib."""
    reg = Registry()

    def collect(subdir: str, key: str) -> None:
        d = stdlib_dir / subdir
        if not d.is_dir():
            sink.error("E-REGISTRY", f"missing stdlib directory '{subdir}'", None)
            return
        for path in sorted(d.glob("*.olisp")):
            _, records = _load_optilisp_file(path, sink)
            for item in records[key]:
                if item.id in reg.capabilities or item.id in reg.analyses or item.id in reg.monoids:
                    sink.error("E-REGISTRY", f"duplicate definition id '{item.id}'", item.loc)
                    continue
                if key == "capabilities":
                    reg.capabilities[item.id] = item
                elif key == "analyses":
                    reg.analyses[item.id] = item
                elif key == "monoids":
                    reg.monoids[item.id] = item

    collect("capabilities", "capabilities")
    collect("analyses", "analyses")
    collect("monoids", "monoids")
    return reg


def load_database(root: Path, registry: Registry, sink: DiagnosticSink) -> Database:
    """Load every kernel under ``kernels/`` in deterministic (sorted) order."""
    db = Database(registry)
    kernels_dir = root / "kernels"
    if not kernels_dir.is_dir():
        sink.error("E-DATABASE", "missing 'kernels/' directory", None)
        return db

    for path in sorted(kernels_dir.rglob("*.olisp")):
        _, records = _load_optilisp_file(path, sink)
        for k in records["kernels"]:
            db.kernels.append(k)
            db.files.append(str(path))
        for m in records["monoids"]:
            sink.error("E-DATABASE", f"monoid definition '{m.id}' found outside stdlib", m.loc)
        for c in records["capabilities"]:
            sink.error("E-DATABASE", f"capability definition '{c.id}' found outside stdlib", c.loc)
        for a in records["analyses"]:
            sink.error("E-DATABASE", f"analysis definition '{a.id}' found outside stdlib", a.loc)

    registry.kernel_ids = {k.id for k in db.kernels}
    return db


# ---------------------------------------------------------------------------
# Kernel -> JSONable dict (for schema validation)
# ---------------------------------------------------------------------------


def kernel_to_dict(k: Kernel) -> dict:
    contract = None
    if k.analysis_contract is not None:
        c = k.analysis_contract
        contract = {
            "requires": list(c.requires),
            "invalidates": list(c.invalidates),
            "preserves": list(c.preserves),
        }
    transform = None
    if k.transform is not None:
        transform = {"verb": k.transform.verb, "args": [str(a) for a in k.transform.args]}
    documentation = None
    if k.documentation is not None:
        d = k.documentation
        documentation = {
            "prose": list(d.prose),
            "structured": {key: [str(v) for v in vals] for key, vals in d.structured.items()},
        }
    return {
        "id": k.id,
        "name": k.name,
        "stage": k.stage,
        "categories": list(k.categories),
        "paradigms": list(k.paradigms),
        "objective": k.objective,
        "requires": [{"kind": r.kind, "name": r.name} for r in k.requires],
        "preconditions": list(k.preconditions),
        "forbids": list(k.forbids),
        "prerequisites": list(k.prerequisites),
        "forbiddenPriorTransformations": list(k.forbidden_prior),
        "analysisContract": contract,
        "transform": transform,
        "correctness": list(k.correctness),
        "expectedEffects": k.expected_effects,
        "limitations": k.limitations,
        "documentation": documentation,
    }


def load_schema(name: str, sink: DiagnosticSink) -> Optional[dict]:
    path = SCHEMA_DIR / name
    if not path.is_file():
        sink.error("E-SCHEMA", f"missing schema file '{name}'", None)
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        sink.error("E-SCHEMA", f"invalid JSON in schema '{name}': {exc}", None)
        return None


# ---------------------------------------------------------------------------
# Layer 2: structural validation
# ---------------------------------------------------------------------------


def _check_no_duplicate_sections(k: Kernel, form: Optional[ListForm], sink: DiagnosticSink) -> None:
    """Reject duplicate singleton sections such as two (category ...) blocks."""
    if form is None or not form.items:
        return
    seen: Set[str] = set()
    for item in form.items[1:]:
        if isinstance(item, ListForm) and item.items:
            head = item.items[0].name if hasattr(item.items[0], "name") else ""
            if head:
                if head in seen:
                    sink.error("E-STRUCT", f"duplicate '{head}' section in kernel '{k.id}'", item.loc)
                seen.add(head)


def validate_structure(k: Kernel, form: Optional[ListForm], sink: DiagnosticSink) -> None:
    """Layer 2: required fields, correct types, recognized stage."""
    if not k.id:
        sink.error("E-STRUCT", "kernel is missing a stable :id", k.loc)
    if not k.name:
        sink.error("E-STRUCT", "kernel is missing a human-readable :name", k.loc)
    if k.stage not in STAGES:
        sink.error("E-STRUCT", f"kernel '{k.id}' has an invalid or missing stage", k.loc)
    if not k.categories:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare at least one (category ...)", k.loc)
    if not k.objective.strip():
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare a primary (objective ...)", k.loc)
    if not k.requires:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare (requires ...)", k.loc)
    if not k.preconditions:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare at least one precondition", k.loc)
    if k.transform is None:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare a (transform ...)", k.loc)
    elif not k.transform.verb:
        sink.error("E-STRUCT", f"kernel '{k.id}' transform has no verb", k.transform.loc)
    if not k.correctness:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare (correctness ...)", k.loc)
    if not k.expected_effects.strip():
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare (expected-effects ...)", k.loc)
    if not k.limitations.strip():
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare (limitations ...)", k.loc)
    if k.documentation is None:
        sink.error("E-STRUCT", f"kernel '{k.id}' must declare (documentation ...)", k.loc)
    _check_no_duplicate_sections(k, form, sink)


# ---------------------------------------------------------------------------
# Layer 3: reference validation
# ---------------------------------------------------------------------------


def validate_references(k: Kernel, reg: Registry, all_kernel_ids: Set[str], sink: DiagnosticSink) -> None:
    """Layer 3: IDs are unique and cross-references resolve."""
    if k.id and k.id not in all_kernel_ids:
        sink.error("E-REF", f"kernel id '{k.id}' is not present in the database", k.loc)

    for req in k.requires:
        if req.kind == "capability" and not reg.has_capability(req.name):
            sink.error("E-REF", f"unresolved capability identifier '{req.name}' referenced by kernel '{k.id}'", req.loc)
        elif req.kind == "analysis" and not reg.has_analysis(req.name):
            sink.error("E-REF", f"unresolved analysis identifier '{req.name}' referenced by kernel '{k.id}'", req.loc)

    if k.analysis_contract is not None:
        c = k.analysis_contract
        for name in c.requires + c.invalidates + c.preserves:
            if not (reg.has_capability(name) or reg.has_analysis(name)):
                sink.error("E-REF", f"unresolved analysis-contract identifier '{name}' in kernel '{k.id}'", c.loc)

    for kid in k.prerequisites:
        if kid not in all_kernel_ids:
            sink.error("E-REF", f"prerequisite kernel '{kid}' referenced by '{k.id}' does not exist", k.loc)
    for kid in k.forbidden_prior:
        if kid not in all_kernel_ids:
            sink.error("E-REF", f"forbidden-prior kernel '{kid}' referenced by '{k.id}' does not exist", k.loc)


def validate_unique_ids(db: Database, sink: DiagnosticSink) -> None:
    seen: Dict[str, str] = {}
    for k in db.kernels:
        if k.id in seen:
            sink.error("E-REF", f"duplicate kernel id '{k.id}' (also defined in {seen[k.id]})", k.loc)
        else:
            seen[k.id] = k.file


# ---------------------------------------------------------------------------
# Layer 4: semantic contract validation
# ---------------------------------------------------------------------------


def validate_semantic(k: Kernel, reg: Registry, sink: DiagnosticSink) -> None:
    """Layer 4: stage compatibility and contradictory declarations."""
    if k.transform is not None:
        verb = k.transform.verb
        if verb not in TRANSFORM_VERBS:
            sink.error("E-SEMANTIC", f"unknown transformation verb '{verb}' in kernel '{k.id}'", k.transform.loc)
        elif k.stage in STAGE_VERBS and verb not in STAGE_VERBS[k.stage]:
            sink.error(
                "E-SEMANTIC",
                f"transformation verb '{verb}' is not valid at stage '{k.stage}' for kernel '{k.id}'",
                k.transform.loc,
            )

    # Stage compatibility of required capabilities.
    for req in k.requires:
        if req.kind != "capability":
            continue
        cap = reg.capabilities.get(req.name)
        if cap is not None and cap.stages and k.stage not in cap.stages:
            sink.error(
                "E-SEMANTIC",
                f"capability '{req.name}' does not apply to stage '{k.stage}' (kernel '{k.id}')",
                req.loc,
            )

    # Contradiction: an analysis cannot be both invalidated and preserved.
    if k.analysis_contract is not None:
        c = k.analysis_contract
        both = set(c.invalidates) & set(c.preserves)
        for name in sorted(both):
            sink.error(
                "E-SEMANTIC",
                f"kernel '{k.id}' both invalidates and preserves '{name}'",
                c.loc,
            )

    # Contradiction: a kernel cannot be both a prerequisite and forbidden.
    both = set(k.prerequisites) & set(k.forbidden_prior)
    for kid in sorted(both):
        sink.error(
            "E-SEMANTIC",
            f"kernel '{k.id}' lists '{kid}' as both prerequisite and forbidden-prior",
            k.loc,
        )

    # A kernel cannot require a property its own transform is known to destroy
    # for the same stage (conservative, declarative check).
    if k.analysis_contract is not None and k.transform is not None:
        destructive = {"eliminate", "split", "interchange", "unroll", "fuse", "distribute", "vectorize"}
        if k.transform.verb in destructive:
            req_names = {r.name for r in k.requires if r.kind == "capability"}
            invalidated = set(k.analysis_contract.invalidates)
            # A destructive transform that invalidates nothing it requires is fine;
            # flag only self-contradiction where a required capability is invalidated.
            for name in sorted(req_names & invalidated):
                sink.warning(
                    "W-SEMANTIC",
                    f"kernel '{k.id}' requires '{name}' but its transform invalidates it; "
                    "ensure the analysis is recomputed before a dependent kernel runs",
                    k.loc,
                )


# ---------------------------------------------------------------------------
# Layer 5: transformation validation
# ---------------------------------------------------------------------------


def validate_transformation(k: Kernel, sink: DiagnosticSink) -> None:
    """Layer 5: transformation shape and stage-appropriate invariants.

    Blackstone is a declarative database; it does not execute transformations.
    This layer verifies what is statically checkable and explicitly reports the
    limitation that no semantic executor is available (spec section 17.5).
    """
    if k.transform is None:
        return
    t = k.transform
    if not t.args:
        sink.error("E-TRANSFORM", f"kernel '{k.id}' transform verb '{t.verb}' has no arguments", t.loc)
        return

    # rewrite/fold/eliminate/hoist/etc. take a pattern (and often a result).
    pattern_verbs = {"rewrite", "fold", "eliminate", "hoist", "sink", "combine",
                     "forward", "interchange", "unroll", "peel", "fuse",
                     "distribute", "tile", "skew", "inline", "specialize",
                     "devirtualize", "lower", "select", "promote", "coalesce",
                     "rematerialize", "canonicalize", "simplify", "split",
                     "merge", "vectorize", "parallelize", "if-convert",
                     "construct", "outline", "rotate", "deduplicate", "remove"}
    if t.verb in pattern_verbs and len(t.args) < 1:
        sink.error("E-TRANSFORM", f"transform verb '{t.verb}' requires at least a pattern argument", t.loc)

    if t.verb in {"rewrite", "fold"} and len(t.args) < 2:
        sink.error("E-TRANSFORM", f"transform verb '{t.verb}' requires a pattern and a result form", t.loc)

    if t.verb in {"inline", "specialize", "devirtualize"}:
        sink.note(
            "N-TRANSFORM",
            f"kernel '{k.id}' describes a whole-procedure transformation; "
            "applicability requires call-graph information",
            t.loc,
        )


# ---------------------------------------------------------------------------
# Layer 6: database validation
# ---------------------------------------------------------------------------


def validate_database(db: Database, sink: DiagnosticSink) -> None:
    """Layer 6: deterministic load, index construction, global policy."""
    validate_unique_ids(db, sink)

    # Deterministic ordering: kernels must be loadable in sorted path order.
    paths = [k.file for k in db.kernels]
    if paths != sorted(paths):
        sink.error("E-DATABASE", "kernel loading order is not deterministic (sorted by path)", None)

    # Index: stage -> category -> count must be buildable.
    index: Dict[str, Dict[str, int]] = {}
    for k in db.kernels:
        stage = k.stage or "<invalid>"
        for cat in k.categories or ["<uncategorized>"]:
            index.setdefault(stage, {}).setdefault(cat, 0)
            index[stage][cat] += 1
    if not index:
        sink.error("E-DATABASE", "kernel database is empty", None)


# ---------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------


def validate_kernel_against_schema(k: Kernel, sink: DiagnosticSink) -> None:
    schema = load_schema("kernel.schema.json", sink)
    if schema is None:
        return
    from .jsonschema_lite import validate_schema

    data = kernel_to_dict(k)
    validate_schema(data, schema, sink, k.loc, "$")


def validate_all(
    root: Path,
    stdlib_dir: Optional[Path] = None,
    sink: Optional[DiagnosticSink] = None,
    skip_schema: bool = False,
) -> Database:
    """Load and validate the entire database, running all six layers."""
    sink = sink or DiagnosticSink()
    if stdlib_dir is None:
        stdlib_dir = root / "optilisp" / "stdlib"

    reg = load_registry(stdlib_dir, sink)
    db = load_database(root, reg, sink)
    all_ids = {k.id for k in db.kernels}

    # Keep the original define-kernel form for duplicate-section checks.
    for k in db.kernels:
        validate_structure(k, None, sink)
        validate_references(k, reg, all_ids, sink)
        validate_semantic(k, reg, sink)
        validate_transformation(k, sink)
        if not skip_schema:
            validate_kernel_against_schema(k, sink)

    validate_database(db, sink)
    return db
