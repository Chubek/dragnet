"""Strict source-level contracts and conservative applicability, not an executor."""

from dataclasses import dataclass, field
from enum import Enum
import json
import math
from pathlib import Path
import re

from .bridge import ROOT, UPSTREAM, reader

SUPPLEMENT = ROOT / "tools/blackstone"
S = reader()
STAGES = {"hir", "mir", "lir"}
DOCUMENTATION = {
    "purpose", "motivation", "stage", "applicability", "preconditions",
    "required-information", "transformation", "semantic-assumptions",
    "correctness", "known-limitations", "expected-effects", "testing-strategy",
    "references", "recommended-tools",
}
SECTIONS = {
    "category", "paradigms", "objective", "requires", "preconditions", "forbids",
    "prerequisites", "forbidden-prior-transformations", "analysis-contract",
    "transform", "correctness", "expected-effects", "limitations", "documentation",
}
REQUIRED = SECTIONS - {"prerequisites", "forbidden-prior-transformations"}
VERBS = {
    "rewrite", "fold", "propagate", "eliminate", "hoist", "sink", "interchange",
    "unroll", "peel", "fuse", "distribute", "tile", "skew", "inline", "specialize",
    "devirtualize", "lower", "select", "combine", "promote", "demote", "coalesce",
    "rematerialize", "forward", "canonicalize", "simplify", "split", "merge",
    "rotate", "vectorize", "parallelize", "if-convert", "construct", "outline",
    "place", "schedule", "predict", "deduplicate", "remove", "strength-reduce",
}
STAGE_RESTRICTIONS = {
    "hir": {"lower", "select", "devirtualize", "coalesce", "rematerialize",
            "vectorize", "parallelize", "construct", "place", "schedule"},
    "mir": {"lower", "select", "rematerialize", "place", "schedule"},
    "lir": {"interchange", "unroll", "peel", "fuse", "distribute", "tile",
            "skew", "inline", "devirtualize", "vectorize", "parallelize", "construct"},
}


def name(form):
    return form.name if isinstance(form, S.Symbol) else None


def identifier(form):
    return form.value if isinstance(form, S.String) else name(form)


def encode(form):
    """Lossless, JSON-safe expression representation used for duplicate detection."""
    if isinstance(form, S.ListForm):
        return [encode(item) for item in form.items]
    if isinstance(form, (S.Symbol, S.Keyword)):
        return {type(form).__name__: form.name}
    if isinstance(form, S.Nil):
        return {"Nil": None}
    return {type(form).__name__: form.value}


@dataclass(frozen=True, order=True)
class Issue:
    file: str
    line: int
    column: int
    code: str
    message: str

    def render(self):
        return f"{self.file}:{self.line}:{self.column}: error [{self.code}]: {self.message}"


@dataclass
class Catalog:
    kernels: dict = field(default_factory=dict)
    capabilities: dict = field(default_factory=dict)
    analyses: dict = field(default_factory=dict)
    issues: list = field(default_factory=list)

    def error(self, code, message, form=None, path="<catalog>"):
        loc = getattr(form, "loc", None)
        self.issues.append(Issue(str(path), loc.line if loc else 1,
                                 loc.column if loc else 1, code, message))


def parse(source, path, catalog):
    """Bound work before invoking the upstream recursive reader."""
    if len(source.encode("utf-8")) > 1_048_576:
        catalog.error("E-LIMIT", "source exceeds 1 MiB", path=path)
        return []
    depth = 0
    quoted = escaped = comment = False
    for char in source:
        if comment:
            comment = char != "\n"
        elif quoted:
            if escaped:
                if char not in 'ntr"\\0':
                    catalog.error("E-SYNTAX", "invalid string escape", path=path)
                    return []
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == ";":
            comment = True
        elif char == '"':
            quoted = True
        elif char == "(":
            depth += 1
            if depth > 128:
                catalog.error("E-LIMIT", "nesting exceeds 128 levels", path=path)
                return []
        elif char == ")":
            depth -= 1
            if depth < 0:
                catalog.error("E-SYNTAX", "unexpected closing parenthesis", path=path)
                return []
    if depth or quoted:
        catalog.error("E-SYNTAX", "unterminated list or string", path=path)
        return []
    if re.search(r"[\u3400-\u9fff\uf900-\ufaff]", source):
        catalog.error("E-LANGUAGE", "Chinese text requires an English translation", path=path)
    try:
        forms, sink = S.read(source, str(path))
    except (RecursionError, ValueError) as exc:
        catalog.error("E-SYNTAX", str(exc), path=path)
        return []
    for diagnostic in sink.items:
        loc = diagnostic.location
        catalog.issues.append(Issue(str(path), loc.line, loc.column,
                                   diagnostic.code, diagnostic.message))
    work = list(forms)
    while work:
        item = work.pop()
        if isinstance(item, S.ListForm):
            work.extend(item.items)
        elif isinstance(item, S.Float) and not math.isfinite(item.value):
            catalog.error("E-NUMBER", "non-finite numeric literal", item, path)
    return [] if sink.has_errors() else forms


def fields(form, catalog, path):
    """Reject silent truncation, unknown fields, and repeated singleton fields."""
    if not isinstance(form, S.ListForm) or not form.items or not name(form.items[0]):
        catalog.error("E-FORM", "expected a named definition", form, path)
        return {}, {}
    keywords, sections = {}, {}
    items = form.items[1:]
    i = 0
    while i < len(items):
        item = items[i]
        if isinstance(item, S.Keyword) and not sections:
            if i + 1 == len(items) or isinstance(items[i + 1], S.Keyword):
                catalog.error("E-FIELD", "keyword requires a value", item, path)
                i += 1
                continue
            if item.name in keywords:
                catalog.error("E-DUPLICATE", f"duplicate :{item.name}", item, path)
            keywords[item.name] = items[i + 1]
            i += 2
        elif isinstance(item, S.ListForm) and item.items and name(item.items[0]):
            head = name(item.items[0])
            if head in sections:
                catalog.error("E-DUPLICATE", f"duplicate {head} section", item, path)
            sections[head] = item
            i += 1
        else:
            catalog.error("E-FIELD", "unexpected item or keyword after body", item, path)
            i += 1
    return keywords, sections


def atoms(section, catalog, path, predicate=False):
    result = []
    for item in section.items[1:] if section else []:
        value = identifier(item)
        if predicate and isinstance(item, S.ListForm) and len(item.items) == 1:
            value = name(item.items[0])
        if not value:
            catalog.error("E-TYPE", "expected a nonempty identifier", item, path)
        else:
            if value in result:
                catalog.error("E-DUPLICATE", f"duplicate identifier {value}", item, path)
            result.append(value)
    return result


def validate_kernel(form, catalog, path):
    kw, sections = fields(form, catalog, path)
    for key in set(kw) - {"id", "name", "stage"}:
        catalog.error("E-FIELD", f"unknown keyword :{key}", form, path)
    for key in set(sections) - SECTIONS:
        catalog.error("E-FIELD", f"unknown section {key}", sections[key], path)
    for key in sorted(REQUIRED - set(sections)):
        catalog.error("E-REQUIRED", f"missing {key}", form, path)
    kid = identifier(kw.get("id"))
    stage = name(kw.get("stage"))
    if not isinstance(kw.get("id"), S.String) or not kid or not re.fullmatch(
            r"blackstone\.(hir|mir|lir)\.[a-z0-9-]+\.[a-z0-9.-]+", kid):
        catalog.error("E-ID", "invalid canonical kernel ID", form, path)
    elif stage != kid.split(".")[1]:
        catalog.error("E-STAGE", "ID and declared stage differ", form, path)
    if stage not in STAGES:
        catalog.error("E-STAGE", "stage must be hir, mir, or lir", form, path)
    display = kw.get("name")
    if not isinstance(display, S.String) or not display.value.strip():
        catalog.error("E-TYPE", "name must be a nonempty string", form, path)
    lists = {}
    for key in ("category", "paradigms", "preconditions", "forbids", "correctness"):
        lists[key] = atoms(sections.get(key), catalog, path, True)
        if not lists[key]:
            catalog.error("E-REQUIRED", f"{key} must be nonempty", form, path)
    if len(lists["category"]) != 1:
        catalog.error("E-OBJECTIVE", "exactly one primary category required", form, path)
    for key in ("objective", "expected-effects", "limitations"):
        section = sections.get(key)
        if not section or len(section.items) != 2 or not isinstance(section.items[1], S.String) or not section.items[1].value.strip():
            catalog.error("E-TYPE", f"{key} requires exactly one nonempty string", section, path)
    for condition in sorted(set(lists["preconditions"]) & set(lists["forbids"])):
        catalog.error("E-CONTRADICTION", f"both requires and forbids {condition}", form, path)
    requirements = []
    section = sections.get("requires")
    for item in section.items[1:] if section else []:
        if not isinstance(item, S.ListForm) or len(item.items) != 2 or name(item.items[0]) not in {"analysis", "capability"} or not identifier(item.items[1]):
            catalog.error("E-REQUIREMENT", "expected (analysis ID) or (capability ID)", item, path)
        else:
            req = (name(item.items[0]), identifier(item.items[1]))
            if req in requirements:
                catalog.error("E-DUPLICATE", f"duplicate requirement {req}", item, path)
            requirements.append(req)
    if not any(kind == "capability" for kind, _ in requirements):
        catalog.error("E-REQUIRED", "declare at least one IR capability", form, path)
    contract = {}
    section = sections.get("analysis-contract")
    for item in section.items[1:] if section else []:
        head = name(item.items[0]) if isinstance(item, S.ListForm) and item.items else None
        if head not in {"requires", "invalidates", "preserves"}:
            catalog.error("E-CONTRACT", "unknown analysis contract clause", item, path)
        else:
            if head in contract:
                catalog.error("E-DUPLICATE", f"duplicate contract {head}", item, path)
            contract[head] = atoms(item, catalog, path)
    if set(contract) != {"requires", "invalidates", "preserves"}:
        catalog.error("E-CONTRACT", "explicit requires/invalidates/preserves clauses required", section, path)
    required_analyses = {value for kind, value in requirements if kind == "analysis"}
    if set(contract.get("requires", [])) != required_analyses:
        catalog.error("E-CONTRACT", "consumed analyses must match requires declarations", section, path)
    if set(contract.get("invalidates", [])) & set(contract.get("preserves", [])):
        catalog.error("E-CONTRADICTION", "analysis both invalidated and preserved", section, path)
    if not contract.get("invalidates"):
        catalog.error("E-CONTRACT", "transformation must declare analysis invalidation", section, path)
    transform = sections.get("transform")
    expression = transform.items[1] if transform and len(transform.items) == 2 else None
    if not isinstance(expression, S.ListForm) or len(expression.items) < 2 or name(expression.items[0]) not in VERBS:
        catalog.error("E-TRANSFORM", "exactly one recognized nonempty transformation required", transform, path)
    elif name(expression.items[0]) in {"rewrite", "fold"} and len(expression.items) != 3:
        catalog.error("E-TRANSFORM", "rewrite/fold requires exactly pattern and result", expression, path)
    elif name(expression.items[0]) in STAGE_RESTRICTIONS.get(stage, set()):
        catalog.error("E-STAGE", "transformation incompatible with declared stage", expression, path)
    documentation = sections.get("documentation")
    docs = {}
    for item in documentation.items[1:] if documentation else []:
        head = name(item.items[0]) if isinstance(item, S.ListForm) and item.items else None
        if head not in DOCUMENTATION or len(item.items) != 2 or not isinstance(item.items[1], S.String) or not item.items[1].value.strip():
            catalog.error("E-DOC", "expected a documented topic with one nonempty string", item, path)
        else:
            if head in docs:
                catalog.error("E-DUPLICATE", f"duplicate documentation {head}", item, path)
            docs[head] = item.items[1].value
    for topic in sorted(DOCUMENTATION - set(docs)):
        catalog.error("E-DOC", f"missing documentation topic {topic}", documentation, path)
    prerequisites = []
    section = sections.get("prerequisites")
    for item in section.items[1:] if section else []:
        if not isinstance(item, S.ListForm) or len(item.items) != 2 or name(item.items[0]) != "optimization-applied" or not isinstance(item.items[1], S.String):
            catalog.error("E-DEPENDENCY", "expected (optimization-applied STRING)", item, path)
        else:
            prerequisites.append(item.items[1].value)
    forbidden = atoms(sections.get("forbidden-prior-transformations"), catalog, path)
    if set(prerequisites) & set(forbidden):
        catalog.error("E-CONTRADICTION", "history requirement also forbidden", form, path)
    record = dict(id=kid, stage=stage, file=str(path), requirements=requirements,
                  preconditions=lists["preconditions"], forbids=lists["forbids"],
                  category=lists["category"], contract=contract, docs=docs,
                  prerequisites=prerequisites, forbidden_prior=forbidden,
                  transform=encode(expression) if expression else None)
    if kid:
        if kid in catalog.kernels:
            catalog.error("E-DUPLICATE", f"duplicate kernel ID {kid}", form, path)
        else:
            catalog.kernels[kid] = record
    return record


def references(catalog):
    fingerprints = {}
    for kid, k in sorted(catalog.kernels.items()):
        for kind, value in k["requirements"]:
            registry = catalog.capabilities if kind == "capability" else catalog.analyses
            if value not in registry:
                catalog.error("E-REFERENCE", f"{kid}: unknown {kind} {value}", path=k["file"])
            elif kind == "capability" and k["stage"] not in registry[value]:
                catalog.error("E-STAGE", f"{kid}: capability {value} incompatible with stage", path=k["file"])
        for values in k["contract"].values():
            for value in values:
                if value not in catalog.analyses:
                    catalog.error("E-REFERENCE", f"{kid}: unknown analysis {value}", path=k["file"])
        for dep in k["prerequisites"] + k["forbidden_prior"]:
            if dep not in catalog.kernels:
                catalog.error("E-REFERENCE", f"{kid}: unresolved kernel {dep}", path=k["file"])
        # Same transform, facts, stage and objective category describe duplicate kernels.
        fingerprint = json.dumps([k["stage"], k["transform"], sorted(k["preconditions"]),
                                  sorted(k["forbids"])], sort_keys=True)
        if fingerprint in fingerprints:
            catalog.error("E-REDUNDANCY", f"{kid}: duplicate contract of {fingerprints[fingerprint]}", path=k["file"])
        fingerprints[fingerprint] = kid
    # Iterative DFS bounds stack use even for long dependency chains.
    done = set()
    for start in sorted(catalog.kernels):
        stack = [(start, False)]
        active = set()
        while stack:
            node, leaving = stack.pop()
            if leaving:
                active.discard(node)
                done.add(node)
            elif node in active:
                catalog.error("E-CYCLE", f"prerequisite cycle involving {node}", path=catalog.kernels[node]["file"])
            elif node not in done:
                active.add(node)
                stack.append((node, True))
                stack.extend((dep, False) for dep in reversed(sorted(catalog.kernels[node]["prerequisites"])) if dep in catalog.kernels)


def load(scope="supplement"):
    if scope not in {"supplement", "all"}:
        raise ValueError("scope must be supplement or all")
    catalog = Catalog()
    for path in sorted((UPSTREAM / "optilisp/stdlib").rglob("*.olisp")):
        for form in parse(path.read_text(encoding="utf-8"), path, catalog):
            kw, _ = fields(form, catalog, path)
            kind = name(form.items[0])
            value = identifier(kw.get("id"))
            if kind not in {"define-capability", "define-analysis", "define-monoid"} or not value:
                catalog.error("E-REGISTRY", "unknown registry definition or missing ID", form, path)
                continue
            registry = catalog.capabilities if kind == "define-capability" else catalog.analyses if kind == "define-analysis" else None
            if registry is not None:
                if value in registry:
                    catalog.error("E-DUPLICATE", f"duplicate registry ID {value}", form, path)
                stages = kw.get("stages")
                registry[value] = atoms(S.ListForm([S.Symbol("stages")] + stages.items), catalog, path) if isinstance(stages, S.ListForm) else []
                if kind == "define-capability" and (not registry[value] or set(registry[value]) - STAGES):
                    catalog.error("E-STAGE", "capability requires recognized nonempty stages", form, path)
    roots = [SUPPLEMENT / "kernels"]
    if scope == "all":
        roots.insert(0, UPSTREAM / "kernels")
    for root in roots:
        for path in sorted(root.rglob("*.olisp")):
            forms = parse(path.read_text(encoding="utf-8"), path, catalog)
            if len(forms) != 1:
                catalog.error("E-FORM", "one canonical kernel definition per file required", path=path)
            for form in forms:
                if not isinstance(form, S.ListForm) or not form.items or name(form.items[0]) != "define-kernel":
                    catalog.error("E-FORM", "expected define-kernel", form, path)
                else:
                    record = validate_kernel(form, catalog, path)
                    directory_stage = path.relative_to(root).parts[0]
                    if directory_stage != record["stage"]:
                        catalog.error("E-LAYOUT", "directory and declared stage differ", form, path)
    if not catalog.kernels:
        catalog.error("E-CATALOG", "kernel catalog is empty")
    references(catalog)
    catalog.issues.sort()
    return catalog


class Fact(Enum):
    TRUE = "true"
    FALSE = "false"
    UNKNOWN = "unknown"
    NOT_COMPUTED = "not-computed"
    INVALIDATED = "invalidated"


def applicability(kernel, stage, facts):
    """Every positive fact needs TRUE, every forbidden fact needs FALSE."""
    reasons = []
    if kernel["stage"] != stage:
        reasons.append("stage mismatch")
    positive = [f"{kind}:{value}" for kind, value in kernel["requirements"]]
    positive += ["condition:" + value for value in kernel["preconditions"]]
    positive += ["history:" + value for value in kernel["prerequisites"]]
    negative = ["condition:" + value for value in kernel["forbids"]]
    negative += ["history:" + value for value in kernel["forbidden_prior"]]
    for expected, keys in ((Fact.TRUE, positive), (Fact.FALSE, negative)):
        for key in keys:
            actual = facts.get(key, Fact.NOT_COMPUTED)
            if actual != expected:
                reasons.append(f"{key}: expected {expected.value}, got {actual.value}")
    return not reasons, sorted(reasons)


def invalidate(kernel, facts):
    """Return a new fact map; unknown analysis preservation is conservative."""
    result = dict(facts)
    preserved = set(kernel["contract"].get("preserves", []))
    for key in result:
        if key.startswith("analysis:") and key[9:] not in preserved:
            result[key] = Fact.INVALIDATED
    for value in kernel["contract"].get("invalidates", []):
        result["analysis:" + value] = Fact.INVALIDATED
    return result


def profitability(kernel, facts):
    """Separate planner policy: absent cost facts cannot make a rewrite illegal."""
    policy = json.loads((SUPPLEMENT / "profitability.json").read_text(encoding="utf-8"))
    keys = policy.get(kernel["stage"], [])
    missing = [key for key in keys if facts.get(key, Fact.NOT_COMPUTED) != Fact.TRUE]
    return not missing, sorted(missing)
