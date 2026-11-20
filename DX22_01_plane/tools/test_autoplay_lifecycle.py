"""Exercise shutdown attribution with synthetic records; never launch a process.

These scenarios distinguish a cleanup request from evidence that the same child
actually received it and finished shutting down. A claimed cleanup match alone
must never turn an unexplained exit or an incomplete shutdown into success.
"""
from __future__ import annotations

from copy import deepcopy
import unittest

from autoplay_validation.lifecycle import classify_result


PID = 4204
TID = 7004
TOKEN = {"wparam": 0x164A9321, "lparam": 0x273B8452}


def native(event: str, tick: int, **fields: object) -> dict:
    return {
        "event": event, "pid": PID, "tid": TID, "uptime_ms": tick,
        "hidden_window": True, "visible": False, **fields,
    }


def request(event: str, tick: int, **fields: object) -> dict:
    return {
        "event": event, "phase": "fixture_finally", "pid": PID,
        "tid": TID, "uptime_ms": tick, **fields,
    }


def completed_session() -> dict:
    return {
        "process_id": PID,
        "condition": "hidden",
        "manifest": {"ok": True},
        "reference_comparable_equal": True,
        "visible_window_samples": 0,
        "exit_code": 0,
        "cleanup_nonce": dict(TOKEN),
        "native": [
            native("wm_quit_received", 110, **TOKEN),
            native("shutdown_begin", 111),
            native("shutdown_completed", 112),
            native("application_completed", 113),
        ],
        "events": [
            request("cleanup_entered", 99, poll=None, prior_native_shutdown=[]),
            request("wm_quit_send_begin", 100, **TOKEN),
            request("wm_quit_send_end", 105, **TOKEN, ok=True, last_error=0),
            request("cleanup_completed", 114, exit_code=0,
                    method="tagged_WM_QUIT_to_owned_thread"),
        ],
    }


class AutoplayLifecycleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.data = completed_session()

    def classify(self) -> dict:
        before = deepcopy(self.data)
        result = classify_result(self.data)
        self.assertEqual(self.data, before, "Classification must not rewrite its evidence")
        return result

    def assert_unattributed(self) -> dict:
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_exit")
        self.assertFalse(result["cleanup_matched"])
        self.assertFalse(result["diagnostics_ok"])
        self.assertTrue(result["evidence"]["unmatched_quit"])
        return result

    def assert_cleanup_failed(self) -> dict:
        result = self.classify()
        self.assertEqual(result["outcome"], "cleanup_failure")
        self.assertFalse(result["diagnostics_ok"])
        return result

    def test_success_requires_received_token_and_completed_orderly_shutdown(self) -> None:
        result = self.classify()
        self.assertEqual(result["outcome"], "ordinary_success")
        self.assertTrue(result["gameplay_ok"])
        self.assertTrue(result["diagnostics_ok"])
        self.assertTrue(result["cleanup_matched"])
        self.assertFalse(result["unexpected_shutdown"])
        self.assertEqual(len(result["evidence"]["matched_requests"]), 1)
        self.assertFalse(result["evidence"]["unmatched_quit"])

    def test_visible_release_condition_keeps_shutdown_attribution_requirements(self) -> None:
        self.data["condition"] = "visible"
        self.data["visible_window_samples"] = 3
        for row in self.data["native"]:
            row["hidden_window"] = False
            row["visible"] = True
        self.assertEqual(self.classify()["outcome"], "ordinary_success")
        self.data["native"][0]["wparam"] += 1
        self.assert_unattributed()

    def test_receive_can_precede_send_end_record(self) -> None:
        self.data["native"][0]["uptime_ms"] = 102
        result = self.classify()
        self.assertEqual(result["outcome"], "ordinary_success")
        self.assertTrue(result["cleanup_matched"])

    def test_receive_at_send_begin_is_accepted(self) -> None:
        self.data["native"][0]["uptime_ms"] = 100
        self.assertEqual(self.classify()["outcome"], "ordinary_success")

    def test_receive_before_send_begin_is_not_claimed_by_cleanup(self) -> None:
        self.data["native"][0]["uptime_ms"] = 99
        self.assert_unattributed()

    def test_cleanup_timeout_window_is_finite_and_inclusive(self) -> None:
        for received, expected in ((10105, "ordinary_success"), (10106, "unexpected_exit")):
            with self.subTest(received=received):
                self.data = completed_session()
                for index, event in enumerate(self.data["native"]):
                    event["uptime_ms"] = received + index
                self.data["events"][-1]["uptime_ms"] = received + 4
                result = self.classify()
                self.assertEqual(result["outcome"], expected)
                self.assertEqual(result["cleanup_matched"], expected == "ordinary_success")

    def test_each_nonce_component_must_match(self) -> None:
        for field in ("wparam", "lparam"):
            with self.subTest(field=field):
                self.data = completed_session()
                self.data["native"][0][field] += 1
                self.assert_unattributed()

    def test_native_pid_and_tid_must_match_the_owned_child_thread(self) -> None:
        for field in ("pid", "tid"):
            with self.subTest(field=field):
                self.data = completed_session()
                self.data["native"][0][field] += 1
                self.assert_unattributed()

    def test_wrong_post_destination_does_not_prove_owned_child_cleanup(self) -> None:
        for field in ("pid", "tid"):
            with self.subTest(field=field):
                self.data = completed_session()
                for event in self.data["events"][1:3]:
                    event[field] += 1
                self.assert_unattributed()

    def test_begin_and_end_must_describe_the_same_request(self) -> None:
        for field in ("pid", "tid", "wparam", "lparam"):
            with self.subTest(field=field):
                self.data = completed_session()
                self.data["events"][2][field] += 1
                self.assert_unattributed()

    def test_accepted_send_end_without_send_begin_is_not_enough(self) -> None:
        del self.data["events"][1]
        self.assert_unattributed()

    def test_native_quit_without_cleanup_request_is_unattributed(self) -> None:
        self.data["events"] = []
        self.assert_unattributed()

    def test_missing_received_timestamp_cannot_prove_a_match(self) -> None:
        del self.data["native"][0]["uptime_ms"]
        self.assert_unattributed()

    def test_declared_nonce_and_match_flag_cannot_replace_request_evidence(self) -> None:
        self.data["cleanup_matched"] = True
        self.data["native"][0].update(wparam=0, lparam=0)
        self.data["cleanup_nonce"] = {"wparam": 0, "lparam": 0}
        self.assert_unattributed()

    def test_external_quit_racing_with_accepted_cleanup_remains_unattributed(self) -> None:
        # Our post was accepted, but the thread dequeued a different quit first.
        self.data["native"][0].update(wparam=0, lparam=0, uptime_ms=102)
        result = self.assert_unattributed()
        self.assertTrue(result["unexpected_shutdown"])
        self.assertEqual(len(result["evidence"]["native_quit"]), 1)

    def test_an_additional_unmatched_quit_cannot_hide_behind_a_matching_one(self) -> None:
        self.data["native"].insert(0, native("wm_quit_received", 101, wparam=0, lparam=0))
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_exit")
        self.assertFalse(result["diagnostics_ok"])
        self.assertEqual(len(result["evidence"]["unmatched_quit"]), 1)

    def test_already_started_native_shutdown_is_not_relabelled_as_own_cleanup(self) -> None:
        prior = native("shutdown_begin", 98)
        self.data["events"][0]["prior_native_shutdown"] = [prior]
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_exit")
        self.assertTrue(result["unexpected_shutdown"])
        self.assertTrue(result["evidence"]["prior_native_shutdown"])

    def test_process_already_exited_when_cleanup_begins_is_not_success(self) -> None:
        self.data["events"][0]["poll"] = 0
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_exit")
        self.assertTrue(result["unexpected_shutdown"])

    def test_early_crash_without_native_log_remains_an_unexplained_exit(self) -> None:
        self.data.update(exit_code=-1073741819, native=[])
        self.data["manifest"] = {"ok": False, "error": "Process exited unexpectedly: -1073741819"}
        self.data["events"] = [request("cleanup_entered", 99, poll=-1073741819, prior_native_shutdown=[])]
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_exit")
        self.assertFalse(result["diagnostics_ok"])
        self.assertFalse(result["cleanup_matched"])

    def test_cancelled_close_is_observed_without_becoming_an_exit(self) -> None:
        self.data["native"][:0] = [
            native("wm_close_received", 90, message=0x10, wparam=0, lparam=0),
            native("wm_close_result", 91, message=0x10, result=2),
        ]
        result = self.classify()
        self.assertEqual(result["outcome"], "ordinary_success")
        self.assertFalse(result["unexpected_shutdown"])
        self.assertTrue(result["evidence"]["close_events"])
        self.assertTrue(any(event.get("result") == 2 for event in result["evidence"]["cancelled_close_events"]))

    def test_approved_close_and_destroy_are_not_attributed_to_our_later_post(self) -> None:
        self.data["native"][:0] = [
            native("sc_close_received", 90, message=0x112, wparam=0xF060),
            native("wm_close_received", 91, message=0x10),
            native("wm_close_result", 92, message=0x10, result=1),
            native("wm_destroy_received", 93, message=2),
        ]
        self.data["native"][4].update(wparam=0, lparam=0, uptime_ms=94)
        result = self.classify()
        self.assertEqual(result["outcome"], "unexpected_close_request")
        self.assertFalse(result["cleanup_matched"])
        self.assertFalse(result["diagnostics_ok"])
        self.assertTrue(result["evidence"]["unmatched_quit"])

    def test_matching_quit_followed_by_nonzero_final_exit_is_not_success(self) -> None:
        for code in (1, -1073741819):
            with self.subTest(exit_code=code):
                self.data = completed_session()
                self.data["exit_code"] = code
                self.data["events"][-1]["exit_code"] = code
                result = self.assert_cleanup_failed()
                self.assertTrue(result["cleanup_matched"])

    def test_matched_quit_without_a_final_exit_code_is_not_success(self) -> None:
        self.data["exit_code"] = None
        self.data["events"][-1]["exit_code"] = None
        result = self.assert_cleanup_failed()
        self.assertTrue(result["cleanup_matched"])

    def test_each_shutdown_marker_is_required_after_a_matched_quit(self) -> None:
        for missing in ("shutdown_begin", "shutdown_completed", "application_completed"):
            with self.subTest(missing=missing):
                self.data = completed_session()
                self.data["native"] = [event for event in self.data["native"] if event["event"] != missing]
                result = self.assert_cleanup_failed()
                self.assertTrue(result["cleanup_matched"])

    def test_shutdown_record_order_must_follow_quit_and_begin(self) -> None:
        for order in ((1, 0, 2, 3), (0, 2, 1, 3), (0, 1, 3, 2)):
            with self.subTest(order=order):
                self.data = completed_session()
                self.data["native"] = [self.data["native"][index] for index in order]
                self.assert_cleanup_failed()

    def test_shutdown_timestamps_cannot_run_backwards(self) -> None:
        self.data["native"][2]["uptime_ms"] = 109
        self.assert_cleanup_failed()

    def test_failed_post_and_forced_termination_are_cleanup_failure(self) -> None:
        self.data["events"][2].update(ok=False, last_error=1444)
        self.data["events"].insert(-1, request("terminate_owned_child", 106))
        self.data.update(native=[], exit_code=1)
        self.data["events"][-1].update(exit_code=1, method="terminate_after_failed_post")
        self.assert_cleanup_failed()

    def test_refused_post_without_native_shutdown_is_not_success(self) -> None:
        self.data["events"][2].update(ok=False, last_error=1444)
        self.data["native"] = []
        result = self.assert_cleanup_failed()
        self.assertFalse(result["cleanup_matched"])

    def test_accepted_post_then_shutdown_timeout_and_kill_is_not_success(self) -> None:
        self.data["native"] = self.data["native"][:2]
        self.data["events"].insert(-1, request("kill_owned_child", 10106))
        self.data.update(exit_code=1)
        self.data["events"][-1].update(exit_code=1, method="kill_after_shutdown_timeout")
        result = self.assert_cleanup_failed()
        self.assertTrue(result["cleanup_matched"])

    def test_gameplay_timeout_with_orderly_cleanup_is_not_spontaneous_exit(self) -> None:
        self.data["manifest"] = {"ok": False, "error": "TimeoutError: waiting for terminal autoplay state"}
        result = self.classify()
        self.assertEqual(result["outcome"], "fixture_timeout")
        self.assertTrue(result["cleanup_matched"])
        self.assertFalse(result["unexpected_shutdown"])

    def test_absent_child_pid_is_launch_failure(self) -> None:
        self.data.update(process_id=None, native=[], events=[], exit_code=None)
        self.data["manifest"] = {"ok": False, "error": "Executable could not be started"}
        self.assertEqual(self.classify()["outcome"], "launch_failure")

    def test_partially_parsed_native_log_cannot_be_certified_as_complete(self) -> None:
        self.data["native_parse_error"] = "Malformed JSON at lifecycle line 5"
        result = self.classify()
        self.assertEqual(result["outcome"], "diagnostics_failure")
        self.assertFalse(result["diagnostics_ok"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
