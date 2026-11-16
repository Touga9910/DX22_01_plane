"""Exercise campaign CLI boundaries without starting a native fixture.

Only temporary fake executables, references and output directories are used.
The capture boundary is replaced, and native Session construction is forbidden.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from copy import deepcopy
from datetime import datetime, timedelta, timezone
from io import StringIO
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from autoplay_validation import campaign, capture


NOW = 1_800_000_000.0


def iso_time(seconds: float, offset_hours: int = 0) -> str:
    return datetime.fromtimestamp(seconds, timezone(timedelta(hours=offset_hours))).isoformat()


class CampaignCliBoundaryTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exe = self.root / "fixture-never-launched.exe"
        self.exe.write_bytes(b"synthetic executable identity, not a native executable")
        self.output = self.root / "output"
        self.serial = 0
        self.last_label = ""
        self.clock = Mock(return_value=NOW)
        self.captured = []

        def fake_capture(exe, folder, **options):
            self.captured.append({"exe": exe, "folder": folder, **deepcopy(options)})
            return {"case": options["case"], "condition": options["condition"],
                    "outcome": "ordinary_success", "elapsed_seconds": 0.25,
                    "evidence": {"close_events": []}}

        self.capture = Mock(side_effect=fake_capture)
        self.session_guard = Mock(side_effect=AssertionError("Native Session must not be constructed by these tests"))
        for target, name, value in ((campaign, "capture_trial", self.capture),
                                    (campaign, "time", SimpleNamespace(time=self.clock)),
                                    (capture, "Session", self.session_guard)):
            replacement = patch.object(target, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        self.identity = campaign._source_identity()
        self.addCleanup(self.assert_native_not_used_and_package_unchanged)

    def assert_native_not_used_and_package_unchanged(self) -> None:
        self.session_guard.assert_not_called()
        self.assertEqual(campaign._source_identity(), self.identity)

    def invoke(self, *options: str) -> tuple[int, dict]:
        self.serial += 1
        self.last_label = f"synthetic_{self.serial}"
        argv = ["verify_autoplay.py", "--exe", str(self.exe), "--output-root", str(self.output),
                "--label", self.last_label, "--trials", "1", "--cases", "terminal_stop", *options]
        with patch.object(sys, "argv", argv), redirect_stdout(StringIO()), redirect_stderr(StringIO()):
            result = campaign.main()
        path = self.output / "campaigns" / self.last_label / "summary.json"
        return result, json.loads(path.read_text(encoding="utf-8"))

    def assert_rejected(self, *options: str, parser: bool = False) -> None:
        previous_calls = self.capture.call_count
        with self.assertRaises(SystemExit if parser else (AssertionError, ValueError)) as raised:
            self.invoke(*options)
        if parser:
            self.assertEqual(raised.exception.code, 2)
        self.assertEqual(self.capture.call_count, previous_calls)
        self.assertFalse((self.output / "campaigns" / self.last_label).exists())

    def reference(self, seed: int = 20261116, decisions: int = 150, case: str = "terminal_stop") -> Path:
        root = self.root / f"reference_{case}_{seed}_{decisions}"
        folder = root / case
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "manifest.json").write_text(json.dumps({"preparation": {
            "run_seed": seed, "decision_delay_frames": decisions}}), encoding="utf-8")
        (folder / "comparable.json").write_text("{}", encoding="utf-8")
        return root

    def test_controlled_decision_window_accepts_both_endpoints(self) -> None:
        for frames in (120, 180):
            with self.subTest(frames=frames):
                result, summary = self.invoke("--decision-frames", str(frames))
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["decision_frames"], frames)
                self.assertEqual(summary["decision_frames"], frames)

    def test_controlled_window_rejects_ordinary_speed_or_unbounded_delay(self) -> None:
        for frames in (20, 119, 181, 600):
            with self.subTest(frames=frames):
                self.assert_rejected("--decision-frames", str(frames))

    def test_ordinary_decision_window_accepts_both_endpoints(self) -> None:
        for frames in (20, 600):
            with self.subTest(frames=frames):
                result, summary = self.invoke("--scenario", "ordinary", "--decision-frames", str(frames))
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["scenario"], "ordinary")
                self.assertEqual(self.captured[-1]["decision_frames"], frames)
                self.assertEqual(summary["scenario"], "ordinary")

    def test_ordinary_window_rejects_less_than_twenty_or_more_than_six_hundred_frames(self) -> None:
        for frames in (0, 19, 601):
            with self.subTest(frames=frames):
                self.assert_rejected("--scenario", "ordinary", "--decision-frames", str(frames))

    def test_publication_interval_has_two_observations_per_ordinary_decision(self) -> None:
        for decisions, publish in ((20, 1), (20, 10), (21, 10), (600, 60)):
            with self.subTest(decisions=decisions, publish=publish):
                result, summary = self.invoke("--scenario", "ordinary", "--decision-frames", str(decisions),
                                              "--publish-frames", str(publish))
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["publish_frames"], publish)
                self.assertEqual(summary["publish_frames"], publish)
        for decisions, publish in ((20, 11), (21, 11), (600, 61), (600, 0), (600, -1)):
            with self.subTest(decisions=decisions, publish=publish):
                self.assert_rejected("--scenario", "ordinary", "--decision-frames", str(decisions),
                                     "--publish-frames", str(publish))

    def test_controlled_publication_still_has_a_sixty_frame_ceiling(self) -> None:
        result, _ = self.invoke("--decision-frames", "120", "--publish-frames", "60")
        self.assertEqual(result, 0)
        self.assert_rejected("--decision-frames", "180", "--publish-frames", "61")

    def test_frame_controls_require_integer_cli_literals(self) -> None:
        for option in ("--publish-frames", "--decision-frames"):
            for value in ("1.5", "true", "False"):
                with self.subTest(option=option, value=value):
                    self.assert_rejected(option, value, parser=True)

    def test_supported_render_requests_are_forwarded_and_recorded(self) -> None:
        for rate in (30, 60, 144):
            with self.subTest(rate=rate):
                result, summary = self.invoke("--render-hz", str(rate))
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["render_hz"], rate)
                self.assertEqual(summary["render_hz_requested"], rate)

    def test_unsupported_render_requests_are_not_silently_rounded(self) -> None:
        for rate in ("0", "59", "61", "145", "60.0"):
            with self.subTest(rate=rate):
                self.assert_rejected("--render-hz", rate, parser=True)

    def test_no_render_option_does_not_request_an_override(self) -> None:
        result, summary = self.invoke()
        self.assertEqual(result, 0)
        self.assertIsNone(self.captured[-1]["render_hz"])
        self.assertIsNone(summary["render_hz_requested"])

    def test_profile_default_timeouts_differ_and_scale_with_native_runs(self) -> None:
        for scenario, runs, expected in (("controlled", 1, 180), ("controlled", 3, 540),
                                          ("ordinary", 1, 1200), ("ordinary", 3, 3600)):
            with self.subTest(scenario=scenario, runs=runs):
                result, summary = self.invoke("--scenario", scenario, "--cases", "maximum_runs",
                                              "--native-runs", str(runs))
                self.assertEqual(result, 0)
                self.assertEqual(summary["case_timeout"], expected)
                self.assertEqual(self.captured[-1]["timeout_seconds"], expected)
                self.assertEqual(self.captured[-1]["expected_runs"], runs)

    def test_timeout_upper_boundary_and_non_finite_input(self) -> None:
        result, _ = self.invoke("--case-timeout", "10800")
        self.assertEqual(result, 0)
        for value in ("0", "-1", "10800.001", "nan", "inf"):
            with self.subTest(value=value):
                self.assert_rejected("--case-timeout", value)

    def test_multiple_native_runs_require_the_maximum_runs_case_exclusively(self) -> None:
        for cases in (("terminal_stop",), ("maximum_runs", "terminal_stop")):
            with self.subTest(cases=cases):
                self.assert_rejected("--scenario", "ordinary", "--native-runs", "2", "--cases", *cases)

    def test_ordinary_profile_cannot_use_two_battle_historical_reference(self) -> None:
        self.assert_rejected("--scenario", "ordinary", "--reference-root", str(self.root / "absent_reference"))

    def test_historical_reference_allows_only_default_or_sixty_hz_requests(self) -> None:
        reference = self.reference()
        for rate in (None, 60):
            with self.subTest(rate=rate):
                options = [] if rate is None else ["--render-hz", str(rate)]
                result, _ = self.invoke("--reference-root", str(reference), *options)
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["reference"], reference / "terminal_stop/comparable.json")
        for rate in (30, 144):
            with self.subTest(rate=rate):
                self.assert_rejected("--reference-root", str(reference), "--render-hz", str(rate))

    def test_historical_reference_cannot_change_publication_seed_or_decision_window(self) -> None:
        reference = self.reference()
        for options in (("--publish-frames", "2"), ("--decision-frames", "120"),
                        ("--seeds", "20261116", "20261117")):
            with self.subTest(options=options):
                self.assert_rejected("--reference-root", str(reference), *options)

    def test_historical_single_run_reference_cannot_validate_multi_run_capture(self) -> None:
        reference = self.reference(case="maximum_runs")
        self.assert_rejected("--reference-root", str(reference), "--native-runs", "2", "--cases", "maximum_runs")

    def test_naive_or_malformed_stop_timestamp_is_rejected_before_output(self) -> None:
        for value in ("2026-11-17T03:00:00", "not-a-timestamp", "2026-13-01T03:00:00+00:00"):
            with self.subTest(value=value):
                self.assert_rejected("--stop-before-utc", value)

    def test_deadline_at_exact_trial_budget_stops_before_any_capture(self) -> None:
        result, summary = self.invoke("--case-timeout", "100", "--stop-before-utc", iso_time(NOW + 120))
        self.assertEqual(result, 0)
        self.assertEqual(summary["status"], "deadline_reached")
        self.assertEqual(summary["completed_trials"], [])
        self.capture.assert_not_called()
        self.assertFalse((self.output / "trials").exists())

    def test_past_deadline_stops_without_creating_a_native_trial(self) -> None:
        result, summary = self.invoke("--stop-before-utc", iso_time(NOW - 1))
        self.assertEqual(result, 0)
        self.assertEqual(summary["status"], "deadline_reached")
        self.capture.assert_not_called()

    def test_one_millisecond_more_than_whole_trial_budget_allows_the_trial(self) -> None:
        result, summary = self.invoke("--case-timeout", "100", "--stop-before-utc", iso_time(NOW + 120.001))
        self.assertEqual(result, 0)
        self.assertEqual(summary["status"], "completed")
        self.assertEqual(len(summary["completed_trials"]), 1)
        self.capture.assert_called_once()

    def test_equivalent_timestamp_offsets_apply_the_same_deadline(self) -> None:
        for offset in (0, 9, -5):
            with self.subTest(offset=offset):
                previous_calls = self.capture.call_count
                result, summary = self.invoke("--case-timeout", "100", "--stop-before-utc", iso_time(NOW + 120, offset))
                self.assertEqual(result, 0)
                self.assertEqual(summary["status"], "deadline_reached")
                self.assertEqual(self.capture.call_count, previous_calls)

    def test_deadline_reserves_all_native_runs_in_the_next_trial(self) -> None:
        result, summary = self.invoke("--scenario", "ordinary", "--cases", "maximum_runs", "--native-runs", "3",
                                      "--stop-before-utc", iso_time(NOW + 1201))
        self.assertEqual(result, 0)
        self.assertEqual(summary["case_timeout"], 3600)
        self.assertEqual(summary["status"], "deadline_reached")
        self.capture.assert_not_called()

    def test_deadline_is_checked_again_between_trials_without_relabelling_completed_work(self) -> None:
        self.clock.side_effect = [NOW, NOW + 101]
        result, summary = self.invoke("--trials", "2", "--case-timeout", "100", "--stop-before-utc", iso_time(NOW + 220))
        self.assertEqual(result, 0)
        self.assertEqual(summary["status"], "deadline_reached")
        self.assertEqual(len(summary["completed_trials"]), 1)
        self.assertEqual(summary["completed_trials"][0]["outcome"], "ordinary_success")
        self.capture.assert_called_once()

    def test_seed_and_condition_schedule_is_forwarded_without_changing_settings(self) -> None:
        result, summary = self.invoke("--scenario", "ordinary", "--trials", "5", "--seeds", "0", "4294967295",
                                      "--conditions", "hidden", "legacy_rehide", "--decision-frames", "20",
                                      "--publish-frames", "10", "--render-hz", "30")
        self.assertEqual(result, 0)
        self.assertEqual([(row["seed"], row["condition"]) for row in self.captured],
                         [(0, "hidden"), (0, "legacy_rehide"), (0xFFFFFFFF, "hidden"),
                          (0xFFFFFFFF, "legacy_rehide"), (0, "hidden")])
        self.assertTrue(all(row["publish_frames"] == 10 and row["render_hz"] == 30
                            and row["decision_frames"] == 20 and row["scenario"] == "ordinary"
                            for row in self.captured))
        self.assertEqual(summary["status"], "completed")

    def test_optional_resource_flag_defaults_false_and_is_forwarded(self) -> None:
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                result, summary = self.invoke(*(["--observe-resources"] if enabled else []))
                self.assertEqual(result, 0)
                self.assertEqual(self.captured[-1]["observe_resources"], enabled)
                self.assertEqual(summary["observe_resources"], enabled)


if __name__ == "__main__":
    unittest.main()
