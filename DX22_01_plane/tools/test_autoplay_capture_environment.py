"""Verify capture's child environment and recorded render request without native IO.

All fixtures/executables are temporary synthetic files. Native Session creation
is replaced by a Mock before capture_trial is called.
"""
from __future__ import annotations

from contextlib import redirect_stdout
from io import StringIO
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from autoplay_validation import capture


RENDER_KEY = "DX22_TEST_RENDER_HZ"


class CaptureEnvironmentTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.exe = self.root / "synthetic-never-launched.exe"
        self.exe.write_bytes(b"not a native executable")
        self.serial = 0
        self.launch_options = {}

        def remember_launch(command, output, **options):
            self.launch_options = options
            return SimpleNamespace(pid=404)

        self.session = SimpleNamespace(
            launch=Mock(side_effect=remember_launch), cleanup=Mock(return_value={"exit_code": 0, "errors": []}),
            observe=Mock(return_value=[]), events=[], windows=[], trace_errors=[],
            visible_samples=0, wparam=101, lparam=202,
            resources_summary=Mock(return_value={"enabled": False, "status": "disabled", "samples": 0}))
        self.driver = SimpleNamespace(
            run=Mock(return_value={"scene": "result", "autoplay_enabled": False,
                                   "stop_after_current_run": False}),
            commands=[], milestones=[], latest_state={})

        def prepare(folder, *args, **options):
            folder.mkdir(parents=True)
            return {"synthetic": True}

        patches = {
            "Session": Mock(return_value=self.session),
            "ScenarioDriver": Mock(return_value=self.driver),
            "prepare_fixture": Mock(side_effect=prepare),
            "prepare_ordinary_fixture": Mock(side_effect=prepare),
            "run_ordinary": Mock(side_effect=lambda driver: driver.run()),
            "validate_run_logs": Mock(), "validate_ordinary_logs": Mock(),
            "read_native": Mock(return_value=[]),
            "classify_result": Mock(return_value={"outcome": "synthetic", "evidence": {"native_quit": []}}),
        }
        for name, value in patches.items():
            replacement = patch.object(capture, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        self.session_constructor = patches["Session"]
        environment_patch = patch.dict(os.environ, {"AUTOPLAY_CAPTURE_ENV_SENTINEL": "retain-other-environment"})
        environment_patch.start()
        self.addCleanup(environment_patch.stop)

    def capture_and_check(self, requested, *, scenario="controlled", condition="hidden", observe_resources=False) -> dict:
        self.serial += 1
        self.session.resources_summary.return_value = {"enabled": observe_resources, "samples": 0}
        folder = self.root / f"trial_{self.serial}"
        parent_before = dict(os.environ)
        with redirect_stdout(StringIO()):
            result = capture.capture_trial(self.exe, folder, case="terminal_stop", condition=condition,
                seed=10, decision_frames=150, scenario=scenario, render_hz=requested, observe_resources=observe_resources)
        self.assertEqual(dict(os.environ), parent_before, "Capture must not mutate its parent's environment")
        self.session_constructor.assert_called_with(folder, condition, observe_resources=observe_resources)
        self.assertIn("env", self.launch_options, "Capture must pass the sanitized child environment explicitly")
        child_env = self.launch_options["env"]
        self.assertIsNot(child_env, os.environ)
        expected_env = {key: value for key, value in parent_before.items() if key != RENDER_KEY}
        if requested is not None:
            expected_env[RENDER_KEY] = str(requested)
        self.assertEqual(child_env, expected_env, "Only the render override may differ from the copied parent environment")
        manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["observation_settings"]["observe_resources"], observe_resources)
        self.assertEqual(result["resources"], self.session.resources_summary.return_value)
        expected_request = None if requested is None else str(requested)
        self.assertEqual(manifest["observation_settings"]["DX22_TEST_RENDER_HZ_requested"], expected_request)
        self.assertEqual(result["render_hz_requested"], expected_request)
        self.assertEqual(child_env.get(RENDER_KEY), expected_request)
        return result

    def test_none_removes_any_inherited_render_override(self) -> None:
        for inherited in ("30", "60", "144", "unrecognized", ""):
            with self.subTest(inherited=inherited), patch.dict(os.environ, {RENDER_KEY: inherited}):
                self.capture_and_check(None)

    def test_none_with_no_parent_override_preserves_the_normal_default(self) -> None:
        with patch.dict(os.environ):
            os.environ.pop(RENDER_KEY, None)
            self.capture_and_check(None)

    def test_explicit_supported_rate_overrides_a_different_parent_rate(self) -> None:
        for requested in (30, 60, 144):
            inherited = "30" if requested != 30 else "144"
            with self.subTest(requested=requested), patch.dict(os.environ, {RENDER_KEY: inherited}):
                self.capture_and_check(requested)

    def test_explicit_rate_is_added_when_parent_has_no_render_key(self) -> None:
        for requested in (30, 60, 144):
            with self.subTest(requested=requested), patch.dict(os.environ):
                os.environ.pop(RENDER_KEY, None)
                self.capture_and_check(requested)

    def test_both_profiles_and_visibility_conditions_share_the_same_render_policy(self) -> None:
        for scenario in ("controlled", "ordinary"):
            for condition in ("hidden", "legacy_rehide"):
                with self.subTest(scenario=scenario, condition=condition), \
                        patch.dict(os.environ, {RENDER_KEY: "144", "DX22_TEST_HIDDEN_WINDOW": "parent-value"}):
                    self.capture_and_check(None, scenario=scenario, condition=condition)
                    self.capture_and_check(30, scenario=scenario, condition=condition)

    def test_optional_resource_flag_reaches_session_and_metadata(self) -> None:
        for enabled in (False, True):
            with self.subTest(enabled=enabled):
                self.capture_and_check(None, observe_resources=enabled)


if __name__ == "__main__":
    unittest.main()
