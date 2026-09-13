from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from analyze_balance_logs import (
    RunFilters,
    apply_boss_revaluation_gate,
    calculate_run_report,
    calculate_stage_report,
    configuration_fingerprint,
    select_run_logs,
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

    def test_stage_type_target_overrides_legacy_default(self) -> None:
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
                    "min": 1.0, "max": 7.0, "tolerance": 5.0,
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
            "stage_type_targets": {
                "boss": {
                    "metrics": {
                        "median_shots": {"min": 7.0, "max": 12.0},
                    },
                },
            },
        }
        records = [{
            "controller_type": "mcp",
            "controller_profile": "intermediate",
            "validation_enabled": True,
            "stage": {
                "stage_type": "boss",
                "stage_context": {},
                "shots": [{"collision_effect": 1}],
            },
            "result": {
                "result": "clear",
                "total_shots": 5,
                "remaining_hp_ratio": 0.5,
            },
        }]

        report = calculate_stage_report("boss_001", 3, records, targets)

        self.assertEqual(
            report["metric_status"]["median_shots"]["target_min"],
            7.0,
        )
        self.assertEqual(
            report["metric_status"]["median_shots"]["status"],
            "below_target",
        )
        self.assertTrue(
            report["target_scope"]["used_stage_type_override"]
        )

    def test_run_report_uses_terminal_runs_for_progression(self) -> None:
        metric = {
            "min": 0.0,
            "max": 1.0,
            "tolerance": 1.0,
            "weight": 0.5,
            "hard_gate": True,
            "difficulty_direction": "easier",
        }
        targets = {
            "minimum_sample_count": 3,
            "adjustment_score_threshold": 60.0,
            "balanced_score_threshold": 80.0,
            "run_targets": {
                "minimum_sample_count": 3,
                "default_profile": "intermediate",
                "metrics": {
                    "first_boss_reach_rate": dict(metric),
                    "terminal_run_rate": {
                        **metric,
                        "min": 0.95,
                        "difficulty_direction": "neutral",
                    },
                },
            },
        }

        def loaded_run(
            result: str,
            progress: int,
            boss_results: list[str],
        ) -> tuple[Path, dict, dict]:
            stages = [
                {
                    "stage_type": "boss",
                    "stage_result": {"result": boss_result},
                }
                for boss_result in boss_results
            ]
            return (
                Path(f"run_{result}_{progress}.json"),
                {
                    "run_result": {
                        "result": result,
                        "reached_stage_index": progress,
                    },
                    "stages": stages,
                },
                {"controller_profile": "intermediate"},
            )

        runs = [
            loaded_run("game_over", 10, ["clear"]),
            loaded_run("validation_complete", 20, ["clear", "clear"]),
            loaded_run("game_over", 5, []),
            loaded_run("application_exit", 12, ["clear"]),
        ]

        report = calculate_run_report(runs, targets)

        self.assertEqual(report["sample_count"], 4)
        self.assertEqual(report["balance_sample_count"], 3)
        self.assertEqual(report["excluded_incomplete_run_count"], 1)
        self.assertEqual(
            report["metrics"]["first_boss_reach_rate"],
            0.6667,
        )
        self.assertEqual(report["metrics"]["median_reached_progress"], 10.0)
        self.assertEqual(report["metrics"]["terminal_run_rate"], 0.75)
        self.assertEqual(
            report["milestones"]["second_boss_reached_count"],
            1,
        )


class RunSelectionTests(unittest.TestCase):
    @staticmethod
    def make_run(
        config_hash: str,
        *,
        profile: str = "intermediate",
        build_profile: str = "standard",
        variant: str = "dda_off",
        dda_enabled: bool = False,
    ) -> dict:
        return {
            "controller_type": "mcp",
            "controller": {
                "type": "mcp",
                "profile": profile,
                "build_profile": build_profile,
                "build_profile_settings_hash": f"hash-{build_profile}",
            },
            "run_context": {
                "controller_profile": profile,
                "build_profile": build_profile,
                "build_profile_settings_hash": f"hash-{build_profile}",
                "dynamic_balance_enabled_at_start": dda_enabled,
                "validation": {
                    "enabled": True,
                    "experiment_id": "paired_test",
                    "variant_id": variant,
                },
            },
            "configuration": {
                "files": [
                    {
                        "path": "assets/data/enemy_data.json",
                        "exists": True,
                        "fnv1a64": config_hash,
                    },
                    {
                        "path": "assets/data/player_status.json",
                        "exists": True,
                        "fnv1a64": "player-status",
                    },
                ],
            },
            "stages": [],
        }

    def test_configuration_fingerprint_ignores_file_order(self) -> None:
        run = self.make_run("enemy-status")
        reversed_run = json.loads(json.dumps(run))
        reversed_run["configuration"]["files"].reverse()

        self.assertEqual(
            configuration_fingerprint(run),
            configuration_fingerprint(reversed_run),
        )

    def test_selects_requested_comparable_cohort(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            log_directory = Path(temporary_directory)
            runs = [
                self.make_run("config-a", variant="dda_off"),
                self.make_run(
                    "config-a",
                    variant="dda_on",
                    dda_enabled=True,
                ),
                self.make_run(
                    "config-b",
                    profile="advanced",
                    build_profile="heavy",
                    variant="dda_on",
                    dda_enabled=True,
                ),
            ]
            for index, run in enumerate(runs, start=1):
                path = log_directory / f"run_{index:03d}.json"
                path.write_text(json.dumps(run), encoding="utf-8")

            selected, summary = select_run_logs(
                log_directory,
                RunFilters(
                    controller_types=("mcp",),
                    controller_profiles=("intermediate",),
                    build_profiles=("standard",),
                    experiment_ids=("paired_test",),
                    validation_variants=("dda_on",),
                    dynamic_balance_enabled=True,
                ),
            )

            self.assertEqual(len(selected), 1)
            self.assertEqual(summary["matched_file_count"], 1)
            self.assertEqual(summary["excluded_file_count"], 2)
            self.assertEqual(
                summary["excluded_by_condition"]["controller_profile"],
                1,
            )
            self.assertEqual(
                summary["selected_build_profiles"],
                {"standard": 1},
            )
            self.assertEqual(
                summary["selected_build_profile_settings_hashes"],
                {"hash-standard": 1},
            )
            self.assertEqual(
                summary["excluded_by_condition"]["validation_variant"],
                1,
            )

    def test_latest_configuration_uses_other_filters_first(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            log_directory = Path(temporary_directory)
            older_run = self.make_run("config-old")
            matching_latest_run = self.make_run("config-matching-latest")
            unrelated_latest_run = self.make_run(
                "config-unrelated-latest",
                profile="advanced",
            )
            (log_directory / "run_001.json").write_text(
                json.dumps(older_run),
                encoding="utf-8",
            )
            (log_directory / "run_002.json").write_text(
                json.dumps(matching_latest_run),
                encoding="utf-8",
            )
            (log_directory / "run_003.json").write_text(
                json.dumps(unrelated_latest_run),
                encoding="utf-8",
            )

            selected, summary = select_run_logs(
                log_directory,
                RunFilters(controller_profiles=("intermediate",)),
                latest_configuration=True,
            )

            latest_fingerprint = configuration_fingerprint(
                matching_latest_run
            )
            self.assertEqual(len(selected), 1)
            self.assertEqual(
                summary["resolved_latest_configuration_fingerprint"],
                latest_fingerprint,
            )
            self.assertEqual(
                summary["selected_configuration_fingerprints"],
                {latest_fingerprint: 1},
            )


if __name__ == "__main__":
    unittest.main()
