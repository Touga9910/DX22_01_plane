"""Verify ordinary-data observation with copied assets and synthetic frames only.

No game, compiler or external AI client is started. The AI-entry-point check
uses its real argument parser/loader and replaces the HTTP boundary with a spy.
"""
from __future__ import annotations

from contextlib import redirect_stdout
from copy import deepcopy
import hashlib
from io import StringIO
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from autoplay_validation import ordinary
import generate_balance_plan_with_ai as ai_tool


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8-sig")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def tree_bytes(folder: Path) -> dict[str, bytes]:
    return {path.relative_to(folder).as_posix(): path.read_bytes()
            for path in folder.rglob("*") if path.is_file()}


class OrdinaryFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.data = self.source / "assets/data"
        self.configurations = {
            "balance_autoplay.json": {
                "enabled": False, "decision_delay_frames": 90, "random_seed": 13,
                "min_shot_power": 4.0, "aim_jitter_degrees": 1.5,
                "future_policy": {"name": "残す", "weights": [7, 3], "extra": None},
            },
            "balance_validation.json": {
                "enabled": True, "random_seed": 999, "random_seeds": [999, 888],
                "fixed_stage_schedule": True, "seed_suites": {"holdout": [13, 11]},
                "variants": [{"id": "future", "attack": 900001}],
            },
            "game_mcp_bridge.json": {"enabled": False, "future_option": {"x": 2}},
            "auto_balance_loop.json": {"enabled": True, "future_option": [5, 2]},
            "balance_ai_openai.json": {"provider": "openai", "model": "retain", "future": 42},
            "enemy_data.json": {"enemies": [{"id": "normal", "hp": 31, "attack": 7}]},
            "player_balls.json": {"hp": 100, "damage": 9000001},
            "stage_layout.json": {"positions": [[1, 2], [3, 4]]},
            "rewards.json": {"money": 15, "offers": ["third", "first"]},
        }
        for name, value in self.configurations.items():
            write_json(self.data / name, value)
        (self.source / "shader/include").mkdir(parents=True)
        (self.source / "shader/main.hlsl").write_bytes(b"// synthetic shader\r\n")
        (self.source / "shader/include/common.hlsl").write_bytes(b"// include\n")
        (self.source / "test.cso").write_bytes(b"\x00synthetic shader\xff")
        (self.source / "assets/texture.bin").write_bytes(b"\x00texture\xff")
        self.original = tree_bytes(self.source)
        source_patch = patch.object(ordinary, "SOURCE_ROOT", self.source)
        source_patch.start()
        self.addCleanup(source_patch.stop)

    def prepare(self, **options: object) -> tuple[Path, dict]:
        folder = self.root / "fixture"
        arguments = {"stop_at_result": True, "seed": 20261116, "decision_frames": 150, **options}
        metadata = ordinary.prepare_ordinary_fixture(folder, **arguments)
        self.assertEqual(tree_bytes(self.source), self.original)
        return folder, metadata

    def test_source_and_every_gameplay_asset_remain_byte_identical(self) -> None:
        folder, metadata = self.prepare()
        for relative in ("assets/data/enemy_data.json", "assets/data/player_balls.json",
                         "assets/data/stage_layout.json", "assets/data/rewards.json",
                         "assets/texture.bin", "shader/main.hlsl", "shader/include/common.hlsl", "test.cso"):
            with self.subTest(path=relative):
                self.assertEqual((folder / relative).read_bytes(), self.original[relative])
        self.assertIn("enemy_data.json", metadata["preserved_gameplay_json_sha256"])

    def test_only_autoplay_execution_controls_replace_existing_values(self) -> None:
        folder, _ = self.prepare()
        actual = read_json(folder / "assets/data/balance_autoplay.json")
        self.assertTrue(actual["enabled"])
        self.assertTrue(actual["restart_after_game_over"])
        self.assertTrue(actual["stop_after_current_run"])
        self.assertEqual((actual["random_seed"], actual["decision_delay_frames"], actual["max_runs"]),
                         (20261116, 150, 1))
        for field in ("min_shot_power", "aim_jitter_degrees", "future_policy"):
            self.assertEqual(actual[field], self.configurations["balance_autoplay.json"][field])

    def test_validation_override_data_survives_but_is_disabled(self) -> None:
        folder, _ = self.prepare()
        self.assertEqual(read_json(folder / "assets/data/balance_validation.json"),
                         dict(self.configurations["balance_validation.json"], enabled=False))

    def test_mcp_is_read_only_and_observes_every_frame(self) -> None:
        folder, metadata = self.prepare()
        bridge = read_json(folder / "assets/data/game_mcp_bridge.json")
        self.assertTrue(bridge["enabled"])
        self.assertFalse(bridge["allow_write_actions"])
        self.assertEqual(bridge["state_publish_interval_frames"], 1)
        self.assertEqual(bridge["command_poll_interval_frames"], 1)
        self.assertEqual(bridge["future_option"], {"x": 2})
        self.assertEqual(metadata["commands_expected"], 0)

    def test_settings_only_mute_audio_and_disable_fullscreen(self) -> None:
        folder, _ = self.prepare()
        self.assertEqual(read_json(folder / "saves/settings.json"),
                         {"bgm_volume": 0, "se_volume": 0, "fullscreen": False})

    def test_external_loop_is_disabled_without_losing_unknown_options(self) -> None:
        folder, _ = self.prepare()
        self.assertEqual(read_json(folder / "assets/data/auto_balance_loop.json"),
                         dict(self.configurations["auto_balance_loop.json"], enabled=False))

    def test_ai_config_without_enabled_flag_is_retired_and_recorded(self) -> None:
        folder, metadata = self.prepare()
        config = folder / "assets/data/balance_ai_openai.json"
        self.assertFalse(config.exists())
        retired = [item for item in metadata["configuration_changes"]
                   if item["path"] == "assets/data/balance_ai_openai.json"][0]
        self.assertFalse(retired["enabled_was_present"])
        self.assertFalse(retired["default_path_exists_after"])
        self.assertEqual((folder / retired["retired_path"]).read_bytes(),
                         self.original["assets/data/balance_ai_openai.json"])

    def test_existing_retired_config_is_not_overwritten(self) -> None:
        preserved = self.data / "balance_ai_openai.json.ordinary-disabled"
        preserved.write_bytes(b"previous independent configuration")
        self.original = tree_bytes(self.source)
        folder, metadata = self.prepare()
        self.assertEqual((folder / "assets/data" / preserved.name).read_bytes(), preserved.read_bytes())
        retired = next(item for item in metadata["configuration_changes"]
                       if item["path"] == "assets/data/balance_ai_openai.json")
        self.assertNotEqual(retired["retired_path"], "assets/data/" + preserved.name)
        self.assertEqual((folder / retired["retired_path"]).read_bytes(),
                         self.original["assets/data/balance_ai_openai.json"])

    def test_absent_optional_controllers_are_not_invented(self) -> None:
        for name in ("auto_balance_loop.json", "balance_ai_openai.json"):
            (self.data / name).unlink()
        self.original = tree_bytes(self.source)
        folder, metadata = self.prepare()
        for name in ("auto_balance_loop.json", "balance_ai_openai.json"):
            self.assertFalse((folder / "assets/data" / name).exists())
            change = next(item for item in metadata["configuration_changes"]
                          if item["path"] == "assets/data/" + name)
            self.assertFalse(change["present"])

    def test_existing_output_is_rejected_without_touching_it(self) -> None:
        folder = self.root / "fixture"
        folder.mkdir()
        (folder / "keep.bin").write_bytes(b"keep")
        before = tree_bytes(folder)
        with self.assertRaises(AssertionError):
            ordinary.prepare_ordinary_fixture(folder, True, 1, 1)
        self.assertEqual(tree_bytes(folder), before)
        self.assertEqual(tree_bytes(self.source), self.original)

    def test_terminal_stop_cannot_request_multiple_runs(self) -> None:
        folder = self.root / "invalid"
        with self.assertRaises(AssertionError):
            ordinary.prepare_ordinary_fixture(folder, True, 1, 1, 2)
        self.assertFalse(folder.exists())

    def test_invalid_numeric_controls_are_rejected_before_creating_output(self) -> None:
        for seed, delay, runs in ((-1, 1, 1), (0x100000000, 1, 1), (1, 0, 1),
                                  (1, 1, 0), (True, 1, 1), (1.5, 1, 1),
                                  (1, False, 1), (1, 1.5, 1), (1, 1, False), (1, 1, 1.5)):
            with self.subTest(seed=seed, delay=delay, runs=runs):
                folder = self.root / "invalid"
                with self.assertRaises(AssertionError):
                    ordinary.prepare_ordinary_fixture(folder, False, seed, delay, runs)
                self.assertFalse(folder.exists())

    def test_stop_control_requires_a_boolean(self) -> None:
        for stop in (0, 1, "false", None):
            with self.subTest(stop=stop):
                folder = self.root / "invalid"
                with self.assertRaises(AssertionError):
                    ordinary.prepare_ordinary_fixture(folder, stop, 1, 1)
                self.assertFalse(folder.exists())

    def test_output_cannot_recurse_into_source_assets_or_shaders(self) -> None:
        for folder in (self.source, self.source / "assets/fixture", self.source / "shader/fixture"):
            with self.subTest(folder=folder), self.assertRaises(AssertionError):
                ordinary.prepare_ordinary_fixture(folder, False, 1, 1)
        self.assertEqual(tree_bytes(self.source), self.original)

    def test_metadata_tracks_uint32_seed_progression_and_current_source_evidence(self) -> None:
        _, metadata = self.prepare(stop_at_result=False, seed=0xFFFFFFFF, maximum_runs=2)
        self.assertEqual(metadata["expected_run_seeds"], [0xFFFFFFFF, 0])
        tools = Path(__file__).resolve().parent
        for evidence in metadata["external_automation"]["evidence"].values():
            path = tools.parent / evidence["source"]
            self.assertEqual(evidence["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertGreaterEqual(evidence["line"], 1)
            self.assertLessEqual(evidence["line"], len(path.read_text(encoding="utf-8-sig").splitlines()))

    def test_default_ai_entry_point_fails_before_planning_or_http_when_config_is_missing(self) -> None:
        folder, _ = self.prepare()
        paths = [folder / name for name in ("targets.json", "policy.json", "enemies.json")]
        for path in paths:
            path.write_text("{}\n", encoding="utf-8")
        argv = ["generate_balance_plan_with_ai.py", "--targets", str(paths[0]),
                "--policy", str(paths[1]), "--enemies", str(paths[2])]
        output = StringIO()
        previous = Path.cwd()
        try:
            os.chdir(folder)
            with patch.object(sys, "argv", argv), redirect_stdout(output), \
                    patch.object(ai_tool, "build_adjustment_plan", side_effect=AssertionError("must not plan")) as plan, \
                    patch.object(ai_tool, "call_openai", side_effect=AssertionError("must not call AI")) as ai, \
                    patch.object(ai_tool.urllib.request, "urlopen", side_effect=AssertionError("must not use HTTP")) as http:
                self.assertEqual(ai_tool.main(), 1)
        finally:
            os.chdir(previous)
        plan.assert_not_called()
        ai.assert_not_called()
        http.assert_not_called()
        self.assertIn("Failed to generate AI adjustment plan:", output.getvalue())
        self.assertIn("balance_ai_openai.json", output.getvalue())
        self.assertEqual(tree_bytes(self.source), self.original)


class FakeClock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


def frame(scene: str, seed: int, *, enabled: bool = True, stop: bool = False,
          validation: bool = False) -> dict:
    return {"scene": scene, "autoplay_enabled": enabled, "autoplay_stop_after_current_run": stop,
            "balance_validation": {"enabled": validation, "run_seed": seed}}


class ObservedFrames:
    """A bounded stream of published states, independent of gameplay decisions."""
    def __init__(self, clock: FakeClock, states: list, *, case: str = "terminal_stop", seed: int = 10,
                 runs: int = 1, deadline: float = 10.0) -> None:
        self.clock = clock
        self.states = [item if isinstance(item, BaseException) else deepcopy(item) for item in states]
        self.options = SimpleNamespace(case=case, seed=seed, expected_runs=runs)
        self.deadline = deadline
        self.commands: list = []
        self.accepted: list[dict] = []
        self.latest: dict | None = None
        self.read_count = 0
        self.store = SimpleNamespace(submit_command=Mock(side_effect=AssertionError("read-only observation")))
        self.queue_layout = Mock(side_effect=AssertionError("ordinary data must not receive layouts"))

    def read_state(self) -> dict | None:
        self.read_count += 1
        if self.states:
            item = self.states.pop(0)
            if isinstance(item, BaseException):
                raise item
            self.latest = item
        return self.latest

    def wait_for(self, name, predicate) -> dict:
        while self.clock.monotonic() < self.deadline:
            current = self.read_state()
            if current is not None and predicate(current):
                self.accepted.append(deepcopy(current))
                return current
            self.clock.sleep(0.05)
        raise TimeoutError("Synthetic observation deadline expired: " + name)


class OrdinaryObservationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.clock = FakeClock()
        clock_patch = patch.object(ordinary, "time", self.clock)
        clock_patch.start()
        self.addCleanup(clock_patch.stop)

    def test_terminal_stop_observes_result_without_gameplay_commands(self) -> None:
        driver = ObservedFrames(self.clock, [None, frame("stage_select", 10, stop=True),
                                frame("battle", 10, stop=True), frame("result", 10, enabled=False)])
        terminal = ordinary.run_ordinary(driver)
        self.assertEqual(terminal, {"scene": "result", "autoplay_enabled": False, "stop_after_current_run": False})
        self.assertGreaterEqual(self.clock.now, 1.0)
        self.assertEqual(driver.commands, [])
        driver.store.submit_command.assert_not_called()
        driver.queue_layout.assert_not_called()

    def test_multirun_waits_for_result_exit_and_uses_each_native_seed(self) -> None:
        driver = ObservedFrames(self.clock, [frame("stage_select", 0xFFFFFFFF), frame("result", 0xFFFFFFFF),
            frame("result", 0xFFFFFFFF), frame("title", 0xFFFFFFFF), frame("stage_select", 0xFFFFFFFF),
            frame("stage_select", 0), frame("result", 0), frame("title", 0, enabled=False)],
            case="maximum_runs", seed=0xFFFFFFFF, runs=2)
        terminal = ordinary.run_ordinary(driver)
        self.assertEqual(terminal["scene"], "title")
        self.assertEqual([state["scene"] for state in driver.accepted],
                         ["stage_select", "result", "title", "stage_select", "result", "title"])
        started = [state["balance_validation"]["run_seed"] for state in driver.accepted
                   if state["scene"] == "stage_select"]
        self.assertEqual(started, [0xFFFFFFFF, 0])
        self.assertFalse(driver.commands)
        driver.store.submit_command.assert_not_called()
        driver.queue_layout.assert_not_called()

    def test_repeated_previous_result_cannot_count_as_a_second_run(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10)],
                                case="maximum_runs", runs=2)
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_second_run_with_unchanged_seed_is_incomplete(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10), frame("title", 10),
                                frame("battle", 10), frame("result", 10)], case="maximum_runs", runs=2)
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_validation_override_state_does_not_count_as_ordinary_start(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10, validation=True)])
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_incomplete_gameplay_propagates_timeout(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10, stop=True)])
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_process_exit_is_not_relabelled_as_a_timeout(self) -> None:
        process_exit = RuntimeError("owned process exited unexpectedly")
        driver = ObservedFrames(self.clock, [frame("battle", 10), process_exit])
        with self.assertRaises(RuntimeError) as raised:
            ordinary.run_ordinary(driver)
        self.assertIs(raised.exception, process_exit)

    def test_uncleared_stop_request_never_counts_as_terminal_completion(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10, enabled=False, stop=True)])
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_maximum_runs_cannot_stop_at_result(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10, enabled=False)],
                                case="maximum_runs")
        with self.assertRaises(AssertionError):
            ordinary.run_ordinary(driver)

    def test_terminal_must_remain_stopped_for_full_observation_interval(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10, enabled=False),
            frame("result", 10, enabled=False), frame("result", 10, enabled=True)])
        with self.assertRaises(AssertionError):
            ordinary.run_ordinary(driver)

    def test_deadline_cannot_silently_shorten_terminal_observation(self) -> None:
        driver = ObservedFrames(self.clock, [frame("battle", 10), frame("result", 10, enabled=False)], deadline=0.5)
        with self.assertRaises(TimeoutError):
            ordinary.run_ordinary(driver)

    def test_existing_gameplay_command_invalidates_read_only_observation(self) -> None:
        driver = ObservedFrames(self.clock, [])
        driver.commands.append({"action": "set_next_stage_layout"})
        with self.assertRaises(AssertionError):
            ordinary.run_ordinary(driver)
        self.assertEqual(driver.read_count, 0)


def completed_run(seed: int, result: str = "game_over", battles: int = 1) -> dict:
    return {"controller_type": "autoplay",
            "run_context": {"randomness": {"run_seed": seed}, "validation": {"enabled": False}},
            "run_result": {"result": result},
            "stages": [{"shots": [{"damage": 9000001, "ball": "native"}]} for _ in range(battles)]}


class OrdinaryLogTests(unittest.TestCase):
    def test_both_natural_results_allow_normal_variable_battle_counts(self) -> None:
        for result, battles in (("game_over", 1), ("completed", 7)):
            with self.subTest(result=result, battles=battles):
                ordinary.validate_ordinary_logs([completed_run(10, result, battles)], 10, 1)

    def test_run_seeds_progress_and_wrap_without_modifying_logs(self) -> None:
        runs = [completed_run(0xFFFFFFFF), completed_run(0, "completed")]
        original = deepcopy(runs)
        ordinary.validate_ordinary_logs(runs, 0xFFFFFFFF, 2)
        self.assertEqual(runs, original)

    def test_cleanup_and_invented_result_labels_are_rejected(self) -> None:
        for result in ("application_exit", "restarted", "run_clear", "victory", "", None):
            with self.subTest(result=result), self.assertRaises(AssertionError):
                ordinary.validate_ordinary_logs([completed_run(10, result)], 10, 1)

    def test_wrong_or_repeated_seed_is_rejected(self) -> None:
        for runs in ([completed_run(11)], [completed_run(10), completed_run(10)]):
            with self.subTest(runs=runs), self.assertRaises(AssertionError):
                ordinary.validate_ordinary_logs(runs, 10, len(runs))

    def test_only_cpp_autoplay_logs_without_validation_overrides_are_accepted(self) -> None:
        for field in ("human", "validation", "missing_validation"):
            run = completed_run(10)
            if field == "human":
                run["controller_type"] = "human"
            elif field == "validation":
                run["run_context"]["validation"]["enabled"] = True
            else:
                del run["run_context"]["validation"]
            with self.subTest(field=field), self.assertRaises(AssertionError):
                ordinary.validate_ordinary_logs([run], 10, 1)

    def test_incomplete_or_extra_run_sets_are_rejected(self) -> None:
        for runs, count in (([], 1), ([completed_run(10)], 2),
                            ([completed_run(10), completed_run(11)], 1)):
            with self.subTest(count=count, actual=len(runs)), self.assertRaises(AssertionError):
                ordinary.validate_ordinary_logs(runs, 10, count)

    def test_log_needs_native_battle_and_shot_evidence(self) -> None:
        for stages in ([], [{"shots": []}], [{"future": "no shots"}]):
            run = completed_run(10)
            run["stages"] = stages
            with self.subTest(stages=stages), self.assertRaises(AssertionError):
                ordinary.validate_ordinary_logs([run], 10, 1)


if __name__ == "__main__":
    unittest.main()
