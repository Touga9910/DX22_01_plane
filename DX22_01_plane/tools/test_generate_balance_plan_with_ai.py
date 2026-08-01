from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

from adjust_balance_from_logs import apply_plan, build_adjustment_plan
from generate_balance_plan_with_ai import (
    build_ai_input,
    build_ai_plan,
    build_openai_request,
    call_openai,
)


TARGETS = {
    "minimum_sample_count": 3,
    "adjustment_score_threshold": 60.0,
    "balanced_score_threshold": 80.0,
    "metrics": {
        "clear_rate": {
            "min": 0.65,
            "max": 0.80,
            "tolerance": 0.25,
            "weight": 0.40,
        },
        "median_shots": {
            "min": 4.0,
            "max": 7.0,
            "tolerance": 5.0,
            "weight": 0.25,
        },
        "average_remaining_hp_ratio": {
            "min": 0.30,
            "max": 0.65,
            "tolerance": 0.35,
            "weight": 0.15,
        },
        "no_hit_shot_rate": {
            "min": 0.0,
            "max": 0.20,
            "tolerance": 0.40,
            "weight": 0.10,
        },
        "average_collision_effect": {
            "min": 1.0,
            "max": 3.0,
            "tolerance": 3.0,
            "weight": 0.10,
        },
    },
}

POLICY = {
    "minimum_enemy_confidence": 0.65,
    "maximum_step_per_iteration": 1,
    "adjustable_fields": ["maxHp", "attack"],
    "limits": {
        "maxHp": {"min": 1, "max": 100},
        "attack": {"min": 0, "max": 50},
    },
}

AI_CONFIG = {
    "model": "gpt-5.6-sol",
    "reasoning_effort": "medium",
    "text_verbosity": "low",
    "request_timeout_seconds": 10,
    "max_output_tokens": 1000,
}

ENEMY_DATA = {
    "enemies": [
        {
            "id": "enemy_test",
            "status": {
                "maxHp": 2,
                "attack": 1,
            },
        }
    ]
}


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def write_easy_run(log_directory: Path, index: int) -> None:
    write_json(
        log_directory / f"run_{index:03d}.json",
        {
            "run_id": f"run_{index:03d}",
            "stages": [
                {
                    "stage_id": "easy_stage",
                    "difficulty": 1,
                    "stage_start": {
                        "enemies": [{"id": "enemy_test"}],
                    },
                    "shots": [
                        {
                            "ball_id": "player_standard",
                            "collision_effect": 1,
                            "player_enemy_hit_count": 1,
                            "enemies_defeated": 1,
                        }
                    ],
                    "stage_result": {
                        "result": "clear",
                        "total_shots": 1,
                        "remaining_hp_ratio": 1.0,
                    },
                }
            ],
        },
    )


class GenerativeBalanceTests(unittest.TestCase):
    def make_guardrail_fixture(
        self,
        root: Path,
    ) -> tuple[Path, Path, dict]:
        log_directory = root / "logs"
        enemy_file = root / "enemy_data.json"
        write_json(enemy_file, ENEMY_DATA)
        for index in range(3):
            write_easy_run(log_directory, index)
        guardrail_plan = build_adjustment_plan(
            log_directory,
            TARGETS,
            POLICY,
            enemy_file,
            json.loads(json.dumps(ENEMY_DATA)),
        )
        return log_directory, enemy_file, guardrail_plan

    def test_request_uses_responses_structured_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            log_directory, _, guardrail_plan = (
                self.make_guardrail_fixture(root)
            )
            ai_input = build_ai_input(
                guardrail_plan,
                TARGETS,
                POLICY,
                ENEMY_DATA,
                log_directory,
            )
            request = build_openai_request(ai_input, AI_CONFIG)

            self.assertEqual(request["model"], "gpt-5.6-sol")
            self.assertFalse(request["store"])
            self.assertEqual(
                request["text"]["format"]["type"],
                "json_schema",
            )
            self.assertTrue(request["text"]["format"]["strict"])

    def test_mock_ai_plan_is_validated_backed_up_and_applied(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            log_directory, enemy_file, guardrail_plan = (
                self.make_guardrail_fixture(root)
            )
            ai_output = {
                "analysis_summary": "このステージは一貫して簡単すぎます。",
                "changes": [
                    {
                        "target_id": "enemy_test",
                        "field": "status.attack",
                        "before": 1,
                        "after": 2,
                        "reason": "プレイヤーの残りHPが高すぎるためです。",
                        "evidence_stage_ids": ["easy_stage"],
                    }
                ],
            }
            enemy_data = json.loads(json.dumps(ENEMY_DATA))
            plan = build_ai_plan(
                guardrail_plan,
                ai_output,
                {"id": "mock_123", "usage": {"input_tokens": 10}},
                enemy_data,
                POLICY,
                AI_CONFIG,
            )

            self.assertEqual(plan["ai"]["status"], "completed")
            self.assertEqual(plan["ai"]["response_id"], "mock_123")
            self.assertEqual(plan["changes"][0]["after"], 2)

            backup = apply_plan(
                plan,
                enemy_file,
                enemy_data,
                root / "backups",
            )
            self.assertIsNotNone(backup)
            applied = json.loads(enemy_file.read_text(encoding="utf-8"))
            self.assertEqual(
                applied["enemies"][0]["status"]["attack"],
                2,
            )

    def test_ai_cannot_reverse_guardrail_direction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            _, _, guardrail_plan = self.make_guardrail_fixture(root)
            invalid_output = {
                "analysis_summary": "変更方向の不正を確認するテストです。",
                "changes": [
                    {
                        "target_id": "enemy_test",
                        "field": "status.attack",
                        "before": 1,
                        "after": 0,
                        "reason": "この変更案は拒否される必要があります。",
                        "evidence_stage_ids": ["easy_stage"],
                    }
                ],
            }

            with self.assertRaisesRegex(RuntimeError, "direction"):
                build_ai_plan(
                    guardrail_plan,
                    invalid_output,
                    {},
                    ENEMY_DATA,
                    POLICY,
                    AI_CONFIG,
                )

    def test_english_explanation_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            _, _, guardrail_plan = self.make_guardrail_fixture(root)
            english_output = {
                "analysis_summary": "The stage is consistently too easy.",
                "changes": [
                    {
                        "target_id": "enemy_test",
                        "field": "status.attack",
                        "before": 1,
                        "after": 2,
                        "reason": "Players finish with too much HP.",
                        "evidence_stage_ids": ["easy_stage"],
                    }
                ],
            }

            with self.assertRaisesRegex(RuntimeError, "Japanese"):
                build_ai_plan(
                    guardrail_plan,
                    english_output,
                    {},
                    ENEMY_DATA,
                    POLICY,
                    AI_CONFIG,
                )

    def test_missing_api_key_stops_before_network(self) -> None:
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "OPENAI_API_KEY"):
                call_openai({}, AI_CONFIG)


if __name__ == "__main__":
    unittest.main()
