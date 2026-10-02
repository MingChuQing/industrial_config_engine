"""Fast controls for CLI failure reporting and evidence comparison safeguards."""
from contextlib import redirect_stderr
import io
import unittest
from unittest.mock import patch

import verify_beverage_full as verify


class VerificationCliTests(unittest.TestCase):
    def test_stale_trace_hash_is_rejected_with_location(self):
        with self.assertRaisesRegex(verify.VerificationError, r"recipes\[0\]\.trace_sha256"):
            verify.require_equal({"recipes": [{"trace_sha256": "fresh"}]},
                                 {"recipes": [{"trace_sha256": "stale"}]}, "saved report")

    def test_numeric_types_do_not_hide_invalid_counts(self):
        with self.assertRaisesRegex(verify.VerificationError, "type int != bool"):
            verify.require_equal({"containers": 1}, {"containers": True}, "saved report")

    def test_configuration_normalization_preserves_other_bytes(self):
        self.assertEqual(verify.normalize_newlines(b"{\r\n  x\r\n}"), b"{\n  x\n}")
        self.assertNotEqual(verify.normalize_newlines(b"{\r\n x\r\n}"), b"{\n  x\n}")

    def test_output_cannot_overwrite_versioned_sources(self):
        for destination in (verify.ROOT, verify.SOURCE / "reports", verify.ROOT / "scripts/new"):
            with self.subTest(destination=destination), self.assertRaises(verify.VerificationError):
                verify.validate_output(destination)

    def test_missing_cpp_prerequisites_are_explicit(self):
        with patch.object(verify.shutil, "which", return_value=None):
            with self.assertRaisesRegex(verify.VerificationError, "Missing: cmake, ctest"):
                verify.cpp_tools()

    def test_failed_verification_returns_nonzero(self):
        log = io.StringIO()
        with patch.object(verify, "verify", side_effect=verify.VerificationError("saved report is stale")):
            with redirect_stderr(log):
                self.assertEqual(verify.main([]), 1)
        self.assertIn("FAIL: saved report is stale", log.getvalue())


if __name__ == "__main__":
    unittest.main()
