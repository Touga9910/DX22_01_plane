"""Check copied fixture boundaries and information-preserving normalization.

All assets are tiny synthetic files in TemporaryDirectory. SOURCE_ROOT is patched
only inside these tests; no installed game assets, processes or builds are used.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from autoplay_validation import fixtures
from autoplay_validation.normalization import normalize_run


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8-sig")


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def tree_bytes(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in root.rglob("*") if path.is_file()}


class AutoplayFixtureTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "synthetic_source"
        self.assets = self.source / "assets"
        self.data = self.assets / "data"
        self.autoplay = {
            "enabled": False, "restart_after_game_over": False,
            "stop_after_current_run": False, "max_runs": 0,
            "decision_delay_frames": 20, "random_seed": 123,
            "min_shot_power": 4.0, "max_shot_power": 8.0,
            "aim_jitter_degrees": 1.5,
            "future_policy": {"name": "残す", "weights": [3, 1], "optional": None},
        }
        self.validation = {
            "enabled": False, "random_seed": 456, "random_seeds": [456, 789],
            "experiment_id": "original", "endurance_mode": True,
            "fixed_stage_schedule": True, "variants": [{"id": "original"}],
            "schema_version": 17, "seed_suites": {"holdout": [11, 22]},
            "future_settings": {"enabled": True, "values": [False, None, 7]},
        }
        self.enemies = {
            "schema_version": 9, "future_catalog": {"keep": "yes"},
            "enemies": [
                {"id": "enemy_normal", "canCharge": True, "display_name": "normal",
                 "status": {"maxHp": 30, "attack": 4, "defense": 2, "mass": 2.5,
                            "friction": 0.07, "radius": 2.0,
                            "future_ability": {"enabled": True, "stages": [2, 1]}}},
                {"id": "enemy_custom", "canCharge": True,
                 "status": {"maxHp": 88, "attack": 9, "future": ["untouched"]}},
                {"id": "enemy_tank", "canCharge": True, "future_behavior": "keep",
                 "status": {"maxHp": 60, "attack": 8, "defense": 3, "mass": 5.0,
                            "friction": 0.04, "radius": 3.0, "restitution": 0.8}},
            ],
        }
        write_json(self.data / "balance_autoplay.json", self.autoplay)
        write_json(self.data / "balance_validation.json", self.validation)
        write_json(self.data / "enemy_data.json", self.enemies)
        write_json(self.data / "game_mcp_bridge.json", {"enabled": False, "source_only": "unchanged"})
        write_json(self.data / "unrelated.json", {"future": [3, 2, 1], "amount": 9000001})
        (self.assets / "texture").mkdir()
        (self.assets / "texture/cue.bin").write_bytes(b"\x00fake texture\xff")
        (self.source / "shader/include").mkdir(parents=True)
        (self.source / "shader/main.hlsl").write_text("// synthetic shader\n", encoding="utf-8")
        (self.source / "shader/include/common.hlsl").write_text("// include\n", encoding="utf-8")
        (self.source / "test_shader.cso").write_bytes(b"\x00synthetic cso\xff")
        self.original_source = tree_bytes(self.source)
        source_patch = patch.object(fixtures, "SOURCE_ROOT", self.source)
        source_patch.start()
        self.addCleanup(source_patch.stop)

    def prepare(self, name: str = "prepared", **kwargs: object) -> tuple[Path, dict]:
        folder = self.root / name
        arguments = {"stop_at_result": True, "seed": 20261116, "decision_frames": 150, **kwargs}
        result = fixtures.prepare_fixture(folder, **arguments)
        self.assertEqual(tree_bytes(self.source), self.original_source)
        return folder, result

    def test_configuration_changes_are_confined_to_the_copy(self) -> None:
        folder, metadata = self.prepare()
        autoplay = read_json(folder / "assets/data/balance_autoplay.json")
        self.assertEqual(autoplay["random_seed"], 20261116)
        self.assertTrue(autoplay["enabled"])
        self.assertTrue(autoplay["restart_after_game_over"])
        self.assertTrue(autoplay["stop_after_current_run"])
        self.assertEqual(autoplay["decision_delay_frames"], 150)
        self.assertEqual(autoplay["max_runs"], 1)
        validation = read_json(folder / "assets/data/balance_validation.json")
        self.assertEqual(validation["random_seeds"], [20261116])
        self.assertFalse(validation["endurance_mode"])
        self.assertFalse(validation["fixed_stage_schedule"])
        self.assertEqual(metadata["run_seed"], 20261116)
        self.assertEqual(metadata["maximum_runs"], 1)

    def test_untargeted_defaults_and_unknown_configuration_survive(self) -> None:
        folder, _ = self.prepare()
        autoplay = read_json(folder / "assets/data/balance_autoplay.json")
        for field in ("min_shot_power", "max_shot_power", "aim_jitter_degrees", "future_policy"):
            with self.subTest(field=field):
                self.assertEqual(autoplay[field], self.autoplay[field])
        validation = read_json(folder / "assets/data/balance_validation.json")
        for field in ("schema_version", "seed_suites", "future_settings"):
            with self.subTest(field=field):
                self.assertEqual(validation[field], self.validation[field])

    def test_absent_optional_shot_settings_keep_loader_default_semantics(self) -> None:
        minimal = {"future_policy": {"keep": True}}
        write_json(self.data / "balance_autoplay.json", minimal)
        self.original_source = tree_bytes(self.source)
        folder, _ = self.prepare()
        autoplay = read_json(folder / "assets/data/balance_autoplay.json")
        self.assertEqual(autoplay["future_policy"], minimal["future_policy"])
        for field in ("min_shot_power", "max_shot_power", "aim_jitter_degrees"):
            self.assertNotIn(field, autoplay)

    def test_enemy_fixture_preserves_order_other_enemies_and_extra_status(self) -> None:
        folder, _ = self.prepare()
        enemies = read_json(folder / "assets/data/enemy_data.json")
        self.assertEqual([enemy["id"] for enemy in enemies["enemies"]],
                         [enemy["id"] for enemy in self.enemies["enemies"]])
        self.assertEqual(enemies["future_catalog"], self.enemies["future_catalog"])
        self.assertEqual(enemies["enemies"][1], self.enemies["enemies"][1])
        easy, fatal = enemies["enemies"][0], enemies["enemies"][2]
        self.assertEqual((easy["status"]["maxHp"], easy["status"]["attack"], easy["status"]["defense"]), (1, 0, 0))
        self.assertFalse(easy["canCharge"])
        self.assertEqual(easy["status"]["future_ability"], self.enemies["enemies"][0]["status"]["future_ability"])
        self.assertEqual(easy["status"]["mass"], 2.5)
        self.assertEqual(fatal["status"]["maxHp"], 999)
        self.assertEqual(fatal["status"]["attack"], 999)
        self.assertEqual(fatal["status"]["mass"], 1000.0)
        self.assertFalse(fatal["canCharge"])
        self.assertEqual(fatal["future_behavior"], "keep")
        self.assertEqual(fatal["status"]["radius"], 3.0)
        self.assertEqual(fatal["status"]["restitution"], 0.8)

    def test_unrelated_files_shaders_and_compiled_shaders_are_copied_verbatim(self) -> None:
        folder, _ = self.prepare()
        for relative in ("assets/data/unrelated.json", "assets/texture/cue.bin",
                         "shader/main.hlsl", "shader/include/common.hlsl", "test_shader.cso"):
            with self.subTest(path=relative):
                self.assertEqual((folder / relative).read_bytes(), (self.source / relative).read_bytes())
        (folder / "shader/main.hlsl").write_bytes(b"changed copied shader")
        (folder / "assets/texture/cue.bin").write_bytes(b"changed copied texture")
        self.assertEqual(tree_bytes(self.source), self.original_source)

    def test_fresh_output_gets_isolated_bridge_and_muted_window_settings(self) -> None:
        folder, _ = self.prepare()
        bridge = read_json(folder / "assets/data/game_mcp_bridge.json")
        self.assertTrue(bridge["enabled"])
        self.assertTrue(bridge["allow_write_actions"])
        self.assertEqual(bridge["bridge_directory"], "runtime/game_mcp")
        self.assertEqual(bridge["state_publish_interval_frames"], 1)
        self.assertEqual(bridge["command_poll_interval_frames"], 1)
        settings = read_json(folder / "saves/settings.json")
        self.assertFalse(settings["fullscreen"])
        self.assertEqual((settings["bgm_volume"], settings["se_volume"]), (0, 0))

    def test_configuration_fingerprints_bind_the_actual_copied_json_files(self) -> None:
        folder, metadata = self.prepare()
        expected = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in (folder / "assets/data").glob("*.json")}
        self.assertEqual(metadata["configuration_sha256"], expected)
        self.assertIn("unrelated.json", metadata["configuration_sha256"])

    def test_existing_directory_is_rejected_without_overwriting_anything(self) -> None:
        folder = self.root / "existing"
        folder.mkdir()
        (folder / "important.txt").write_bytes(b"keep me")
        before = tree_bytes(folder)
        with self.assertRaises(AssertionError):
            fixtures.prepare_fixture(folder, True, 123, 150)
        self.assertEqual(tree_bytes(folder), before)
        self.assertEqual(tree_bytes(self.source), self.original_source)

    def test_existing_file_is_rejected_without_overwriting_it(self) -> None:
        folder = self.root / "existing_file"
        folder.write_bytes(b"not a fixture directory")
        with self.assertRaises(AssertionError):
            fixtures.prepare_fixture(folder, True, 123, 150)
        self.assertEqual(folder.read_bytes(), b"not a fixture directory")

    def test_terminal_stop_rejects_multiple_runs_before_creating_output(self) -> None:
        folder = self.root / "invalid_terminal"
        with self.assertRaises(AssertionError):
            fixtures.prepare_fixture(folder, True, 123, 150, maximum_runs=2)
        self.assertFalse(folder.exists())
        self.assertEqual(tree_bytes(self.source), self.original_source)

    def test_maximum_runs_case_accepts_multiple_runs(self) -> None:
        folder, metadata = self.prepare(stop_at_result=False, maximum_runs=3)
        autoplay = read_json(folder / "assets/data/balance_autoplay.json")
        self.assertFalse(autoplay["stop_after_current_run"])
        self.assertEqual(autoplay["max_runs"], 3)
        self.assertEqual(metadata["maximum_runs"], 3)

    def test_uint32_seed_boundaries_and_one_frame_decisions_are_kept(self) -> None:
        for seed in (0, 0xFFFFFFFF):
            with self.subTest(seed=seed):
                folder, metadata = self.prepare(name=f"seed_{seed}", seed=seed, decision_frames=1)
                self.assertEqual(metadata["run_seed"], seed)
                self.assertEqual(read_json(folder / "assets/data/balance_autoplay.json")["random_seed"], seed)
                self.assertEqual(read_json(folder / "assets/data/balance_validation.json")["random_seeds"], [seed])

    def test_invalid_numeric_boundaries_leave_no_output(self) -> None:
        invalid = ({"seed": -1}, {"seed": 0x100000000}, {"decision_frames": 0}, {"maximum_runs": 0})
        for index, override in enumerate(invalid):
            with self.subTest(override=override):
                folder = self.root / f"invalid_{index}"
                arguments = {"stop_at_result": False, "seed": 123, "decision_frames": 150, **override}
                with self.assertRaises(AssertionError):
                    fixtures.prepare_fixture(folder, **arguments)
                self.assertFalse(folder.exists())
        self.assertEqual(tree_bytes(self.source), self.original_source)

    def test_alternate_assets_still_use_the_patched_source_shader_and_cso(self) -> None:
        alternate = self.root / "alternate_assets"
        alternate_data = alternate / "data"
        for name, value in (("balance_autoplay.json", self.autoplay),
                            ("balance_validation.json", self.validation), ("enemy_data.json", self.enemies)):
            write_json(alternate_data / name, value)
        (alternate / "alternate.bin").write_bytes(b"alternate assets")
        before = tree_bytes(alternate)
        folder, _ = self.prepare(asset_source=alternate)
        self.assertEqual((folder / "assets/alternate.bin").read_bytes(), b"alternate assets")
        self.assertFalse((folder / "assets/texture/cue.bin").exists())
        self.assertEqual((folder / "test_shader.cso").read_bytes(), (self.source / "test_shader.cso").read_bytes())
        self.assertEqual(tree_bytes(alternate), before)


class AutoplayNormalizationTests(unittest.TestCase):
    def test_clock_changes_are_ignored_without_dropping_fields(self) -> None:
        first = {"started_at": "first", "duration_ms": 10,
                 "shots": [{"fired_at": "first shot", "physics_processing_microseconds": 55}], "hp": 50}
        later = {"started_at": "later", "duration_ms": 999,
                 "shots": [{"fired_at": "later shot", "physics_processing_microseconds": 800}], "hp": 50}
        normalized = normalize_run(first)
        self.assertEqual(normalized, normalize_run(later))
        self.assertEqual(set(normalized), set(first))
        self.assertEqual(set(normalized["shots"][0]), set(first["shots"][0]))

    def test_clock_type_or_missing_field_remains_a_difference(self) -> None:
        self.assertNotEqual(normalize_run({"duration_ms": 1}), normalize_run({"duration_ms": 1.0}))
        self.assertNotEqual(normalize_run({"started_at": None}), normalize_run({"started_at": ""}))
        self.assertNotEqual(normalize_run({"duration_ms": 1}), normalize_run({}))

    def test_relocated_pointers_compare_equal_but_identity_links_are_kept(self) -> None:
        def run(pointer: int) -> dict:
            return {"shots": [{"target_id": pointer, "source_key": str(pointer)}],
                    "actual_prevention_by_target": {str(pointer): {"amount": 15}}, "hp": 50}
        first, relocated = run(9_000_000), run(50_000_000)
        normalized = normalize_run(first)
        self.assertEqual(normalized, normalize_run(relocated))
        target = normalized["shots"][0]["target_id"]
        self.assertEqual(target, normalized["shots"][0]["source_key"])
        self.assertIn(target, normalized["actual_prevention_by_target"])
        self.assertEqual(normalized["actual_prevention_by_target"][target]["amount"], 15)

    def test_distinct_target_vs_repeated_target_stays_distinct(self) -> None:
        same_target = {"hits": [{"target_key": 9_000_000}, {"target_key": 9_000_000}]}
        two_targets = {"hits": [{"target_key": 50_000_000}, {"target_key": 50_000_001}]}
        self.assertNotEqual(normalize_run(same_target), normalize_run(two_targets))

    def test_small_semantic_ids_boolean_and_noninteger_values_are_not_pointers(self) -> None:
        values = [0, 1, 1_000_000, True, False, "enemy:0", "1000000", 9_000_000.5]
        for value in values:
            with self.subTest(value=value):
                actual = normalize_run({"target_id": value})["target_id"]
                self.assertEqual(actual, value)
                self.assertIs(type(actual), type(value))
        self.assertNotEqual(normalize_run({"target_id": 1_000_001})["target_id"], 1_000_001)

    def test_large_numbers_outside_pointer_fields_are_preserved(self) -> None:
        run = {"hp": 9_000_001, "seed": 0xFFFFFFFF, "damage": 2**60,
               "selected_index": 9_000_002, "metadata": {"9000000": "retain this key"}}
        self.assertEqual(normalize_run(run), run)

    def test_gameplay_scalar_changes_survive_clock_and_pointer_normalization(self) -> None:
        baseline = {"hp": 50, "run_seed": 20261116, "selected_ball": "player_heavy",
                    "offer_index": 1, "route_index": 0, "damage": 9_000_001,
                    "target_id": 40_000_000, "duration_ms": 10}
        changes = {"hp": 49, "run_seed": 20261117, "selected_ball": "player_standard",
                   "offer_index": 2, "route_index": 1, "damage": 9_000_002}
        for field, value in changes.items():
            with self.subTest(field=field):
                changed = {**baseline, field: value, "duration_ms": 900, "target_id": 50_000_000}
                self.assertNotEqual(normalize_run(baseline), normalize_run(changed))

    def test_choice_and_event_array_order_is_never_sorted_away(self) -> None:
        first = {"choices": ["heavy", "pierce"], "events": [{"damage": 1}, {"damage": 2}]}
        changed_choices = {**first, "choices": list(reversed(first["choices"]))}
        changed_events = {**first, "events": list(reversed(first["events"]))}
        self.assertNotEqual(normalize_run(first), normalize_run(changed_choices))
        self.assertNotEqual(normalize_run(first), normalize_run(changed_events))

    def test_unlisted_timing_and_unknown_fields_are_not_discarded(self) -> None:
        first = {"frame_time_ms": 10, "future_telemetry": {"value": 9_000_000, "enabled": True}}
        self.assertNotEqual(normalize_run(first), normalize_run({**first, "frame_time_ms": 11}))
        changed = deepcopy(first)
        changed["future_telemetry"]["value"] += 1
        self.assertNotEqual(normalize_run(first), normalize_run(changed))

    def test_normalization_does_not_rewrite_the_input(self) -> None:
        original = {"duration_ms": 15, "shots": [{"target_id": 90_000_000}],
                    "damage_by_target": {"90000000": {"amount": 3}}}
        before = deepcopy(original)
        normalized = normalize_run(original)
        self.assertEqual(original, before)
        self.assertIsNot(normalized, original)
        self.assertIsNot(normalized["shots"], original["shots"])
        normalized["shots"][0]["target_id"] = "changed normalized output"
        self.assertEqual(original, before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
