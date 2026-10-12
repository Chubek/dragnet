"""Layered validation tests (spec section 17)."""

import unittest
from pathlib import Path

from optilisp.errors import DiagnosticSink
from optilisp.semantic import analyze_file
from optilisp.sexpr import read
from optilisp.validate import (
    load_database,
    load_registry,
    validate_semantic,
    validate_structure,
    validate_transformation,
    validate_unique_ids,
    kernel_to_dict,
    validate_kernel_against_schema,
)

FIXTURES = Path(__file__).resolve().parents[1] / "fixtures"
ROOT = Path(__file__).resolve().parents[2]


def _load_fixture(name: str):
    source = (FIXTURES / name).read_text(encoding="utf-8")
    sink = DiagnosticSink()
    forms, read_sink = read(source, name)
    sink.items.extend(read_sink.items)
    records = analyze_file(forms, name, sink)
    return records["kernels"][0], sink


class TestValidKernel(unittest.TestCase):
    def test_valid_kernel_passes_structure(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        sink = DiagnosticSink()
        validate_structure(k, None, sink)
        self.assertFalse(sink.has_errors(), sink.render())

    def test_valid_kernel_passes_references(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        reg = load_registry(ROOT / "optilisp" / "stdlib", DiagnosticSink())
        sink = DiagnosticSink()
        # The fixture kernel references capabilities that exist in the real registry.
        from optilisp.validate import validate_references
        validate_references(k, reg, {k.id}, sink)
        self.assertFalse(sink.has_errors(), sink.render())

    def test_valid_kernel_passes_schema(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        sink = DiagnosticSink()
        validate_kernel_against_schema(k, sink)
        self.assertFalse(sink.has_errors(), sink.render())


class TestStructuralLayer(unittest.TestCase):
    def test_missing_stage_fails(self):
        k, _ = _load_fixture("missing-stage.olisp")
        sink = DiagnosticSink()
        validate_structure(k, None, sink)
        self.assertTrue(sink.has_errors())
        self.assertTrue(any("stage" in d.message.lower() for d in sink.errors))


class TestReferenceLayer(unittest.TestCase):
    def test_unknown_capability_fails(self):
        k, _ = _load_fixture("unknown-capability.olisp")
        reg = load_registry(ROOT / "optilisp" / "stdlib", DiagnosticSink())
        sink = DiagnosticSink()
        from optilisp.validate import validate_references
        validate_references(k, reg, {k.id}, sink)
        self.assertTrue(sink.has_errors())
        self.assertTrue(any("unresolved capability" in d.message for d in sink.errors))


class TestSemanticLayer(unittest.TestCase):
    def test_stage_verb_mismatch_fails(self):
        k, _ = _load_fixture("stage-verb-mismatch.olisp")
        reg = load_registry(ROOT / "optilisp" / "stdlib", DiagnosticSink())
        sink = DiagnosticSink()
        validate_semantic(k, reg, sink)
        self.assertTrue(sink.has_errors())
        self.assertTrue(any("not valid at stage" in d.message for d in sink.errors))

    def test_contradictory_contract_fails(self):
        k, _ = _load_fixture("contradictory-contract.olisp")
        reg = load_registry(ROOT / "optilisp" / "stdlib", DiagnosticSink())
        sink = DiagnosticSink()
        validate_semantic(k, reg, sink)
        self.assertTrue(sink.has_errors())
        self.assertTrue(any("both invalidates and preserves" in d.message for d in sink.errors))


class TestTransformationLayer(unittest.TestCase):
    def test_valid_transform_passes(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        sink = DiagnosticSink()
        validate_transformation(k, sink)
        self.assertFalse(sink.has_errors(), sink.render())


class TestDatabaseLayer(unittest.TestCase):
    def test_duplicate_ids_fail(self):
        # Build a minimal database from the two duplicate fixtures.
        reg = load_registry(ROOT / "optilisp" / "stdlib", DiagnosticSink())
        db = load_database(ROOT, reg, DiagnosticSink())
        # Inject the duplicate kernels directly.
        ka, _ = _load_fixture("duplicate-id-a.olisp")
        kb, _ = _load_fixture("duplicate-id-b.olisp")
        db.kernels.extend([ka, kb])
        sink = DiagnosticSink()
        validate_unique_ids(db, sink)
        self.assertTrue(sink.has_errors())
        self.assertTrue(any("duplicate kernel id" in d.message for d in sink.errors))


class TestKernelToDict(unittest.TestCase):
    def test_round_trip_structure(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        d = kernel_to_dict(k)
        self.assertEqual(d["id"], "blackstone.test.valid")
        self.assertEqual(d["stage"], "hir")
        self.assertEqual(d["transform"]["verb"], "fold")
        self.assertGreaterEqual(len(d["requires"]), 1)


class TestSchemaValidation(unittest.TestCase):
    def test_schema_rejects_bad_stage(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        k.stage = "invalid-stage"
        sink = DiagnosticSink()
        validate_kernel_against_schema(k, sink)
        self.assertTrue(sink.has_errors())

    def test_schema_rejects_missing_required(self):
        k, _ = _load_fixture("valid-kernel.olisp")
        k.objective = ""
        sink = DiagnosticSink()
        validate_kernel_against_schema(k, sink)
        self.assertTrue(sink.has_errors())


if __name__ == "__main__":
    unittest.main()
