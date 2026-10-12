"""Optilisp reader: Layer 1 (syntax).

The reader is deliberately generic.  It recognizes only the surface grammar of
the language:

    program  := form*
    form     := list | atom
    list     := '(' form* ')'
    atom     := symbol | keyword | string | number | boolean | nil

It knows nothing about ``define-kernel``, ``rewrite``, or any other semantic
form.  Domain knowledge lives in :mod:`optilisp.semantic` and the validator,
which keeps parsing and semantic validation separate (spec section 7.4).

Every form carries a :class:`~optilisp.errors.Location` so that later layers can
produce diagnostics such as::

    kernels/hir/constant-folding/integer-add.olisp:18:7:
    error: expected a valid kernel transformation form
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Union

from .errors import DiagnosticSink, Location

# ---------------------------------------------------------------------------
# Form types
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Symbol:
    name: str
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return self.name


@dataclass(frozen=True)
class Keyword:
    name: str  # without the leading ':'
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return ":" + self.name


@dataclass(frozen=True)
class String:
    value: str
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return '"' + self.value + '"'


@dataclass(frozen=True)
class Integer:
    value: int
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return str(self.value)


@dataclass(frozen=True)
class Float:
    value: float
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return repr(self.value)


@dataclass(frozen=True)
class Boolean:
    value: bool
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return "true" if self.value else "false"


@dataclass(frozen=True)
class Nil:
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return "nil"


@dataclass(frozen=True)
class ListForm:
    items: List["Form"]
    loc: Optional[Location] = None

    def __str__(self) -> str:
        return "(" + " ".join(str(i) for i in self.items) + ")"


Form = Union[Symbol, Keyword, String, Integer, Float, Boolean, Nil, ListForm]


def is_symbol(form: Form, name: Optional[str] = None) -> bool:
    return isinstance(form, Symbol) and (name is None or form.name == name)


def is_keyword(form: Form, name: Optional[str] = None) -> bool:
    return isinstance(form, Keyword) and (name is None or form.name == name)


def as_symbol(form: Form) -> Optional[str]:
    return form.name if isinstance(form, Symbol) else None


def as_string(form: Form) -> Optional[str]:
    return form.value if isinstance(form, String) else None


# ---------------------------------------------------------------------------
# Reader
# ---------------------------------------------------------------------------

_DELIMS = set("()")
_WHITESPACE = set(" \t\r\n\f\v")
_SYMBOL_EXTRA = set("._+/-*=<>!?#@%&|^~")


class Reader:
    """Reads Optilisp forms from a string, tracking source locations."""

    def __init__(self, source: str, file: str = "<string>") -> None:
        self.source = source
        self.file = file
        self.pos = 0
        self.line = 1
        self.col = 1
        self.sink = DiagnosticSink()

    # -- low-level helpers --------------------------------------------------

    def _error(self, message: str, line: Optional[int] = None, col: Optional[int] = None) -> None:
        loc = Location(self.file, line if line is not None else self.line,
                       col if col is not None else self.col)
        self.sink.error("E-SYNTAX", message, loc)

    def _advance(self, n: int = 1) -> None:
        for _ in range(n):
            if self.pos < len(self.source):
                if self.source[self.pos] == "\n":
                    self.line += 1
                    self.col = 1
                else:
                    self.col += 1
                self.pos += 1

    def _peek(self, offset: int = 0) -> str:
        i = self.pos + offset
        return self.source[i] if i < len(self.source) else ""

    def _eof(self) -> bool:
        return self.pos >= len(self.source)

    # -- token-level reading ------------------------------------------------

    def _skip_trivia(self) -> None:
        while not self._eof():
            c = self._peek()
            if c in _WHITESPACE:
                self._advance()
            elif c == ";":
                # line comment
                while not self._eof() and self._peek() != "\n":
                    self._advance()
            else:
                break

    def _read_string(self) -> Optional[String]:
        start_line, start_col = self.line, self.col
        self._advance()  # opening quote
        buf: List[str] = []
        while True:
            if self._eof():
                self._error("unterminated string literal", start_line, start_col)
                return None
            c = self._peek()
            if c == '"':
                self._advance()
                return String("".join(buf), Location(self.file, start_line, start_col))
            if c == "\\":
                self._advance()
                if self._eof():
                    self._error("unterminated escape sequence", start_line, start_col)
                    return None
                esc = self._peek()
                self._advance()
                simple = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "0": "\0"}
                if esc in simple:
                    buf.append(simple[esc])
                else:
                    self._error(f"invalid escape sequence '\\{esc}'", start_line, start_col)
                    return None
            else:
                buf.append(c)
                self._advance()

    def _read_number_or_symbol(self) -> Optional[Form]:
        start_line, start_col = self.line, self.col
        text = self._read_atom_text()
        if not text:
            return None
        # boolean / nil literals
        if text == "true":
            return Boolean(True, Location(self.file, start_line, start_col))
        if text == "false":
            return Boolean(False, Location(self.file, start_line, start_col))
        if text == "nil":
            return Nil(Location(self.file, start_line, start_col))
        # integer
        if _is_integer_literal(text):
            try:
                return Integer(int(text), Location(self.file, start_line, start_col))
            except ValueError:
                self._error(f"integer literal out of range: {text}", start_line, start_col)
                return None
        # float
        if _is_float_literal(text):
            try:
                return Float(float(text), Location(self.file, start_line, start_col))
            except ValueError:
                self._error(f"malformed float literal: {text}", start_line, start_col)
                return None
        return Symbol(text, Location(self.file, start_line, start_col))

    def _read_atom_text(self) -> str:
        start = self.pos
        while not self._eof():
            c = self._peek()
            if c in _WHITESPACE or c in _DELIMS or c == ";":
                break
            self._advance()
        return self.source[start:self.pos]

    def _read_list(self) -> Optional[ListForm]:
        start_line, start_col = self.line, self.col
        self._advance()  # '('
        items: List[Form] = []
        while True:
            self._skip_trivia()
            if self._eof():
                self._error("unexpected end of input: missing ')'", start_line, start_col)
                return None
            c = self._peek()
            if c == ")":
                self._advance()
                return ListForm(items, Location(self.file, start_line, start_col))
            form = self._read_form()
            if form is not None:
                items.append(form)

    def _read_form(self) -> Optional[Form]:
        self._skip_trivia()
        if self._eof():
            return None
        c = self._peek()
        if c == "(":
            return self._read_list()
        if c == ")":
            self._error("unexpected ')'")
            self._advance()
            return None
        if c == '"':
            return self._read_string()
        if c == ":":
            self._advance()
            name = self._read_atom_text()
            if not name:
                self._error("expected a keyword name after ':'")
                return None
            return Keyword(name, Location(self.file, self.line, self.col))
        return self._read_number_or_symbol()

    # -- public API ----------------------------------------------------------

    def read_all(self) -> List[Form]:
        """Read every top-level form.  Returns forms read before any fatal error."""
        forms: List[Form] = []
        while True:
            self._skip_trivia()
            if self._eof():
                break
            form = self._read_form()
            if form is not None:
                forms.append(form)
        return forms


def _is_integer_literal(text: str) -> bool:
    if not text:
        return False
    body = text[1:] if text[0] in "+-" else text
    return body.isdigit()


def _is_float_literal(text: str) -> bool:
    if not text or text in ("+", "-"):
        return False
    body = text[1:] if text[0] in "+-" else text
    if not any(ch in body for ch in ".eE"):
        return False
    try:
        float(text)
        return True
    except ValueError:
        return False


def read(source: str, file: str = "<string>") -> tuple[List[Form], DiagnosticSink]:
    """Convenience wrapper: read all forms and return them with the sink."""
    reader = Reader(source, file)
    forms = reader.read_all()
    return forms, reader.sink
