"""Optilisp: Blackstone's declarative S-expression language.

Public API:

- :func:`read`            -- Layer 1 (syntax): parse source into forms
- :func:`analyze_file`    -- parsed forms -> typed records
- :func:`validate_all`    -- run all six validation layers
- :func:`run_query`       -- query the kernel database
- :func:`build_index`     -- build the searchable index
"""

from .errors import Diagnostic, DiagnosticSink, Location
from .query import Query, build_index, run_query, summarize
from .semantic import (
    STAGES,
    Analysis,
    Capability,
    Kernel,
    Monoid,
    analyze_file,
)
from .sexpr import Form, ListForm, read
from .validate import Database, Registry, load_database, load_registry, validate_all

__all__ = [
    "Diagnostic",
    "DiagnosticSink",
    "Location",
    "Form",
    "ListForm",
    "read",
    "analyze_file",
    "STAGES",
    "Kernel",
    "Monoid",
    "Capability",
    "Analysis",
    "Database",
    "Registry",
    "load_registry",
    "load_database",
    "validate_all",
    "Query",
    "run_query",
    "build_index",
    "summarize",
]

__version__ = "0.1.0"
