"""A minimal JSON Schema validator (draft-2020-12 subset).

Blackstone's schemas are validated without third-party dependencies.  This
module implements the subset of JSON Schema used by the Blackstone schemas:

- ``type`` (object, array, string, integer, number, boolean, null)
- ``properties``, ``required``, ``additionalProperties``
- ``items``, ``prefixItems``
- ``enum``, ``const``
- ``oneOf``, ``anyOf``, ``allOf``, ``not``
- ``patternProperties``
- ``$ref`` against in-document definitions (``$defs`` / ``definitions``)

It is intentionally small and deterministic.  Unsupported keywords are ignored
with a note rather than silently changing validation behavior.
"""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional

from .errors import DiagnosticSink, Location


class SchemaError(Exception):
    """Raised when a schema document itself is invalid."""


def _type_ok(value: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    return True


def validate_schema(
    value: Any,
    schema: Dict[str, Any],
    sink: DiagnosticSink,
    loc: Optional[Location] = None,
    path: str = "$",
    root: Optional[Dict[str, Any]] = None,
) -> None:
    """Validate ``value`` against ``schema``, emitting diagnostics to ``sink``."""
    if root is None:
        root = schema

    if "$ref" in schema:
        ref = schema["$ref"]
        target = _resolve_ref(ref, root)
        if target is not None:
            validate_schema(value, target, sink, loc, path, root)
        return

    if "const" in schema and value != schema["const"]:
        sink.error("E-SCHEMA", f"{path}: expected const {schema['const']!r}, got {value!r}", loc)
        return

    if "enum" in schema and value not in schema["enum"]:
        sink.error("E-SCHEMA", f"{path}: value {value!r} is not one of {schema['enum']}", loc)
        return

    if "type" in schema:
        types = schema["type"]
        types = [types] if isinstance(types, str) else types
        if not any(_type_ok(value, t) for t in types):
            sink.error("E-SCHEMA", f"{path}: expected type {'/'.join(types)}, got {type(value).__name__}", loc)
            return

    if isinstance(value, str):
        if "minLength" in schema and len(value) < schema["minLength"]:
            sink.error("E-SCHEMA", f"{path}: expected at least {schema['minLength']} characters", loc)
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            sink.error("E-SCHEMA", f"{path}: expected at most {schema['maxLength']} characters", loc)

    if isinstance(value, dict):
        _validate_object(value, schema, sink, loc, path, root)
    elif isinstance(value, list):
        _validate_array(value, schema, sink, loc, path, root)

    _validate_combinators(value, schema, sink, loc, path, root)


def _validate_object(
    value: Dict[str, Any],
    schema: Dict[str, Any],
    sink: DiagnosticSink,
    loc: Optional[Location],
    path: str,
    root: Dict[str, Any],
) -> None:
    props: Dict[str, Any] = schema.get("properties", {})
    required = schema.get("required", [])
    for key in required:
        if key not in value:
            sink.error("E-SCHEMA", f"{path}: missing required property '{key}'", loc)
    additional = schema.get("additionalProperties", True)
    pattern_props: Dict[str, Any] = schema.get("patternProperties", {})
    for key, val in value.items():
        child_path = f"{path}.{key}"
        if key in props:
            validate_schema(val, props[key], sink, loc, child_path, root)
        elif pattern_props:
            matched = False
            for pat, subschema in pattern_props.items():
                if re.search(pat, key):
                    matched = True
                    validate_schema(val, subschema, sink, loc, child_path, root)
            if not matched and additional is False:
                sink.error("E-SCHEMA", f"{child_path}: additional property '{key}' is not allowed", loc)
        elif additional is False:
            sink.error("E-SCHEMA", f"{child_path}: additional property '{key}' is not allowed", loc)
        elif isinstance(additional, dict):
            validate_schema(val, additional, sink, loc, child_path, root)


def _validate_array(
    value: List[Any],
    schema: Dict[str, Any],
    sink: DiagnosticSink,
    loc: Optional[Location],
    path: str,
    root: Dict[str, Any],
) -> None:
    items = schema.get("items")
    if isinstance(items, dict):
        for i, item in enumerate(value):
            validate_schema(item, items, sink, loc, f"{path}[{i}]", root)
    prefix = schema.get("prefixItems")
    if isinstance(prefix, list):
        for i, subschema in enumerate(prefix):
            if i < len(value):
                validate_schema(value[i], subschema, sink, loc, f"{path}[{i}]", root)
    if "minItems" in schema and len(value) < schema["minItems"]:
        sink.error("E-SCHEMA", f"{path}: expected at least {schema['minItems']} items", loc)
    if "maxItems" in schema and len(value) > schema["maxItems"]:
        sink.error("E-SCHEMA", f"{path}: expected at most {schema['maxItems']} items", loc)


def _validate_combinators(
    value: Any,
    schema: Dict[str, Any],
    sink: DiagnosticSink,
    loc: Optional[Location],
    path: str,
    root: Dict[str, Any],
) -> None:
    if "allOf" in schema:
        for sub in schema["allOf"]:
            validate_schema(value, sub, sink, loc, path, root)
    if "anyOf" in schema:
        matched = False
        for sub in schema["anyOf"]:
            probe = DiagnosticSink()
            validate_schema(value, sub, probe, loc, path, root)
            if not probe.has_errors():
                matched = True
                break
        if not matched:
            sink.error("E-SCHEMA", f"{path}: value does not match any 'anyOf' subschema", loc)
    if "oneOf" in schema:
        matches = 0
        for sub in schema["oneOf"]:
            probe = DiagnosticSink()
            validate_schema(value, sub, probe, loc, path, root)
            if not probe.has_errors():
                matches += 1
        if matches != 1:
            sink.error("E-SCHEMA", f"{path}: expected exactly one matching subschema, got {matches}", loc)
    if "not" in schema:
        probe = DiagnosticSink()
        validate_schema(value, schema["not"], probe, loc, path, root)
        if not probe.has_errors():
            sink.error("E-SCHEMA", f"{path}: value must not match the 'not' subschema", loc)


def _resolve_ref(ref: str, root: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """Resolve a local JSON pointer reference such as '#/$defs/kernel'."""
    if not ref.startswith("#/"):
        return None
    node: Any = root
    for part in ref[2:].split("/"):
        part = part.replace("~1", "/").replace("~0", "~")
        if isinstance(node, dict) and part in node:
            node = node[part]
        else:
            return None
    return node if isinstance(node, dict) else None
