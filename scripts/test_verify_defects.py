"""Safeguards for distinguishing actual detections from comparison findings."""
from copy import deepcopy
from contextlib import redirect_stderr
import io
import unittest
from unittest.mock import patch

import verify_defects as verify
from defect_cases import build_cases


class DefectVerificationTests(unittest.TestCase):
    def test_cases_have_distinct_mutants_and_explicit_restorations(self):
        cases = build_cases(verify.ROOT)
        self.assertGreaterEqual(len(cases), 15)
        self.assertEqual(len({case["id"] for case in cases}), len(cases))
        self.assertEqual(len({case["example"] for case in cases}), 5)
        for case in cases:
            with self.subTest(case=case["id"]):
                self.assertEqual(case["control_request"], case["fixed_request"])
                self.assertNotEqual(case["control_request"], case["faulty_request"])

    def test_output_cannot_overwrite_versioned_sources(self):
        for destination in (verify.ROOT, verify.ROOT / "examples/new",
                            verify.ROOT / "scripts", verify.ROOT / "tests"):
            with self.assertRaises(verify.VerificationError):
                verify.validate_output(destination)

    def test_comparison_is_type_sensitive(self):
        self.assertIsNotNone(verify.first_difference({"volume": 1}, {"volume": True}))

    def test_timing_difference_is_oracle_only_not_engine_rejection(self):
        normal = {"structural": {"accepted": True, "errors": []},
                  "simulation": {"status": "SUCCESS", "message": "", "audit": [],
                                 "variables": {}, "devices": {}, "sim_time_ms": 120000, "steps": 3}}
        faulty = deepcopy(normal)
        faulty["simulation"]["sim_time_ms"] = 1000
        result = verify.summary(faulty, normal)
        self.assertTrue(result["structural_accepted"])
        self.assertEqual(result["simulation_status"], "SUCCESS")
        self.assertFalse(result["oracle_matched"])

    def test_registered_miss_is_not_changed_to_a_detection(self):
        case = {"faulty": {"structural_accepted": True, "simulation_status": "SUCCESS", "oracle_matched": True},
                "control": {"observation_sha256": "baseline"}}
        self.assertEqual(verify.measured_signature(case)["simulation_status"], "SUCCESS")
        self.assertTrue(verify.measured_signature(case)["oracle_matched"])

    def test_runner_failure_returns_nonzero(self):
        with patch.object(verify, "verify", side_effect=verify.VerificationError("probe failed")):
            with redirect_stderr(io.StringIO()):
                self.assertEqual(verify.main([]), 1)


if __name__ == "__main__":
    unittest.main()
