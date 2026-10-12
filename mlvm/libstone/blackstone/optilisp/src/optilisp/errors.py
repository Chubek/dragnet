"""Diagnostics with source locations for Optilisp.

Every diagnostic carries a severity, a stable code, a human-readable message,
and a source location (file, line, column).  Diagnostics are collected rather
than raised so that a single run can report many problems, which is essential
for a declarative database validator.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


SEVERITY_ERROR = "error"
SEVERITY_WARNING = "warning"
SEVERITY_NOTE = "note"


@dataclass(frozen=True)
class Location:
    """A source location.  Line and column are 1-based."""

    file: str
    line: int
    column: int

    def render(self) -> str:
        return f"{self.file}:{self.line}:{self.column}"


@dataclass(frozen=True)
class Diagnostic:
    severity: str
    code: str
    message: str
    location: Optional[Location] = None
    related: List[Location] = field(default_factory=list)

    def render(self) -> str:
        loc = self.location.render() if self.location else "<unknown>"
        return f"{loc}: {self.severity}: [{self.code}] {self.message}"


class DiagnosticSink:
    """Collects diagnostics in a deterministic order."""

    def __init__(self) -> None:
        self.items: List[Diagnostic] = []

    def emit(
        self,
        severity: str,
        code: str,
        message: str,
        location: Optional[Location] = None,
        related: Optional[List[Location]] = None,
    ) -> None:
        self.items.append(
            Diagnostic(severity, code, message, location, related or [])
        )

    def error(self, code: str, message: str, location: Optional[Location] = None) -> None:
        self.emit(SEVERITY_ERROR, code, message, location)

    def warning(self, code: str, message: str, location: Optional[Location] = None) -> None:
        self.emit(SEVERITY_WARNING, code, message, location)

    def note(self, code: str, message: str, location: Optional[Location] = None) -> None:
        self.emit(SEVERITY_NOTE, code, message, location)

    @property
    def errors(self) -> List[Diagnostic]:
        return [d for d in self.items if d.severity == SEVERITY_ERROR]

    @property
    def warnings(self) -> List[Diagnostic]:
        return [d for d in self.items if d.severity == SEVERITY_WARNING]

    def has_errors(self) -> bool:
        return any(d.severity == SEVERITY_ERROR for d in self.items)

    def render(self) -> str:
        return "\n".join(d.render() for d in self.items)

    def __len__(self) -> int:
        return len(self.items)
