"""Semantic analysis tests: parsed forms -> typed records."""

import unittest

from optilisp.errors import DiagnosticSink
from optilisp.semantic import STAGES, analyze_file
from optilisp.sexpr import read

KERNEL_SOURCE = """
(define-kernel
  :id "blackstone.test.example"
  :name "Example Kernel"
  :stage hir
  (category constant-evaluation)
  (paradigms rule-based)
  (objective "An example objective.")
  (requires
    (capability typed-expressions)
    (analysis constant-value-analysis))
  (preconditions
    (operation-has-defined-compile-time-semantics))
  (forbids (operation-has-side-effects))
  (transform
    (fold (add (integer a) (integer b)) (integer-result (integer-add a b))))
  (correctness (preserve-observable-behavior))
  (expected-effects "Folds a constant addition.")
  (limitations "Requires constant operands.")
  (documentation
    (purpose "Fold constant additions.")))
"""


class TestAnalyzeKernel(unittest.TestCase):
    def setUp(self):
        self.sink = DiagnosticSink()
        forms, read_sink = read(KERNEL_SOURCE, "test.olisp")
        self.sink.items.extend(read_sink.items)
        self.records = analyze_file(forms, "test.olisp", self.sink)

    def test_kernel_extracted(self):
        self.assertEqual(len(self.records["kernels"]), 1)
        k = self.records["kernels"][0]
        self.assertEqual(k.id, "blackstone.test.example")
        self.assertEqual(k.name, "Example Kernel")
        self.assertEqual(k.stage, "hir")

    def test_categories_and_paradigms(self):
        k = self.records["kernels"][0]
        self.assertEqual(k.categories, ["constant-evaluation"])
        self.assertEqual(k.paradigms, ["rule-based"])

    def test_requires(self):
        k = self.records["kernels"][0]
        self.assertEqual(len(k.requires), 2)
        self.assertEqual(k.requires[0].kind, "capability")
        self.assertEqual(k.requires[0].name, "typed-expressions")
        self.assertEqual(k.requires[1].kind, "analysis")
        self.assertEqual(k.requires[1].name, "constant-value-analysis")

    def test_transform(self):
        k = self.records["kernels"][0]
        self.assertIsNotNone(k.transform)
        self.assertEqual(k.transform.verb, "fold")
        self.assertEqual(len(k.transform.args), 2)

    def test_documentation(self):
        k = self.records["kernels"][0]
        self.assertIsNotNone(k.documentation)
        self.assertIn("purpose", k.documentation.structured)

    def test_no_errors(self):
        self.assertFalse(self.sink.has_errors())


class TestAnalyzeRegistry(unittest.TestCase):
    def test_capability(self):
        source = '(define-capability :id "typed-expressions" :name "Typed" :stages (hir))'
        forms, rs = read(source)
        records = analyze_file(forms, "cap.olisp", DiagnosticSink())
        self.assertEqual(len(records["capabilities"]), 1)
        self.assertEqual(records["capabilities"][0].id, "typed-expressions")
        self.assertEqual(records["capabilities"][0].stages, ["hir"])

    def test_analysis(self):
        source = '(define-analysis :id "liveness-analysis" :name "Liveness")'
        forms, rs = read(source)
        records = analyze_file(forms, "an.olisp", DiagnosticSink())
        self.assertEqual(len(records["analyses"]), 1)
        self.assertEqual(records["analyses"][0].id, "liveness-analysis")

    def test_monoid(self):
        source = """
        (define-monoid
          :id "blackstone.monoid.integer-addition"
          :name "Integer Addition"
          :carrier integer
          :operation integer-add
          :identity 0
          (laws (associative integer-add)))
        """
        forms, rs = read(source)
        records = analyze_file(forms, "m.olisp", DiagnosticSink())
        self.assertEqual(len(records["monoids"]), 1)
        m = records["monoids"][0]
        self.assertEqual(m.carrier, "integer")
        self.assertEqual(m.operation, "integer-add")
        self.assertEqual(m.identity, "0")
        self.assertIn("associative integer-add", m.laws)


class TestUnknownForm(unittest.TestCase):
    def test_unknown_top_level_form(self):
        forms, _ = read("(define-unknown :id \"x\")")
        sink = DiagnosticSink()
        analyze_file(forms, "bad.olisp", sink)
        self.assertTrue(sink.has_errors())
        self.assertIn("unknown top-level form", sink.errors[0].message)


class TestStages(unittest.TestCase):
    def test_stages_defined(self):
        self.assertEqual(set(STAGES), {"hir", "mir", "lir"})


if __name__ == "__main__":
    unittest.main()
