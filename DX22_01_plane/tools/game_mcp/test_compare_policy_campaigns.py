import json
import hashlib
from pathlib import Path
import tempfile
import unittest

from compare_policy_campaigns import BUILDS, compare


class PolicyCampaignTests(unittest.TestCase):
    def campaign(self, directory, *, signature="same", wins=True, seed=1001):
        directory.mkdir()
        batches = []
        for build in BUILDS:
            batch = directory / build
            batch.mkdir()
            batches.append(str(batch))
            source = batch / "run.json"
            raw = {
                "balance_version": "B0014",
                "run_context": {"run_combat_mode": "legacy", "mcp_shot_policy": "standard",
                                "controller_profile": "intermediate", "dynamic_balance_enabled_at_start": False},
                "stages": [{"stage_id": "boss", "stage_type": "boss", "stage_index": 1,
                            "stage_result": {"result": "clear" if wins else "game_over",
                                             "player_damage_taken": 3, "total_shots": 1},
                            "shots": [{"ball_id": "player_standard", "enemy_enemy_hit_count": 2}]}],
                "events": [],
            }
            source.write_text(json.dumps(raw), encoding="utf-8")
            record = {"source_log": str(source), "normal_end": True, "configuration_signature": signature,
                      "build_profile": build, "run_seed": seed, "final_boss_defeated": wins, "final_hp": 47 if wins else 0}
            (batch / "all_runs.jsonl").write_text(json.dumps(record) + "\n", encoding="utf-8")
        (directory / "analysis.json").write_text(json.dumps({"batches": batches}), encoding="utf-8")
        snapshot = directory / "policy_snapshot/tools/game_mcp"
        snapshot.mkdir(parents=True)
        (snapshot / "route_decision.py").write_text(str(wins), encoding="utf-8")
        manifest = {"exe_sha256": "exe", "profile_progress_sha256": "save", "player_level": "intermediate",
                    "validation_variant": "fixed", "policy_source_hashes": {
                        "route_decision.py": hashlib.sha256(str(wins).encode()).hexdigest()}}
        (directory / "overnight_status.json").write_text(json.dumps(manifest), encoding="utf-8")

    def test_pairs_include_regressions_and_actual_chain_hits(self):
        with tempfile.TemporaryDirectory() as folder:
            before, after = Path(folder) / "before", Path(folder) / "after"
            self.campaign(before)
            self.campaign(after, wins=False)
            report = compare(before, after, target=1)
            self.assertTrue(Path(report).exists())
            result = json.loads((after / "policy_comparison.json").read_text(encoding="utf-8"))
            self.assertEqual(result["paired_runs"], 5)
            self.assertTrue(all(row["win_to_loss"] == 1 for row in result["builds"]))
            self.assertEqual(result["samples_before"][0]["enemy_enemy_hits"], 2)

    def test_changed_game_rules_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            before, after = Path(folder) / "before", Path(folder) / "after"
            self.campaign(before)
            self.campaign(after, signature="different")
            with self.assertRaisesRegex(ValueError, "configuration"):
                compare(before, after, target=1)

    def test_nondefault_paired_seeds_are_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            before, after = Path(folder) / "before", Path(folder) / "after"
            self.campaign(before, seed=2026112101)
            self.campaign(after, seed=2026112101)
            compare(before, after, target=1)
            result = json.loads((after / "policy_comparison.json").read_text(encoding="utf-8"))
            self.assertEqual({r["seed"] for r in result["pairs"]}, {2026112101})

    def test_missing_pair_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            before, after = Path(folder) / "before", Path(folder) / "after"
            self.campaign(before)
            self.campaign(after)
            (after / BUILDS[0] / "all_runs.jsonl").write_text("", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "seed/build"):
                compare(before, after, target=1)

    def test_changed_snapshot_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            before, after = Path(folder) / "before", Path(folder) / "after"
            self.campaign(before)
            self.campaign(after)
            (after / "policy_snapshot/tools/game_mcp/route_decision.py").write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Frozen policy"):
                compare(before, after, target=1)


if __name__ == "__main__":
    unittest.main()
