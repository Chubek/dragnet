"""Layer 1 (syntax) tests for the Optilisp reader."""

import unittest

from optilisp.errors import SEVERITY_ERROR
from optilisp.sexpr import (
    Boolean,
    Float,
    Integer,
    Keyword,
    ListForm,
    Nil,
    String,
    Symbol,
    read,
)


class TestReaderAtoms(unittest.TestCase):
    def test_symbol(self):
        forms, sink = read("integer-add")
        self.assertEqual(len(forms), 1)
        self.assertIsInstance(forms[0], Symbol)
        self.assertEqual(forms[0].name, "integer-add")
        self.assertFalse(sink.has_errors())

    def test_dotted_symbol(self):
        forms, _ = read("blackstone.hir.constant-folding.integer-add")
        self.assertEqual(forms[0].name, "blackstone.hir.constant-folding.integer-add")

    def test_keyword(self):
        forms, _ = read(":id")
        self.assertIsInstance(forms[0], Keyword)
        self.assertEqual(forms[0].name, "id")

    def test_string(self):
        forms, _ = read('"hello world"')
        self.assertIsInstance(forms[0], String)
        self.assertEqual(forms[0].value, "hello world")

    def test_string_escapes(self):
        forms, _ = read('"a\\"b\\\\c\\n"')
        self.assertEqual(forms[0].value, 'a"b\\c\n')

    def test_integer(self):
        forms, _ = read("42")
        self.assertIsInstance(forms[0], Integer)
        self.assertEqual(forms[0].value, 42)

    def test_negative_integer(self):
        forms, _ = read("-7")
        self.assertEqual(forms[0].value, -7)

    def test_float(self):
        forms, _ = read("3.14")
        self.assertIsInstance(forms[0], Float)
        self.assertAlmostEqual(forms[0].value, 3.14)

    def test_boolean(self):
        forms, _ = read("true false")
        self.assertIsInstance(forms[0], Boolean)
        self.assertTrue(forms[0].value)
        self.assertIsInstance(forms[1], Boolean)
        self.assertFalse(forms[1].value)

    def test_nil(self):
        forms, _ = read("nil")
        self.assertIsInstance(forms[0], Nil)


class TestReaderLists(unittest.TestCase):
    def test_empty_list(self):
        forms, _ = read("()")
        self.assertIsInstance(forms[0], ListForm)
        self.assertEqual(len(forms[0].items), 0)

    def test_nested_list(self):
        forms, _ = read("(add x (integer 0))")
        self.assertIsInstance(forms[0], ListForm)
        self.assertEqual(len(forms[0].items), 3)
        self.assertIsInstance(forms[0].items[2], ListForm)

    def test_multiple_forms(self):
        forms, _ = read("a b c")
        self.assertEqual(len(forms), 3)

    def test_comment(self):
        forms, _ = read("; this is a comment\nfoo")
        self.assertEqual(len(forms), 1)
        self.assertEqual(forms[0].name, "foo")


class TestReaderErrors(unittest.TestCase):
    def test_unclosed_list(self):
        forms, sink = read("(add x")
        self.assertTrue(sink.has_errors())
        self.assertIn("missing ')'", sink.errors[0].message)

    def test_unexpected_close(self):
        _, sink = read(")")
        self.assertTrue(sink.has_errors())

    def test_unterminated_string(self):
        _, sink = read('"unterminated')
        self.assertTrue(sink.has_errors())
        self.assertIn("unterminated string", sink.errors[0].message)

    def test_invalid_escape(self):
        _, sink = read('"bad\\qescape"')
        self.assertTrue(sink.has_errors())
        self.assertIn("invalid escape", sink.errors[0].message)

    def test_location_tracking(self):
        source = "\n\n  (foo)"
        forms, _ = read(source, "test.olisp")
        self.assertEqual(forms[0].loc.line, 3)
        self.assertEqual(forms[0].loc.column, 3)
        self.assertEqual(forms[0].loc.file, "test.olisp")


if __name__ == "__main__":
    unittest.main()
