"""Database querying and discovery (spec section 19).

Supports filtering kernels by stage, category, paradigm, required capability,
required analysis, invalidated analysis, prerequisites, and more.  Queries are
deterministic: results are always returned in sorted kernel-id order.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, List, Optional

from .semantic import Kernel
from .validate import Database


@dataclass
class Query:
    """A declarative query over the kernel database."""

    stage: Optional[str] = None
    category: Optional[str] = None
    paradigm: Optional[str] = None
    requires_capability: Optional[str] = None
    requires_analysis: Optional[str] = None
    invalidates: Optional[str] = None
    has_prerequisites: Optional[bool] = None
    verb: Optional[str] = None
    text: Optional[str] = None
    predicate: Optional[Callable[[Kernel], bool]] = None


def run_query(db: Database, q: Query) -> List[Kernel]:
    """Run a query, returning matching kernels in sorted-id order."""
    results: List[Kernel] = []
    for k in db.kernels:
        if q.stage is not None and k.stage != q.stage:
            continue
        if q.category is not None and q.category not in k.categories:
            continue
        if q.paradigm is not None and q.paradigm not in k.paradigms:
            continue
        if q.requires_capability is not None:
            if not any(r.kind == "capability" and r.name == q.requires_capability for r in k.requires):
                continue
        if q.requires_analysis is not None:
            if not any(r.kind == "analysis" and r.name == q.requires_analysis for r in k.requires):
                continue
        if q.invalidates is not None:
            if k.analysis_contract is None or q.invalidates not in k.analysis_contract.invalidates:
                continue
        if q.has_prerequisites is not None:
            has = bool(k.prerequisites)
            if has != q.has_prerequisites:
                continue
        if q.verb is not None:
            if k.transform is None or k.transform.verb != q.verb:
                continue
        if q.text is not None:
            hay = " ".join([
                k.id, k.name, k.objective, k.expected_effects, k.limitations,
                " ".join(k.categories), " ".join(k.paradigms),
            ]).lower()
            if q.text.lower() not in hay:
                continue
        if q.predicate is not None and not q.predicate(k):
            continue
        results.append(k)
    return sorted(results, key=lambda k: k.id)


def build_index(db: Database) -> dict:
    """Build a searchable index: stage -> category -> [kernel ids]."""
    index: dict = {}
    for k in db.kernels:
        for cat in k.categories or ["<uncategorized>"]:
            index.setdefault(k.stage, {}).setdefault(cat, []).append(k.id)
    for stage in index:
        for cat in index[stage]:
            index[stage][cat].sort()
    return index


def summarize(db: Database) -> dict:
    """Return a summary of the database for inspection."""
    index = build_index(db)
    return {
        "total_kernels": len(db.kernels),
        "stages": {stage: sum(len(v) for v in cats.values()) for stage, cats in index.items()},
        "categories": {stage: {cat: len(ids) for cat, ids in cats.items()} for stage, cats in index.items()},
        "capabilities": len(db.registry.capabilities),
        "analyses": len(db.registry.analyses),
        "monoids": len(db.registry.monoids),
    }
