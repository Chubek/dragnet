"""JSON Schema lite validator tests."""

import unittest

from optilisp.errors import DiagnosticSink
from optilisp.jsonschema_lite import validate_schema


class TestTypeValidation(unittest.TestCase):
    def test_string_type(self):
        sink = DiagnosticSink()
        validate_schema("hello", {"type": "string"}, sink)
        self.assertFalse(sink.has_errors())

    def test_integer_type(self):
        sink = DiagnosticSink()
        validate_schema(42, {"type": "integer"}, sink)
        self.assertFalse(sink.has_errors())

    def test_type_mismatch(self):
        sink = DiagnosticSink()
        validate_schema("not an int", {"type": "integer"}, sink)
        self.assertTrue(sink.has_errors())

    def test_boolean_not_integer(self):
        sink = DiagnosticSink()
        validate_schema(True, {"type": "integer"}, sink)
        self.assertTrue(sink.has_errors())


class TestObjectValidation(unittest.TestCase):
    def test_required_properties(self):
        schema = {
            "type": "object",
            "required": ["id", "name"],
            "properties": {
                "id": {"type": "string"},
                "name": {"type": "string"},
            },
        }
        sink = DiagnosticSink()
        validate_schema({"id": "x"}, schema, sink)
        self.assertTrue(sink.has_errors())
        self.assertIn("missing required property 'name'", sink.errors[0].message)

    def test_additional_properties_rejected(self):
        schema = {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "additionalProperties": False,
        }
        sink = DiagnosticSink()
        validate_schema({"id": "x", "extra": 1}, schema, sink)
        self.assertTrue(sink.has_errors())

    def test_nested_object(self):
        schema = {
            "type": "object",
            "properties": {
                "transform": {
                    "type": "object",
                    "required": ["verb"],
                    "properties": {"verb": {"type": "string"}},
                }
            },
        }
        sink = DiagnosticSink()
        validate_schema({"transform": {}}, schema, sink)
        self.assertTrue(sink.has_errors())


class TestEnumAndConst(unittest.TestCase):
    def test_enum_valid(self):
        sink = DiagnosticSink()
        validate_schema("hir", {"enum": ["hir", "mir", "lir"]}, sink)
        self.assertFalse(sink.has_errors())

    def test_enum_invalid(self):
        sink = DiagnosticSink()
        validate_schema("bad", {"enum": ["hir", "mir", "lir"]}, sink)
        self.assertTrue(sink.has_errors())

    def test_const(self):
        sink = DiagnosticSink()
        validate_schema(42, {"const": 42}, sink)
        self.assertFalse(sink.has_errors())
        sink2 = DiagnosticSink()
        validate_schema(43, {"const": 42}, sink2)
        self.assertTrue(sink2.has_errors())


class TestArrayValidation(unittest.TestCase):
    def test_items(self):
        schema = {"type": "array", "items": {"type": "string"}}
        sink = DiagnosticSink()
        validate_schema(["a", "b"], schema, sink)
        self.assertFalse(sink.has_errors())
        sink2 = DiagnosticSink()
        validate_schema(["a", 1], schema, sink2)
        self.assertTrue(sink2.has_errors())

    def test_min_items(self):
        schema = {"type": "array", "minItems": 2, "items": {"type": "string"}}
        sink = DiagnosticSink()
        validate_schema(["a"], schema, sink)
        self.assertTrue(sink.has_errors())


class TestCombinators(unittest.TestCase):
    def test_one_of(self):
        schema = {"oneOf": [{"type": "string"}, {"type": "integer"}]}
        sink = DiagnosticSink()
        validate_schema("hello", schema, sink)
        self.assertFalse(sink.has_errors())
        sink2 = DiagnosticSink()
        validate_schema(True, schema, sink2)
        self.assertTrue(sink2.has_errors())

    def test_any_of(self):
        schema = {"anyOf": [{"type": "string"}, {"type": "integer"}]}
        sink = DiagnosticSink()
        validate_schema(42, schema, sink)
        self.assertFalse(sink.has_errors())

    def test_all_of(self):
        schema = {
            "allOf": [
                {"type": "object", "required": ["id"]},
                {"type": "object", "required": ["name"]},
            ]
        }
        sink = DiagnosticSink()
        validate_schema({"id": "x", "name": "y"}, schema, sink)
        self.assertFalse(sink.has_errors())
        sink2 = DiagnosticSink()
        validate_schema({"id": "x"}, schema, sink2)
        self.assertTrue(sink2.has_errors())


class TestRef(unittest.TestCase):
    def test_local_ref(self):
        schema = {
            "$defs": {"stage": {"enum": ["hir", "mir", "lir"]}},
            "$ref": "#/$defs/stage",
        }
        sink = DiagnosticSink()
        validate_schema("hir", schema, sink)
        self.assertFalse(sink.has_errors())
        sink2 = DiagnosticSink()
        validate_schema("bad", schema, sink2)
        self.assertTrue(sink2.has_errors())


if __name__ == "__main__":
    unittest.main()
