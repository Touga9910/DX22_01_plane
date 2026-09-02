from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from auto_balance_loop import (
    AutoBalanceLock,
    CONFIGURATION_PATHS,
    LockUnavailableError,
    assess_readiness,
    budget_failure,
    compare_validation_snapshots,
    create_configuration_backup,
    current_configuration_fingerprint,
    current_configuration_snapshot,
    detect_change_oscillation,
    evaluate_sample_quality,
    fnv1a64_file,
    projected_enemy_status_sha256,
    record_ai_usage,
    restore_configuration_backup,
    validation_cases,
    update_improvement_guard,
)
from adjust_balance_from_logs import apply_plan, file_sha256
from analyze_balance_logs import (
    RunFilters,
    configuration_fingerprint,
    select_run_logs,
)


class AutoBalanceLoopTests(unittest.TestCase):
    def test_fnv1a64_matches_game_implementation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "sample.bin"
            path.write_bytes(b"hello")

            exists, size, fingerprint = fnv1a64_file(path)

            self.assertTrue(exists)
            self.assertEqual(size, 5)
            self.assertEqual(fingerprint, "a430d84680aabd0b")

    def test_current_fingerprint_matches_logged_snapshot_format(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for index, relative_path in enumerate(CONFIGURATION_PATHS):
                path = root / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(f"config-{index}".encode("utf-8"))

            snapshot = current_configuration_snapshot(root)
            expected = configuration_fingerprint(
                {"configuration": snapshot}
            )

            self.assertEqual(
                current_configuration_fingerprint(root),
                expected,
            )
            self.assertEqual(len(snapshot["files"]), len(CONFIGURATION_PATHS))

    def test_readiness_collects_only_missing_current_cohort_runs(self) -> None:
        evaluation = {
            "run_balance": {
                "balance_sample_count": 7,
                "minimum_sample_count": 30,
                "judgement": "insufficient_data",
            },
            "guardrail_plan": {
                "changes": [],
                "stage_evaluations": [],
            },
        }

        readiness = assess_readiness(evaluation)

        self.assertEqual(readiness["status"], "collect")
        self.assertEqual(readiness["missing_runs"], 23)

    def test_balanced_requires_run_and_stage_targets(self) -> None:
        evaluation = {
            "run_balance": {
                "balance_sample_count": 30,
                "minimum_sample_count": 30,
                "judgement": "balanced",
            },
            "guardrail_plan": {
                "changes": [],
                "stage_evaluations": [
                    {"adjustment_required": False},
                ],
            },
        }

        self.assertEqual(assess_readiness(evaluation)["status"], "balanced")

    def test_direction_reversal_stops_before_apply(self) -> None:
        history = [
            {
                "changes": [
                    {
                        "target_type": "enemy",
                        "target_id": "enemy_001",
                        "field": "status.attack",
                        "before": 1,
                        "after": 2,
                    }
                ]
            }
        ]
        proposed = [
            {
                "target_type": "enemy",
                "target_id": "enemy_001",
                "field": "status.attack",
                "before": 2,
                "after": 1,
            }
        ]

        result = detect_change_oscillation(
            proposed,
            history,
            {
                "stop_on_direction_reversal": True,
                "reversal_lookback_iterations": 3,
                "maximum_same_direction_changes": 3,
            },
        )

        self.assertIsNotNone(result)
        self.assertEqual(result["type"], "direction_reversal")

    def test_repeated_same_direction_changes_have_a_limit(self) -> None:
        history = [
            {
                "changes": [
                    {
                        "target_type": "enemy",
                        "target_id": "enemy_001",
                        "field": "status.maxHp",
                        "before": value,
                        "after": value + 1,
                    }
                ]
            }
            for value in range(1, 4)
        ]
        proposed = [
            {
                "target_type": "enemy",
                "target_id": "enemy_001",
                "field": "status.maxHp",
                "before": 4,
                "after": 5,
            }
        ]

        result = detect_change_oscillation(
            proposed,
            history,
            {"maximum_same_direction_changes": 3},
        )

        self.assertIsNotNone(result)
        self.assertEqual(result["type"], "repeated_same_direction_limit")

    def test_projected_hash_detects_return_to_previous_enemy_data(self) -> None:
        original = {
            "enemies": [
                {
                    "id": "enemy_001",
                    "status": {"maxHp": 2, "attack": 1},
                }
            ]
        }
        raised = json.loads(json.dumps(original))
        raised["enemies"][0]["status"]["attack"] = 2
        reversal_plan = {
            "changes": [
                {
                    "target_id": "enemy_001",
                    "field": "status.attack",
                    "before": 2,
                    "after": 1,
                }
            ]
        }
        original_hash = projected_enemy_status_sha256(
            {"changes": []},
            original,
        )

        self.assertEqual(
            projected_enemy_status_sha256(reversal_plan, raised),
            original_hash,
        )

    def test_non_improving_score_stops_after_configured_count(self) -> None:
        state = {
            "last_scored_configuration_fingerprint": "",
            "non_improving_iterations": 1,
            "history": [
                {
                    "evaluation_before": {
                        "run_balance_score": 50.0,
                    }
                }
            ],
        }

        failure = update_improvement_guard(
            state,
            "new-fingerprint",
            50.2,
            {
                "minimum_score_improvement": 0.5,
                "maximum_non_improving_iterations": 2,
            },
        )

        self.assertIsNotNone(failure)
        self.assertEqual(state["non_improving_iterations"], 2)

    def test_sample_quality_rejects_seed_dominance(self) -> None:
        selected_runs = []
        for index in range(10):
            seed = 100 if index < 8 else 100 + index
            selected_runs.append(
                (
                    Path(f"run_{index}.json"),
                    {
                        "run_context": {
                            "randomness": {"run_seed": seed},
                        },
                        "run_result": {"duration_ms": 1000 + index},
                    },
                    {},
                )
            )

        result = evaluate_sample_quality(
            selected_runs,
            {"balance_sample_count": 9},
            {
                "minimum_unique_run_seeds": 3,
                "minimum_terminal_run_rate": 0.8,
                "maximum_single_seed_share": 0.5,
                "maximum_duration_outlier_rate": 0.2,
            },
        )

        self.assertFalse(result["passed"])
        self.assertIn(
            "single_seed_dominance",
            {issue["type"] for issue in result["issues"]},
        )

    def test_readiness_collects_more_runs_for_sample_quality(self) -> None:
        evaluation = {
            "run_balance": {
                "balance_sample_count": 30,
                "minimum_sample_count": 30,
                "judgement": "too_easy",
            },
            "sample_quality": {
                "passed": False,
                "additional_runs_needed": 4,
                "issues": [{"type": "insufficient_seed_diversity"}],
            },
            "guardrail_plan": {
                "changes": [{"target_id": "enemy_001"}],
                "stage_evaluations": [],
            },
        }

        readiness = assess_readiness(evaluation)

        self.assertEqual(readiness["status"], "collect")
        self.assertEqual(readiness["missing_runs"], 4)
        self.assertEqual(
            readiness["reason"],
            "sample_quality_requirements_not_met",
        )

    def test_process_lock_rejects_a_second_owner(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            lock_path = Path(temporary_directory) / "loop.lock"
            with AutoBalanceLock(lock_path):
                with self.assertRaises(LockUnavailableError):
                    with AutoBalanceLock(lock_path):
                        self.fail("A second process lock was acquired")

    def test_configuration_backup_restores_original_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for index, relative_name in enumerate(CONFIGURATION_PATHS):
                path = root / relative_name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    json.dumps({"value": index}),
                    encoding="utf-8",
                )
            original = current_configuration_fingerprint(root)
            backup = create_configuration_backup(
                root,
                root / "backups",
                original,
                1,
            )
            changed_path = root / CONFIGURATION_PATHS[1]
            changed_path.write_text('{"value": 999}', encoding="utf-8")
            self.assertNotEqual(current_configuration_fingerprint(root), original)

            restored = restore_configuration_backup(root, backup)

            self.assertEqual(restored, original)
            self.assertEqual(current_configuration_fingerprint(root), original)

    def test_e2e_apply_starts_new_fingerprint_with_zero_runs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            for index, relative_name in enumerate(CONFIGURATION_PATHS):
                path = root / relative_name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    json.dumps({"value": index}),
                    encoding="utf-8",
                )
            enemy_path = root / "assets/data/enemy_data.json"
            enemy_data = {
                "enemies": [
                    {
                        "id": "enemy_001",
                        "status": {"attack": 1, "maxHp": 2},
                    }
                ]
            }
            enemy_path.write_text(
                json.dumps(enemy_data),
                encoding="utf-8",
            )
            original_snapshot = current_configuration_snapshot(root)
            original_fingerprint = current_configuration_fingerprint(root)
            log_directory = root / "logs/balance"
            log_directory.mkdir(parents=True)
            (log_directory / "run_before.json").write_text(
                json.dumps(
                    {
                        "configuration": original_snapshot,
                        "controller": {
                            "type": "mcp",
                            "profile": "intermediate",
                            "build_profile": "standard",
                        },
                        "run_context": {},
                    }
                ),
                encoding="utf-8",
            )
            plan = {
                "source": {"enemy_status_sha256": file_sha256(enemy_path)},
                "changes": [
                    {
                        "target_id": "enemy_001",
                        "field": "status.attack",
                        "before": 1,
                        "after": 2,
                    }
                ],
            }

            apply_plan(
                plan,
                enemy_path,
                enemy_data,
                root / "backups",
            )
            new_fingerprint = current_configuration_fingerprint(root)
            selected, summary = select_run_logs(
                log_directory,
                filters=RunFilters(
                    controller_types=("mcp",),
                    controller_profiles=("intermediate",),
                    build_profiles=("standard",),
                    configuration_fingerprints=(new_fingerprint,),
                ),
            )

            self.assertNotEqual(new_fingerprint, original_fingerprint)
            self.assertEqual(selected, [])
            self.assertEqual(summary["matched_file_count"], 0)
            self.assertEqual(summary["excluded_file_count"], 1)

    def test_paired_validation_rejects_a_worse_same_seed_score(self) -> None:
        case = {
            "id": "primary",
            "kind": "primary",
            "minimum_score_delta": 0.0,
            "minimum_terminal_run_rate": 1.0,
        }
        baseline = {
            "case": case,
            "missing_seeds": [],
            "run_balance": {
                "balance_score": 70.0,
                "metrics": {"terminal_run_rate": 1.0},
            },
            "outcomes": {
                "1": {
                    "reached_stage_index": 10,
                    "cleared_stage_count": 9,
                    "remaining_hp_ratio": 0.2,
                }
            },
        }
        candidate = {
            "missing_seeds": [],
            "run_balance": {
                "balance_score": 65.0,
                "metrics": {"terminal_run_rate": 1.0},
            },
            "outcomes": {
                "1": {
                    "reached_stage_index": 8,
                    "cleared_stage_count": 7,
                    "remaining_hp_ratio": 0.1,
                }
            },
        }

        result = compare_validation_snapshots(baseline, candidate)

        self.assertFalse(result["passed"])
        self.assertEqual(result["balance_score_delta"], -5.0)

    def test_regression_cases_cover_beginner_and_advanced(self) -> None:
        config = {
            "controller_profile": "intermediate",
            "build_profile": "standard",
            "paired_validation": {
                "enabled": True,
                "validation_variant": "dda_off",
                "seeds": [1, 2, 3],
            },
            "regression_validation": {
                "enabled": True,
                "seed_count": 2,
                "cases": [
                    {"id": "beginner", "controller_profile": "beginner"},
                    {"id": "advanced", "controller_profile": "advanced"},
                ],
            },
        }

        cases = validation_cases(config)

        self.assertEqual(
            [case["controller_profile"] for case in cases],
            ["intermediate", "beginner", "advanced"],
        )
        self.assertEqual(cases[1]["run_seeds"], [1, 2])

    def test_holdout_case_uses_disjoint_unseen_seeds(self) -> None:
        config = {
            "controller_profile": "intermediate",
            "build_profile": "standard",
            "paired_validation": {
                "enabled": True,
                "validation_variant": "dda_off",
                "seeds": [1, 2, 3],
            },
            "holdout_validation": {
                "enabled": True,
                "seeds": [101, 102],
                "minimum_balance_score_delta": 0.0,
            },
            "regression_validation": {"enabled": False},
        }

        cases = validation_cases(config)

        self.assertEqual([case["kind"] for case in cases], ["primary", "holdout"])
        self.assertEqual(cases[1]["run_seeds"], [101, 102])

    def test_readiness_collects_more_runs_for_statistical_uncertainty(self) -> None:
        evaluation = {
            "run_balance": {
                "balance_sample_count": 30,
                "minimum_sample_count": 30,
                "judgement": "inconclusive",
                "statistical_decision": {
                    "additional_samples_recommended": 10,
                },
            },
            "sample_quality": {"passed": True},
            "guardrail_plan": {
                "changes": [],
                "stage_evaluations": [],
            },
        }

        readiness = assess_readiness(evaluation)

        self.assertEqual(readiness["status"], "collect")
        self.assertEqual(readiness["missing_runs"], 10)
        self.assertEqual(
            readiness["reason"],
            "statistical_decision_inconclusive",
        )

    def test_run_budget_blocks_projected_collection(self) -> None:
        state = {
            "started_at": "2026-08-17T00:00:00+00:00",
            "budget_usage": {
                "completed_runs": 9,
                "ai_calls": 0,
                "input_tokens": 0,
                "output_tokens": 0,
                "total_tokens": 0,
                "estimated_cost_usd": 0.0,
            },
        }
        config = {
            "budgets": {
                "maximum_wall_clock_seconds": 999999999,
                "maximum_completed_runs": 10,
                "maximum_ai_calls": 2,
                "maximum_total_tokens": 1000,
            }
        }

        result = budget_failure(state, config, additional_runs=2)

        self.assertIsNotNone(result)
        self.assertEqual(result["type"], "completed_runs")

    def test_ai_usage_records_cached_tokens_and_estimated_cost(self) -> None:
        state = {"budget_usage": {}}
        plan = {
            "ai": {
                "usage": {
                    "input_tokens": 1000,
                    "output_tokens": 100,
                    "total_tokens": 1100,
                    "input_tokens_details": {"cached_tokens": 400},
                }
            }
        }
        config = {
            "budgets": {
                "pricing_usd_per_million_tokens": {
                    "input": 5.0,
                    "cached_input": 0.5,
                    "output": 30.0,
                }
            }
        }

        record_ai_usage(state, plan, config)

        usage = state["budget_usage"]
        self.assertEqual(usage["ai_calls"], 1)
        self.assertEqual(usage["cached_input_tokens"], 400)
        self.assertEqual(usage["total_tokens"], 1100)
        self.assertAlmostEqual(usage["estimated_cost_usd"], 0.0062)


if __name__ == "__main__":
    unittest.main()
