"""Require deliberate cleanup context, not just matching message numbers."""
from copy import deepcopy
import unittest

from autoplay_validation.lifecycle import classify_result
from test_autoplay_lifecycle import completed_session


class CleanupAttributionContextTests(unittest.TestCase):
    def assert_not_cleanup(self, data):
        original = deepcopy(data)
        result = classify_result(data)
        self.assertEqual(data, original)
        self.assertFalse(result["cleanup_matched"])
        self.assertFalse(result["diagnostics_ok"])
        self.assertNotEqual(result["outcome"], "ordinary_success")

    def test_explicit_injection_is_not_a_finally_cleanup_even_with_equal_token(self):
        data = completed_session()
        for row in data["events"]:
            if row["event"].startswith("wm_quit_send_"):
                row["phase"] = "explicit_injection"
        self.assert_not_cleanup(data)

    def test_missing_request_phase_cannot_establish_cleanup(self):
        for event in ("wm_quit_send_begin", "wm_quit_send_end"):
            with self.subTest(event=event):
                data = completed_session()
                next(row for row in data["events"] if row["event"] == event).pop("phase")
                self.assert_not_cleanup(data)

    def test_mixed_request_contexts_do_not_form_one_cleanup_pair(self):
        data = completed_session()
        next(row for row in data["events"] if row["event"] == "wm_quit_send_end")["phase"] = "explicit_injection"
        self.assert_not_cleanup(data)

    def test_missing_expected_nonce_does_not_authenticate_recorded_numbers(self):
        data = completed_session()
        data.pop("cleanup_nonce")
        self.assert_not_cleanup(data)

    def test_both_expected_nonce_components_are_required(self):
        for key in ("wparam", "lparam"):
            with self.subTest(key=key):
                data = completed_session()
                data["cleanup_nonce"].pop(key)
                self.assert_not_cleanup(data)

    def test_public_capture_schema_retains_the_same_attribution(self):
        original = completed_session()
        public = {"process_id": original["process_id"], "condition": original["condition"],
                  "gameplay_ok": True, "cleanup": {"exit_code": 0},
                  "request_events": original["events"], "native_events": original["native"],
                  "visible_samples": 0, "nonce": original["cleanup_nonce"],
                  "reference_comparable_equal": True}
        self.assertEqual(classify_result(public), classify_result(original))


if __name__ == "__main__":
    unittest.main()
