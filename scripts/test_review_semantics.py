"""Synthetic unit fixtures only: these are NOT human-review measurements."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

import review_semantics as review


class SemanticReviewTests(unittest.TestCase):
    def setUp(self):
        build = review.ROOT / "build"
        build.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="semantic-unit-", dir=build)
        self.output = Path(self.temporary.name).resolve()
        self.assertTrue(self.output.is_relative_to(build.resolve()))
        self.addCleanup(self.temporary.cleanup)
        review.prepare(self.output)
        self.a = self.output / "reviewer_a.json"
        self.b = self.output / "reviewer_b.json"
        self.key_path = self.output / "coordinator_only/reference_key.json"
        self.key = review.read(self.key_path)
        self.result = self.output / "SYNTHETIC_UNIT_RESULT_NOT_RESEARCH.json"

    def write(self, path, value):
        path.write_text(review.encoded(value), encoding="utf-8")

    def synthetic_forms(self, decision=None):
        """Exercise scoring arithmetic in isolated temporary files, never paper data."""
        references = {row["candidate_id"]: row["expected_decision"] for row in self.key["answers"]}
        forms = []
        for role, path in (("A", self.a), ("B", self.b)):
            form = review.read(path)
            form["reviewer"].update(
                reviewer_id="SYNTHETIC_UNIT_FIXTURE_NOT_A_PERSON_" + role,
                process_control_experience="SYNTHETIC TEST ONLY",
                is_real_human=True, independent_review=True,
                saw_reference_key=False, discussed_decisions_before_submission=False,
                involved_in_generation_or_repair=False)
            for row in form["reviews"]:
                row.update(decision=decision or references[row["candidate_id"]],
                           explanation="SYNTHETIC UNIT FIXTURE; not a human judgment.",
                           evidence=["Synthetic arithmetic test only."],
                           reviewed_at_utc="2026-10-03T00:00:00Z", elapsed_seconds=60)
            self.write(path, form)
            forms.append(form)
        return forms

    def test_packet_has_26_blind_candidates_and_blank_forms(self):
        candidates = list((self.output / "reviewer_packet/candidates").glob("*.json"))
        self.assertEqual(len(candidates), 26)
        for path in candidates:
            value = review.read(path)
            self.assertNotIn("expected_decision", value)
            self.assertNotIn("source_case_ids", value)
            self.assertNotIn("reference_repair", value)
            for support in value["supporting_definitions"]:
                name = support["path"].removeprefix("L3_group/")
                if support["path"].startswith("L3_group/") and name in review.REQUIREMENTS:
                    # The original body of another candidate must not leak via dependencies.
                    self.assertNotIn("body", support.get("definition", {}))
                    self.assertNotIn("then", support.get("definition", {}))
        for path in (self.a, self.b):
            self.assertTrue(all(row["decision"] is None for row in review.read(path)["reviews"]))

    def test_blank_forms_cannot_produce_measurements(self):
        with self.assertRaises(review.ReviewError):
            review.score(self.a, self.b, self.key_path, self.result)
        self.assertFalse(self.result.exists())

    def test_preparation_preserves_partly_filled_forms(self):
        form = review.read(self.a)
        form["reviewer"]["reviewer_id"] = "TEST_PRESERVATION"
        self.write(self.a, form)
        original = self.a.read_bytes()
        review.prepare(self.output)
        self.assertEqual(self.a.read_bytes(), original)

    def test_scoring_arithmetic_and_disagreement(self):
        _, b = self.synthetic_forms()
        b["reviews"][0]["decision"] = "reject" if b["reviews"][0]["decision"] == "accept" else "accept"
        self.write(self.b, b)
        result = review.score(self.a, self.b, self.key_path, self.result)
        self.assertEqual(result["raw_agreement_count"], 25)
        self.assertAlmostEqual(result["raw_agreement"], 25 / 26)
        a_accept = 8 / 26
        b_accept = sum(row["decision"] == "accept" for row in b["reviews"]) / 26
        chance = a_accept * b_accept + (1 - a_accept) * (1 - b_accept)
        self.assertAlmostEqual(result["cohen_kappa"], (25 / 26 - chance) / (1 - chance))
        self.assertEqual(len(result["disagreements"]), 1)
        self.assertEqual(result["reference_comparison"]["A"]["false_accept"], 0)
        self.assertEqual(result["reference_comparison"]["A"]["false_reject"], 0)

    def test_degenerate_kappa_is_undefined(self):
        self.synthetic_forms(decision="accept")
        result = review.score(self.a, self.b, self.key_path, self.result)
        self.assertEqual(result["raw_agreement"], 1)
        self.assertIsNone(result["cohen_kappa"])

    def test_nonindependent_or_duplicate_reviewer_is_rejected(self):
        a, b = self.synthetic_forms()
        for field, value in (("involved_in_generation_or_repair", True),
                             ("saw_reference_key", True),
                             ("reviewer_id", a["reviewer"]["reviewer_id"])):
            changed = deepcopy(b)
            changed["reviewer"][field] = value
            self.write(self.b, changed)
            with self.subTest(field=field), self.assertRaises(review.ReviewError):
                review.score(self.a, self.b, self.key_path, self.result)
            self.assertFalse(self.result.exists())

    def test_missing_duplicate_or_invalid_duration_row_is_rejected(self):
        _, b = self.synthetic_forms()
        variants = []
        missing = deepcopy(b)
        missing["reviews"].pop()
        variants.append(missing)
        duplicate = deepcopy(b)
        duplicate["reviews"][1] = deepcopy(duplicate["reviews"][0])
        variants.append(duplicate)
        duration = deepcopy(b)
        duration["reviews"][0]["elapsed_seconds"] = True
        variants.append(duration)
        for form in variants:
            self.write(self.b, form)
            with self.assertRaises(review.ReviewError):
                review.score(self.a, self.b, self.key_path, self.result)
        self.assertFalse(self.result.exists())


if __name__ == "__main__":
    unittest.main()
