"""Semantic analysis: parsed forms -> typed records.

This module knows the *shape* of the declarative forms (``define-kernel``,
``define-monoid``, ``define-capability``, ``define-analysis``) but deliberately
does not judge whether the content is meaningful.  Structural rules (required
fields, types) and reference rules (do the named capabilities exist?) are
enforced later by :mod:`optilisp.validate`, keeping parsing, structural
validation, and semantic validation separate (spec sections 7.4 and 17).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .errors import DiagnosticSink, Location
from .sexpr import (
    Form,
    Integer,
    Keyword,
    ListForm,
    String,
    Symbol,
    as_string,
    is_keyword,
    is_symbol,
)

STAGES = ("hir", "mir", "lir")

# Transformation verbs recognized by the validator.  Each kernel uses exactly
# one primary verb; the vocabulary is closed so that unknown verbs are caught.
TRANSFORM_VERBS = frozenset({
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
    "interchange", "unroll", "peel", "fuse", "distribute", "tile", "skew",
    "inline", "specialize", "devirtualize", "lower", "select", "combine",
    "promote", "demote", "coalesce", "rematerialize", "forward",
    "canonicalize", "simplify", "split", "merge", "rotate", "vectorize",
    "parallelize", "if-convert", "construct", "outline", "place", "schedule",
    "predict", "deduplicate", "remove", "strength-reduce", "devirtualize",
})

# Verbs that are only meaningful at particular stages (spec section 4.1).
STAGE_VERBS = {
    "hir": frozenset({
        "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
        "interchange", "unroll", "peel", "fuse", "distribute", "tile",
        "inline", "specialize", "canonicalize", "simplify", "split",
        "merge", "deduplicate", "remove", "strength-reduce",
    }),
    "mir": frozenset({
        "rewrite", "fold", "propagate", "eliminate", "hoist", "sink",
        "interchange", "unroll", "peel", "fuse", "distribute", "tile",
        "skew", "inline", "specialize", "devirtualize", "promote",
        "forward", "canonicalize", "simplify", "split", "merge",
        "vectorize", "parallelize", "if-convert", "construct", "outline",
        "deduplicate", "remove", "strength-reduce", "coalesce",
    }),
    "lir": frozenset({
        "rewrite", "fold", "eliminate", "hoist", "lower", "select",
        "combine", "promote", "demote", "coalesce", "rematerialize",
        "canonicalize", "simplify", "split", "merge", "place", "schedule",
        "deduplicate", "remove", "strength-reduce", "sink",
    }),
}


@dataclass(frozen=True)
class Requirement:
    """A required capability or analysis."""

    kind: str  # "capability" | "analysis"
    name: str
    loc: Optional[Location] = None


@dataclass(frozen=True)
class AnalysisContract:
    requires: List[str] = field(default_factory=list)
    invalidates: List[str] = field(default_factory=list)
    preserves: List[str] = field(default_factory=list)
    loc: Optional[Location] = None


@dataclass(frozen=True)
class Transform:
    verb: str
    args: List[Form]
    loc: Optional[Location] = None


@dataclass(frozen=True)
class Documentation:
    prose: List[str] = field(default_factory=list)
    structured: Dict[str, List[Form]] = field(default_factory=dict)
    loc: Optional[Location] = None


@dataclass
class Kernel:
    id: str
    name: str
    stage: str
    categories: List[str] = field(default_factory=list)
    paradigms: List[str] = field(default_factory=list)
    objective: str = ""
    requires: List[Requirement] = field(default_factory=list)
    preconditions: List[str] = field(default_factory=list)
    forbids: List[str] = field(default_factory=list)
    prerequisites: List[str] = field(default_factory=list)
    forbidden_prior: List[str] = field(default_factory=list)
    analysis_contract: Optional[AnalysisContract] = None
    transform: Optional[Transform] = None
    correctness: List[str] = field(default_factory=list)
    expected_effects: str = ""
    limitations: str = ""
    documentation: Optional[Documentation] = None
    file: str = ""
    loc: Optional[Location] = None


@dataclass
class Monoid:
    id: str
    name: str
    carrier: str = ""
    operation: str = ""
    identity: str = ""
    laws: List[str] = field(default_factory=list)
    description: str = ""
    file: str = ""
    loc: Optional[Location] = None


@dataclass
class Capability:
    id: str
    name: str
    description: str = ""
    stages: List[str] = field(default_factory=list)
    file: str = ""
    loc: Optional[Location] = None


@dataclass
class Analysis:
    id: str
    name: str
    description: str = ""
    file: str = ""
    loc: Optional[Location] = None


# ---------------------------------------------------------------------------
# Form extraction helpers
# ---------------------------------------------------------------------------


def _keyword_args(form: ListForm) -> Dict[str, Form]:
    """Collect leading ``:keyword value`` argument pairs from a define form."""
    args: Dict[str, Form] = {}
    items = form.items[1:]
    i = 0
    while i < len(items):
        item = items[i]
        if isinstance(item, Keyword):
            if i + 1 < len(items):
                args[item.name] = items[i + 1]
                i += 2
            else:
                args[item.name] = None  # type: ignore[assignment]
                i += 1
        else:
            break
    return args


def _body_forms(form: ListForm) -> List[ListForm]:
    """Collect the parenthesized body sections that follow the keyword arguments."""
    body: List[ListForm] = []
    items = form.items[1:]
    i = 0
    # Skip leading keyword argument pairs.
    while i < len(items) and isinstance(items[i], Keyword):
        i += 2
    # Collect body sections.
    while i < len(items):
        item = items[i]
        if isinstance(item, ListForm):
            body.append(item)
            i += 1
        else:
            break
    return body


def _section(body: List[ListForm], head: str) -> Optional[ListForm]:
    for form in body:
        if form.items and is_symbol(form.items[0], head):
            return form
    return None


def _string_items(form: Optional[ListForm]) -> List[str]:
    if form is None:
        return []
    out: List[str] = []
    for item in form.items[1:]:
        s = as_string(item)
        if s is not None:
            out.append(s)
    return out


def _symbol_items(form: Optional[ListForm]) -> List[str]:
    """Extract symbol items, handling both bare symbols and nested one-element lists.

    Handles ``(category foo)`` (bare symbol) and ``(preconditions (foo) (bar))``
    (nested list whose head symbol is the item).
    """
    if form is None:
        return []
    out: List[str] = []
    for item in form.items[1:]:
        if isinstance(item, Symbol):
            out.append(item.name)
        elif isinstance(item, ListForm) and item.items and isinstance(item.items[0], Symbol):
            out.append(item.items[0].name)
    return out


def _requirement_items(form: Optional[ListForm]) -> List[Requirement]:
    if form is None:
        return []
    out: List[Requirement] = []
    for item in form.items[1:]:
        if isinstance(item, ListForm) and item.items:
            head = item.items[0]
            if is_symbol(head, "capability") and len(item.items) >= 2:
                name = as_string(item.items[1]) or (
                    item.items[1].name if isinstance(item.items[1], Symbol) else ""
                )
                out.append(Requirement("capability", name, item.loc))
            elif is_symbol(head, "analysis") and len(item.items) >= 2:
                name = as_string(item.items[1]) or (
                    item.items[1].name if isinstance(item.items[1], Symbol) else ""
                )
                out.append(Requirement("analysis", name, item.loc))
    return out


def _kernel_id_items(form: Optional[ListForm]) -> List[str]:
    if form is None:
        return []
    out: List[str] = []
    for item in form.items[1:]:
        if isinstance(item, ListForm) and item.items and is_symbol(item.items[0], "optimization-applied"):
            if len(item.items) >= 2:
                s = as_string(item.items[1])
                if s is not None:
                    out.append(s)
    return out


def _plain_id_items(form: Optional[ListForm]) -> List[str]:
    """Extract identifier items, handling both symbols and strings."""
    if form is None:
        return []
    out: List[str] = []
    for item in form.items[1:]:
        if isinstance(item, Symbol):
            out.append(item.name)
        elif isinstance(item, String):
            out.append(item.value)
    return out


def _analysis_contract(form: Optional[ListForm]) -> Optional[AnalysisContract]:
    if form is None:
        return None
    req: List[str] = []
    inv: List[str] = []
    pres: List[str] = []
    for item in form.items[1:]:
        if isinstance(item, ListForm) and item.items:
            head = item.items[0]
            if is_symbol(head, "requires"):
                req.extend(_plain_id_items(item))
            elif is_symbol(head, "invalidates"):
                inv.extend(_plain_id_items(item))
            elif is_symbol(head, "preserves"):
                pres.extend(_plain_id_items(item))
    return AnalysisContract(req, inv, pres, form.loc)


def _law_items(form: Optional[ListForm]) -> List[str]:
    """Extract monoid law items, preserving the full list structure.

    Each law is a list such as ``(associative integer-add)``; the item is
    rendered as its space-joined string representation.
    """
    if form is None:
        return []
    out: List[str] = []
    for item in form.items[1:]:
        if isinstance(item, ListForm):
            out.append(" ".join(str(x) for x in item.items))
        elif isinstance(item, Symbol):
            out.append(item.name)
    return out


def _transform(form: Optional[ListForm]) -> Optional[Transform]:
    """Extract the transformation verb and arguments.

    The transform section has the shape ``(transform (verb args...) ...)``;
    the verb is the head symbol of the first nested list.
    """
    if form is None:
        return None
    for item in form.items[1:]:
        if isinstance(item, ListForm) and item.items and isinstance(item.items[0], Symbol):
            return Transform(item.items[0].name, list(item.items[1:]), form.loc)
    return None


def _documentation(form: Optional[ListForm]) -> Optional[Documentation]:
    if form is None:
        return None
    prose: List[str] = []
    structured: Dict[str, List[Form]] = {}
    for item in form.items[1:]:
        s = as_string(item)
        if s is not None:
            prose.append(s)
        elif isinstance(item, ListForm) and item.items:
            head = item.items[0].name if isinstance(item.items[0], Symbol) else ""
            if head:
                structured.setdefault(head, []).append(item)
    return Documentation(prose, structured, form.loc)


# ---------------------------------------------------------------------------
# Top-level analysis
# ---------------------------------------------------------------------------


def analyze_kernel(form: ListForm, file: str, sink: DiagnosticSink) -> Optional[Kernel]:
    """Build a Kernel record from a ``define-kernel`` form."""
    if not form.items or not is_symbol(form.items[0], "define-kernel"):
        return None

    kwargs = _keyword_args(form)
    body = _body_forms(form)

    def req_kw(name: str) -> Optional[str]:
        f = kwargs.get(name)
        return as_string(f) if f is not None else None

    kid = req_kw("id") or ""
    name = req_kw("name") or ""
    stage_f = kwargs.get("stage")
    stage = stage_f.name if isinstance(stage_f, Symbol) else ""

    if not kid:
        sink.error("E-KERNEL-ID", "kernel is missing a stable :id", form.loc)
    if not name:
        sink.error("E-KERNEL-NAME", "kernel is missing a human-readable :name", form.loc)
    if not stage:
        sink.error("E-KERNEL-STAGE", "kernel is missing a :stage declaration", form.loc)
    elif stage not in STAGES:
        sink.error("E-KERNEL-STAGE", f"unrecognized stage '{stage}' (expected hir, mir, or lir)", form.loc)

    categories = _symbol_items(_section(body, "category"))
    paradigms = _symbol_items(_section(body, "paradigms"))
    objective = " ".join(_string_items(_section(body, "objective")))
    requires = _requirement_items(_section(body, "requires"))
    preconditions = _symbol_items(_section(body, "preconditions"))
    forbids = _symbol_items(_section(body, "forbids"))
    prerequisites = _kernel_id_items(_section(body, "prerequisites"))
    forbidden_prior = _plain_id_items(_section(body, "forbidden-prior-transformations"))
    contract = _analysis_contract(_section(body, "analysis-contract"))
    transform = _transform(_section(body, "transform"))
    correctness = _symbol_items(_section(body, "correctness"))
    expected_effects = " ".join(_string_items(_section(body, "expected-effects")))
    limitations = " ".join(_string_items(_section(body, "limitations")))
    documentation = _documentation(_section(body, "documentation"))

    return Kernel(
        id=kid, name=name, stage=stage, categories=categories, paradigms=paradigms,
        objective=objective, requires=requires, preconditions=preconditions,
        forbids=forbids, prerequisites=prerequisites, forbidden_prior=forbidden_prior,
        analysis_contract=contract, transform=transform, correctness=correctness,
        expected_effects=expected_effects, limitations=limitations,
        documentation=documentation, file=file, loc=form.loc,
    )


def analyze_monoid(form: ListForm, file: str, sink: DiagnosticSink) -> Optional[Monoid]:
    if not form.items or not is_symbol(form.items[0], "define-monoid"):
        return None
    kwargs = _keyword_args(form)
    body = _body_forms(form)

    def req_kw(name: str) -> Optional[str]:
        f = kwargs.get(name)
        return as_string(f) if f is not None else None

    mid = req_kw("id") or ""
    name = req_kw("name") or ""
    carrier_f = kwargs.get("carrier")
    carrier = carrier_f.name if isinstance(carrier_f, Symbol) else ""
    operation_f = kwargs.get("operation")
    operation = operation_f.name if isinstance(operation_f, Symbol) else ""
    identity_f = kwargs.get("identity")
    if isinstance(identity_f, String):
        identity = identity_f.value
    elif isinstance(identity_f, Symbol):
        identity = identity_f.name
    elif isinstance(identity_f, Integer):
        identity = str(identity_f.value)
    else:
        identity = ""
    laws = _law_items(_section(body, "laws"))
    description = " ".join(_string_items(_section(body, "description")))

    if not mid:
        sink.error("E-MONOID-ID", "monoid is missing a stable :id", form.loc)

    return Monoid(mid, name, carrier, operation, identity, laws, description, file, form.loc)


def analyze_capability(form: ListForm, file: str, sink: DiagnosticSink) -> Optional[Capability]:
    if not form.items or not is_symbol(form.items[0], "define-capability"):
        return None
    kwargs = _keyword_args(form)
    body = _body_forms(form)

    def req_kw(name: str) -> Optional[str]:
        f = kwargs.get(name)
        return as_string(f) if f is not None else None

    cid = req_kw("id") or ""
    name = req_kw("name") or ""
    description = " ".join(_string_items(_section(body, "description")))

    # :stages is a keyword argument whose value is a list of stage symbols.
    stages: List[str] = []
    stages_f = kwargs.get("stages")
    if isinstance(stages_f, ListForm):
        stages = [item.name for item in stages_f.items if isinstance(item, Symbol)]

    if not cid:
        sink.error("E-CAPABILITY-ID", "capability is missing a stable :id", form.loc)

    return Capability(cid, name, description, stages, file, form.loc)


def analyze_analysis(form: ListForm, file: str, sink: DiagnosticSink) -> Optional[Analysis]:
    if not form.items or not is_symbol(form.items[0], "define-analysis"):
        return None
    kwargs = _keyword_args(form)
    body = _body_forms(form)

    def req_kw(name: str) -> Optional[str]:
        f = kwargs.get(name)
        return as_string(f) if f is not None else None

    aid = req_kw("id") or ""
    name = req_kw("name") or ""
    description = " ".join(_string_items(_section(body, "description")))

    if not aid:
        sink.error("E-ANALYSIS-ID", "analysis is missing a stable :id", form.loc)

    return Analysis(aid, name, description, file, form.loc)


def analyze_file(forms: List[Form], file: str, sink: DiagnosticSink) -> Dict[str, list]:
    """Analyze all top-level forms in a file, dispatching on the head symbol."""
    kernels: List[Kernel] = []
    monoids: List[Monoid] = []
    capabilities: List[Capability] = []
    analyses: List[Analysis] = []

    for form in forms:
        if not isinstance(form, ListForm) or not form.items:
            sink.error("E-FORM", "top-level form must be a parenthesized definition", form.loc)
            continue
        head = form.items[0]
        if is_symbol(head, "define-kernel"):
            k = analyze_kernel(form, file, sink)
            if k is not None:
                kernels.append(k)
        elif is_symbol(head, "define-monoid"):
            m = analyze_monoid(form, file, sink)
            if m is not None:
                monoids.append(m)
        elif is_symbol(head, "define-capability"):
            c = analyze_capability(form, file, sink)
            if c is not None:
                capabilities.append(c)
        elif is_symbol(head, "define-analysis"):
            a = analyze_analysis(form, file, sink)
            if a is not None:
                analyses.append(a)
        else:
            sink.error(
                "E-FORM",
                f"unknown top-level form '{(head.name if isinstance(head, Symbol) else '?')}' "
                "(expected define-kernel, define-monoid, define-capability, or define-analysis)",
                form.loc,
            )

    return {
        "kernels": kernels,
        "monoids": monoids,
        "capabilities": capabilities,
        "analyses": analyses,
    }
