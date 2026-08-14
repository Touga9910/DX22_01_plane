from __future__ import annotations

import unittest

from analyze_balance_logs import (
    apply_boss_revaluation_gate,
    calculate_stage_report,
)


class BalanceMetricGateTests(unittest.TestCase):
    def test_high_weighted_score_cannot_hide_hard_gate_failure(self) -> None:
        targets = {
            "minimum_sample_count": 1,
            "adjustment_score_threshold": 60.0,
            "balanced_score_threshold": 80.0,
            "metrics": {
                "clear_rate": {
                    "min": 0.0, "max": 1.0, "tolerance": 1.0,
                    "weight": 0.4, "hard_gate": True,
                },
                "median_shots": {
                    "min": 4.0, "max": 7.0, "tolerance": 100.0,
                    "weight": 0.25, "hard_gate": True,
                },
                "average_remaining_hp_ratio": {
                    "min": 0.0, "max": 1.0, "tolerance": 1.0,
                    "weight": 0.15, "hard_gate": True,
                },
                "no_hit_shot_rate": {
                    "min": 0.0, "max": 1.0, "tolerance": 1.0,
                    "weight": 0.1, "hard_gate": True,
                },
                "average_collision_effect": {
                    "min": 0.0, "max": 10.0, "tolerance": 10.0,
                    "weight": 0.1, "hard_gate": True,
                },
            },
        }
        records = [{
            "controller_type": "mcp",
            "controller_profile": "intermediate",
            "validation_enabled": True,
            "stage": {
                "stage_context": {
                    "baseline_difficulty": {"profile": "normal"},
                    "assist_mode": {"enabled": False, "applied_level": 0},
                },
                "shots": [{"collision_effect": 1}],
            },
            "result": {
                "result": "clear",
                "total_shots": 1,
                "remaining_hp_ratio": 0.5,
            },
        }]

        report = calculate_stage_report(
            "normal_001", 1, records, targets
        )

        self.assertFalse(report["hard_gate"]["passed"])
        self.assertIn("median_shots", report["out_of_range_metrics"])
        self.assertNotEqual(report["judgement"], "balanced")
        self.assertEqual(
            report["condition_breakdown"]["controller_type"],
            {"mcp": 1},
        )

    def test_boss_adjustment_is_locked_until_reach_target(self) -> None:
        targets = {
            "boss_revaluation": {"minimum_reached_runs": 2},
        }
        grouped = {
            ("boss_001", 3): [{
                "run_id": "run_1",
                "stage": {"stage_type": "boss"},
            }],
        }
        stage_reports = [{
            "stage_id": "boss_001",
            "stage_type": "boss",
            "judgement": "too_easy",
            "adjustment_required": True,
            "adjustment_priority": 50.0,
        }]

        summary = apply_boss_revaluation_gate(
            stage_reports,
            grouped,
            analyzed_file_count=15,
            targets=targets,
        )

        self.assertFalse(summary["eligible"])
        self.assertTrue(summary["strength_adjustment_locked"])
        self.assertEqual(
            stage_reports[0]["judgement"],
            "insufficient_boss_reach",
        )
        self.assertFalse(stage_reports[0]["adjustment_required"])


if __name__ == "__main__":
    unittest.main()
