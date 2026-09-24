from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from analyze_balance_logs import calculate_stage_report
from balance_diagnostics import calculate_balance_diagnostics
from balance_playtest_feedback import aggregate_feedback, write_feedback
from balance_statistics import classify_interval, wilson_interval


def stage_targets() -> dict:
    common = {
        "min": 0.0,
        "max": 100.0,
        "tolerance": 100.0,
        "weight": 0.0,
        "hard_gate": False,
    }
    return {
        "minimum_sample_count": 20,
        "adjustment_score_threshold": 60.0,
        "balanced_score_threshold": 80.0,
        "statistical_decision": {
            "enabled": True,
            "confidence_level": 0.95,
            "additional_batch_size": 10,
            "maximum_sample_count": 60,
        },
        "metrics": {
            "clear_rate": {
                "min": 0.80,
                "max": 0.95,
                "tolerance": 0.5,
                "weight": 1.0,
                "hard_gate": True,
            },
            "median_shots": dict(common),
            "average_remaining_hp_ratio": {
                **common,
                "max": 1.0,
            },
            "no_hit_shot_rate": {**common, "max": 1.0},
            "average_collision_effect": dict(common),
        },
    }


class StatisticalDecisionTests(unittest.TestCase):
    def test_wilson_interval_reports_uncertain_target_overlap(self) -> None:
        interval = wilson_interval(18, 20, 0.95)

        self.assertEqual(classify_interval(interval, 0.80, 0.95), "inconclusive")

    def test_stage_report_requests_more_samples_when_hard_gate_is_uncertain(self) -> None:
        records = []
        for index in range(20):
            cleared = index < 18
            records.append(
                {
                    "controller_type": "mcp",
                    "controller_profile": "intermediate",
                    "build_profile": "standard",
                    "validation_enabled": True,
                    "stage": {
                        "stage_type": "normal",
                        "stage_context": {},
                        "shots": [{"collision_effect": 1}],
                    },
                    "result": {
                        "result": "clear" if cleared else "game_over",
                        "total_shots": 4,
                        "remaining_hp_ratio": 0.5 if cleared else 0.0,
                    },
                }
            )

        report = calculate_stage_report("normal_001", 1, records, stage_targets())

        self.assertEqual(report["judgement"], "inconclusive")
        self.assertEqual(
            report["statistical_decision"]["additional_samples_recommended"],
            10,
        )
        self.assertIn(
            "clear_rate",
            report["hard_gate"]["inconclusive_metrics"],
        )


class DiagnosticTests(unittest.TestCase):
    def test_detects_pocket_failure_and_floor_spike(self) -> None:
        def stage(progress: int, result: str, damage: int, shots: int) -> dict:
            return {
                "stage_index": progress,
                "stage_type": "normal",
                "stage_context": {"progress": progress},
                "stage_start": {"player_max_hp": 10},
                "damage_events": (
                    [{"source": "pocket", "damage": damage}]
                    if damage
                    else []
                ),
                "stage_result": {
                    "result": result,
                    "total_shots": shots,
                    "player_max_hp": 10,
                    "player_damage_taken": damage,
                    "remaining_hp_ratio": 0.0 if result == "game_over" else 0.8,
                },
            }

        runs = []
        for index in range(5):
            run = {
                "run_id": f"run_{index}",
                "run_result": {"result": "game_over"},
                "stages": [
                    stage(1, "clear", 0, 3),
                    stage(2, "game_over", 4, 8),
                ],
                "events": [],
            }
            runs.append((Path(f"run_{index}.json"), run, {}))
        targets = {
            "metrics": {"median_shots": {"max": 5.0}},
            "diagnostics": {
                "failure_taxonomy": {
                    "burst_damage_hp_ratio": 0.35,
                    "long_battle_shot_multiplier": 1.5,
                },
                "difficulty_spikes": {"minimum_samples_per_floor": 5},
            },
        }

        report = calculate_balance_diagnostics(runs, targets)

        self.assertEqual(
            report["failure_taxonomy"]["primary_reason_counts"],
            {"player_pocket": 5},
        )
        self.assertTrue(report["difficulty_curve"]["detected_spikes"])


class HumanFeedbackTests(unittest.TestCase):
    def test_records_and_aggregates_human_feedback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_feedback(
                root,
                run_id="run_001",
                player_profile="intermediate",
                ratings={
                    "decision_clarity": 4,
                    "control_feel": 3,
                    "result_trust": 5,
                    "enjoyment": 4,
                },
                issue_tags=["damage_prediction", "damage_prediction"],
                note="予測値は理解できた。",
            )

            report = aggregate_feedback(root)

        self.assertEqual(report["feedback_count"], 1)
        self.assertEqual(report["linked_run_count"], 1)
        self.assertEqual(report["average_ratings"]["result_trust"], 5.0)
        self.assertEqual(report["issue_tags"], {"damage_prediction": 1})


if __name__ == "__main__":
    unittest.main()
