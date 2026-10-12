import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest

from tools.blackstone.author import ROWS, render
from tools.blackstone.bridge import ROOT, UPSTREAM
from tools.blackstone.catalog import (
    Catalog, Fact, SUPPLEMENT, applicability, encode, fields, invalidate,
    load, parse, profitability, references, validate_kernel,
)
from tools.blackstone.__main__ import main


def inspect(source):
    catalog = Catalog()
    forms = parse(source, "fixture.olisp", catalog)
    for form in forms:
        validate_kernel(form, catalog, "fixture.olisp")
    return catalog


def passing_facts(kernel):
    facts = {f"{kind}:{value}": Fact.TRUE for kind, value in kernel["requirements"]}
    facts.update({"condition:" + value: Fact.TRUE for value in kernel["preconditions"]})
    facts.update({"condition:" + value: Fact.FALSE for value in kernel["forbids"]})
    facts.update({"history:" + value: Fact.TRUE for value in kernel["prerequisites"]})
    facts.update({"history:" + value: Fact.FALSE for value in kernel["forbidden_prior"]})
    return facts


class StructuralRegressionTests(unittest.TestCase):
    def setUp(self):
        self.valid = render(ROWS[0])

    def reject(self, source, code):
        result = inspect(source)
        self.assertIn(code, {i.code for i in result.issues}, [i.render() for i in result.issues])

    def test_duplicate_keywords_do_not_overwrite_silently(self):
        self.reject(self.valid.replace(':name "And all ones"', ':id "blackstone.hir.bitwise.other"\n  :name "And all ones"'), "E-DUPLICATE")

    def test_duplicate_singleton_sections(self):
        self.reject(self.valid.replace("  (category", "  (category ignored)\n  (category"), "E-DUPLICATE")

    def test_unknown_keyword_and_section(self):
        for replacement in (":typo 7\n  :stage", ":stage"):
            source = self.valid.replace(":stage", replacement, 1)
            if replacement == ":stage":
                source = source.replace("(category", "(typo", 1)
            with self.subTest(replacement=replacement):
                self.reject(source, "E-FIELD")

    def test_trailing_body_atom_not_ignored(self):
        self.reject(self.valid.replace("  (transform", "  123\n  (transform"), "E-FIELD")

    def test_late_keyword_not_ignored(self):
        self.reject(self.valid.replace("  (transform", "  :stage lir\n  (transform"), "E-FIELD")

    def test_malformed_requirement_and_extra_argument(self):
        for requirement in ("(capability)", "(capability typed-expressions extra)", "(unknown typed-expressions)", "123"):
            with self.subTest(requirement=requirement):
                self.reject(self.valid.replace("(capability typed-expressions)", requirement), "E-REQUIREMENT")

    def test_multiple_transformations(self):
        self.reject(self.valid.replace("(transform (rewrite (and x mask) x))", "(transform (rewrite (and x mask) x) (eliminate x))"), "E-TRANSFORM")

    def test_missing_or_empty_objective(self):
        self.reject(self.valid.replace('(objective "Replace (and x mask) with x when the declared proof obligations hold.")', '(objective "")'), "E-TYPE")

    def test_non_string_metadata(self):
        self.reject(self.valid.replace('(limitations "', '(limitations 123 "', 1), "E-TYPE")

    def test_invalid_stage_and_id_disagreement(self):
        self.reject(self.valid.replace(":stage hir", ":stage lir"), "E-STAGE")

    def test_stage_incompatible_transform(self):
        self.reject(self.valid.replace("(rewrite (and x mask) x)", "(select (and x mask) x)"), "E-STAGE")

    def test_precondition_forbidden_contradiction(self):
        self.reject(self.valid.replace("(forbids ", "(forbids operand-values-defined "), "E-CONTRADICTION")

    def test_preserved_and_invalidated_analysis(self):
        self.reject(self.valid.replace("(preserves)", "(preserves range-analysis)"), "E-CONTRADICTION")

    def test_consumed_analysis_contract_matches_requirements(self):
        self.reject(self.valid.replace("    (requires )", "    (requires range-analysis)"), "E-CONTRACT")

    def test_empty_documentation(self):
        self.reject(self.valid.replace('(purpose "', '(purpose "" "', 1), "E-DOC")

    def test_duplicate_documentation(self):
        self.reject(self.valid.replace("  (documentation", '  (documentation (purpose "duplicate")'), "E-DUPLICATE")

    def test_invalid_kernel_id(self):
        self.reject(self.valid.replace("blackstone.hir.bitwise.and-all-ones", "bad id"), "E-ID")

    def test_rewrite_arity(self):
        self.reject(self.valid.replace("(rewrite (and x mask) x)", "(rewrite (and x mask))"), "E-TRANSFORM")

    def test_source_locations_retained(self):
        result = inspect(self.valid.replace("  (category", "  (category ignored)\n  (category"))
        issue = next(i for i in result.issues if i.code == "E-DUPLICATE")
        self.assertGreater(issue.line, 1)
        self.assertEqual(issue.column, 3)

    def test_falsey_sink_regression_issues_visible(self):
        result = inspect(self.valid.replace("(category", "(misspelled", 1))
        self.assertGreater(len(result.issues), 0)


class ReaderBoundaryTests(unittest.TestCase):
    def test_invalid_syntax(self):
        for source in ('(', ')', '"unterminated', '"invalid\\q"'):
            with self.subTest(source=source):
                catalog = Catalog()
                self.assertEqual(parse(source, "fixture", catalog), [])
                self.assertTrue(catalog.issues)

    def test_bounded_depth_and_size(self):
        for source in ("(" * 129 + ")" * 129, "x" * 1_048_577):
            catalog = Catalog()
            self.assertEqual(parse(source, "fixture", catalog), [])
            self.assertEqual(catalog.issues[0].code, "E-LIMIT")

    def test_comments_and_strings_do_not_count_as_nesting(self):
        catalog = Catalog()
        self.assertTrue(parse('; (((\n"(((("', "fixture", catalog))
        self.assertFalse(catalog.issues)

    def test_nonfinite_float_rejected(self):
        catalog = Catalog()
        parse("1e999", "fixture", catalog)
        self.assertEqual(catalog.issues[0].code, "E-NUMBER")

    def test_chinese_scan(self):
        catalog = Catalog()
        parse('"' + chr(0x4E2D) + '"', "fixture", catalog)
        self.assertEqual(catalog.issues[0].code, "E-LANGUAGE")

    def test_string_fingerprints_preserve_escape_boundaries(self):
        catalog = Catalog()
        forms = parse('("a\\n") ("a" "n")', "fixture", catalog)
        self.assertNotEqual(encode(forms[0]), encode(forms[1]))


class CatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load()

    def test_fifty_supplemental_kernels_valid(self):
        self.assertEqual(len(self.catalog.kernels), 50)
        self.assertFalse(self.catalog.issues, [i.render() for i in self.catalog.issues])

    def test_canonical_files_match_authoring_table(self):
        paths = set()
        for row in ROWS:
            stage, domain, operation = row[:3]
            path = SUPPLEMENT / "kernels" / stage / domain / (operation + ".olisp")
            self.assertEqual(path.read_text(encoding="utf-8"), render(row))
            paths.add(path)
        self.assertEqual(paths, set((SUPPLEMENT / "kernels").rglob("*.olisp")))

    def test_two_hundred_records_audited_without_false_success(self):
        catalog = load("all")
        self.assertEqual(len(catalog.kernels), 200)
        self.assertTrue(catalog.issues)
        self.assertTrue(all("libstone/blackstone/kernels/" in i.file for i in catalog.issues))

    def test_unknown_reference(self):
        catalog = copy.deepcopy(self.catalog)
        next(iter(catalog.kernels.values()))["requirements"].append(("analysis", "missing"))
        references(catalog)
        self.assertIn("E-REFERENCE", {i.code for i in catalog.issues})

    def test_capability_stage_mismatch(self):
        catalog = copy.deepcopy(self.catalog)
        next(iter(catalog.kernels.values()))["requirements"].append(("capability", "machine-operations"))
        references(catalog)
        self.assertIn("E-STAGE", {i.code for i in catalog.issues})

    def test_cycles_and_self_dependency(self):
        for self_cycle in (False, True):
            catalog = copy.deepcopy(self.catalog)
            a, b = list(catalog.kernels)[:2]
            catalog.kernels[a]["prerequisites"] = [a if self_cycle else b]
            catalog.kernels[b]["prerequisites"] = [a]
            references(catalog)
            self.assertIn("E-CYCLE", {i.code for i in catalog.issues})

    def test_long_acyclic_dependency_graph(self):
        kernel = copy.deepcopy(next(iter(self.catalog.kernels.values())))
        catalog = Catalog(capabilities=self.catalog.capabilities, analyses=self.catalog.analyses)
        for i in range(1500):
            record = copy.deepcopy(kernel)
            record["prerequisites"] = [str(i + 1)] if i < 1499 else []
            record["preconditions"].append(str(i))
            catalog.kernels[str(i)] = record
        references(catalog)
        self.assertFalse(catalog.issues)

    def test_duplicate_transform_contracts(self):
        catalog = copy.deepcopy(self.catalog)
        record = copy.deepcopy(next(iter(catalog.kernels.values())))
        record["id"] = "blackstone.hir.bitwise.duplicate"
        catalog.kernels[record["id"]] = record
        references(catalog)
        self.assertIn("E-REDUNDANCY", {i.code for i in catalog.issues})

    def test_duplicate_global_kernel_id(self):
        result = inspect(render(ROWS[0]) + render(ROWS[0]))
        self.assertIn("E-DUPLICATE", {i.code for i in result.issues})

    def test_loading_does_not_write_upstream(self):
        def digest():
            return {str(p.relative_to(UPSTREAM)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in UPSTREAM.rglob("*") if p.is_file()}
        before = digest()
        load("all")
        self.assertEqual(before, digest())

    def test_source_and_docs_english_only(self):
        import re
        for path in SUPPLEMENT.rglob("*"):
            if path.is_file() and path.suffix in {".py", ".md", ".olisp", ".json"}:
                self.assertIsNone(re.search(r"[\u3400-\u9fff\uf900-\ufaff]", path.read_text(encoding="utf-8")), str(path))


class ApplicabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load()

    def test_positive_negative_edge_and_unknown_for_every_kernel(self):
        for kid, kernel in self.catalog.kernels.items():
            with self.subTest(kernel=kid):
                facts = passing_facts(kernel)
                self.assertEqual(applicability(kernel, kernel["stage"], facts), (True, []))
                # Every required fact independently blocks applicability when missing,
                # false, unknown, not computed, or invalidated.
                positives = [key for key, value in facts.items() if value == Fact.TRUE]
                negatives = [key for key, value in facts.items() if value == Fact.FALSE]
                for key in positives:
                    for state in (Fact.FALSE, Fact.UNKNOWN, Fact.NOT_COMPUTED, Fact.INVALIDATED):
                        changed = {**facts, key: state}
                        self.assertFalse(applicability(kernel, kernel["stage"], changed)[0], (kid, key, state))
                    changed = dict(facts)
                    del changed[key]
                    self.assertFalse(applicability(kernel, kernel["stage"], changed)[0])
                for key in negatives:
                    for state in (Fact.TRUE, Fact.UNKNOWN, Fact.NOT_COMPUTED, Fact.INVALIDATED):
                        self.assertFalse(applicability(kernel, kernel["stage"], {**facts, key: state})[0])
                self.assertFalse(applicability(kernel, "invalid-stage", facts)[0])
                self.assertFalse(applicability(kernel, kernel["stage"], {})[0])

    def test_history_is_checked_separately_from_current_properties(self):
        kernel = copy.deepcopy(next(iter(self.catalog.kernels.values())))
        kernel["prerequisites"] = ["previous-transform"]
        kernel["forbidden_prior"] = ["forbidden-transform"]
        facts = passing_facts(kernel)
        self.assertTrue(applicability(kernel, kernel["stage"], facts)[0])
        del facts["history:previous-transform"]
        self.assertFalse(applicability(kernel, kernel["stage"], facts)[0])

    def test_analysis_invalidation_is_conservative_and_nonmutating(self):
        kernel = next(iter(self.catalog.kernels.values()))
        original = {"analysis:range-analysis": Fact.TRUE, "analysis:custom-analysis": Fact.TRUE,
                    "condition:operand-values-defined": Fact.TRUE}
        result = invalidate(kernel, original)
        self.assertEqual(result["analysis:range-analysis"], Fact.INVALIDATED)
        self.assertEqual(result["analysis:custom-analysis"], Fact.INVALIDATED)
        self.assertEqual(original["analysis:range-analysis"], Fact.TRUE)
        self.assertEqual(result["condition:operand-values-defined"], Fact.TRUE)

    def test_explicit_analysis_preservation(self):
        kernel = copy.deepcopy(next(iter(self.catalog.kernels.values())))
        kernel["contract"]["preserves"] = ["dominator-analysis"]
        result = invalidate(kernel, {"analysis:dominator-analysis": Fact.TRUE})
        self.assertEqual(result["analysis:dominator-analysis"], Fact.TRUE)

    def test_profitability_is_separate_from_correctness(self):
        kernel = next(k for k in self.catalog.kernels.values() if k["stage"] == "lir")
        facts = passing_facts(kernel)
        self.assertTrue(applicability(kernel, "lir", facts)[0])
        self.assertFalse(profitability(kernel, facts)[0])
        facts.update({"capability:instruction-cost-model": Fact.TRUE,
                      "condition:replacement-profitable-under-target-cost-model": Fact.TRUE})
        self.assertTrue(profitability(kernel, facts)[0])


class CliTests(unittest.TestCase):
    def invoke(self, arguments):
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            status = main(arguments)
        return status, output.getvalue(), errors.getvalue()

    def test_validate_and_query(self):
        self.assertEqual(self.invoke(["validate"])[0], 0)
        status, out, err = self.invoke(["query", "--stage", "lir", "--json"])
        self.assertEqual(status, 0)
        self.assertEqual(len(json.loads(out)), 10)
        self.assertFalse(err)

    def test_all_audit_fails_and_query_refuses_invalid_catalog(self):
        status, out, _ = self.invoke(["audit", "--json"])
        self.assertEqual(status, 1)
        self.assertEqual(json.loads(out)["kernels"], 200)
        self.assertEqual(self.invoke(["query", "--scope", "all"])[0], 1)

    def test_deterministic_output(self):
        self.assertEqual(self.invoke(["audit", "--json"]), self.invoke(["audit", "--json"]))

    def test_help_version_and_usage(self):
        for flag, expected in (("--help", 0), ("--version", 0), ("--unknown", 2)):
            completed = subprocess.run([sys.executable, "-B", "-m", "tools.blackstone", flag],
                                       cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(completed.returncode, expected, completed.stderr)


if __name__ == "__main__":
    unittest.main()
