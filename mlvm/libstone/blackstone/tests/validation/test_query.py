"""Database query and discovery tests (spec section 19)."""

import unittest
from pathlib import Path

from optilisp.errors import DiagnosticSink
from optilisp.query import Query, build_index, run_query, summarize
from optilisp.validate import load_database, load_registry

ROOT = Path(__file__).resolve().parents[2]


class TestQuery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sink = DiagnosticSink()
        reg = load_registry(ROOT / "optilisp" / "stdlib", sink)
        cls.db = load_database(ROOT, reg, sink)

    def test_query_by_stage(self):
        q = Query(stage="hir")
        results = run_query(self.db, q)
        self.assertTrue(all(k.stage == "hir" for k in results))

    def test_query_by_category(self):
        q = Query(category="constant-evaluation")
        results = run_query(self.db, q)
        self.assertTrue(all("constant-evaluation" in k.categories for k in results))
        self.assertGreater(len(results), 0)

    def test_query_by_capability(self):
        q = Query(requires_capability="typed-expressions")
        results = run_query(self.db, q)
        self.assertTrue(all(
            any(r.kind == "capability" and r.name == "typed-expressions" for r in k.requires)
            for k in results
        ))

    def test_query_by_analysis(self):
        q = Query(requires_analysis="liveness-analysis")
        results = run_query(self.db, q)
        self.assertTrue(all(
            any(r.kind == "analysis" and r.name == "liveness-analysis" for r in k.requires)
            for k in results
        ))

    def test_query_by_verb(self):
        q = Query(verb="fold")
        results = run_query(self.db, q)
        self.assertTrue(all(k.transform is not None and k.transform.verb == "fold" for k in results))

    def test_query_by_text(self):
        q = Query(text="constant folding")
        results = run_query(self.db, q)
        self.assertGreater(len(results), 0)

    def test_query_combined(self):
        q = Query(stage="hir", category="algebraic-simplification")
        results = run_query(self.db, q)
        self.assertTrue(all(k.stage == "hir" for k in results))
        self.assertTrue(all("algebraic-simplification" in k.categories for k in results))

    def test_query_sorted_by_id(self):
        results = run_query(self.db, Query())
        ids = [k.id for k in results]
        self.assertEqual(ids, sorted(ids))

    def test_build_index(self):
        index = build_index(self.db)
        self.assertIn("hir", index)
        self.assertIn("mir", index)
        self.assertIn("lir", index)

    def test_summarize(self):
        s = summarize(self.db)
        self.assertGreater(s["total_kernels"], 0)
        self.assertEqual(
            s["total_kernels"],
            sum(s["stages"].values()),
        )


if __name__ == "__main__":
    unittest.main()
