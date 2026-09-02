from __future__ import annotations

import unittest
from pathlib import Path

from balance_system_metrics import calculate_system_metrics


class BalanceSystemMetricTests(unittest.TestCase):
    def test_aggregates_build_economy_route_and_pocket_metrics(self) -> None:
        events = [
            {
                "event_type": "clear_reward_offered",
                "details": {
                    "reward_types": ["new_ball", "extra_money"],
                    "new_ball_candidates": [
                        {"ball_id": "player_bounce"},
                    ],
                    "extra_money_amount": 10,
                },
            },
            {
                "event_type": "clear_reward_choice",
                "details": {
                    "reward": "new_ball",
                    "selected_index": 0,
                    "money_after": 10,
                },
            },
            {
                "event_type": "clear_reward_offered",
                "details": {
                    "reward_types": ["upgrade_ball", "extra_money"],
                    "extra_money_amount": 10,
                },
            },
            {
                "event_type": "clear_reward_choice",
                "details": {
                    "reward": "extra_money",
                    "selected_index": 0,
                    "money_after": 20,
                },
            },
            {
                "event_type": "ball_upgraded",
                "details": {
                    "ball_id": "player_standard",
                    "source_scene": "REST_SITE",
                },
            },
            {
                "event_type": "stage_money_reward",
                "details": {"amount": 10, "money_after": 10},
            },
            {
                "event_type": "route_choice",
                "details": {
                    "controller": "mcp",
                    "offered_routes": ["Battle", "Shop", "Rest Site"],
                    "selected_index": 1,
                    "selected_route": "Shop",
                },
            },
            {
                "event_type": "relic_purchased",
                "details": {
                    "relic_name": "Bank Shot",
                    "cost": 20,
                    "money_after": 0,
                },
            },
            {"event_type": "bank_shot_triggered", "details": {}},
            {
                "event_type": "emergency_repair_triggered",
                "details": {"heal_amount": 1},
            },
            {
                "event_type": "rest_heal",
                "details": {
                    "heal_amount": 10,
                    "configured_heal_amount": 13,
                },
            },
            {
                "event_type": "enemy_pocket_controlled",
                "details": {
                    "enemy_id": "enemy_normal",
                    "stage_type": "normal",
                },
            },
            {
                "event_type": "enemy_pocket_returned",
                "details": {"enemy_id": "enemy_normal"},
            },
            {
                "event_type": "enemy_pocket_finisher",
                "details": {
                    "enemy_id": "enemy_strong",
                    "stage_type": "normal",
                    "already_defeated": False,
                },
            },
            {
                "event_type": "enemy_pocket_finisher",
                "details": {
                    "enemy_id": "enemy_tank",
                    "stage_type": "midBoss",
                    "already_defeated": True,
                },
            },
        ]
        run = {
            "run_context": {"initial_money": 0},
            "configuration": {
                "files": [
                    {"path": "assets/data/pocket_rules.json"},
                ],
            },
            "events": events,
            "stages": [
                {
                    "stage_context": {
                        "deck": [
                            {
                                "id": "player_standard",
                                "instance_id": 1,
                                "upgrade_level": 1,
                            },
                        ],
                    },
                    "shots": [
                        {"shot_context": {"selected_instance_id": 1}},
                        {"shot_context": {"selected_instance_id": 99}},
                    ],
                    "damage_events": [
                        {"source": "pocket", "damage": 3},
                    ],
                },
            ],
        }

        report = calculate_system_metrics([
            (Path("run_001.json"), run, {}),
        ])

        build = report["build"]
        self.assertEqual(
            build["new_ball_acquisitions_by_ball_id"],
            {"player_bounce": 1},
        )
        self.assertEqual(
            build["new_ball_choices"]["offered_by_ball_id"],
            {"player_bounce": 1},
        )
        self.assertEqual(
            build["new_ball_choices"]["selection_rate_when_offered"],
            {"player_bounce": 1.0},
        )
        self.assertEqual(
            build["reward_decisions"]["completion_rate"],
            1.0,
        )
        self.assertEqual(
            build["upgrades"]["by_ball_id"],
            {"player_standard": 1},
        )
        self.assertEqual(
            build["ball_usage"]["shots_by_ball_id"],
            {"player_standard": 1},
        )
        self.assertEqual(build["ball_usage"]["unmapped_shot_count"], 1)
        self.assertEqual(
            build["relics"]["effect_triggers_by_name"],
            {"Bank Shot": 1, "Emergency Repair Kit": 1},
        )

        economy = report["economy"]
        self.assertEqual(economy["total_money_earned"], 20)
        self.assertEqual(economy["relic_money_spent"], 20)
        self.assertEqual(economy["average_final_money"], 0.0)
        self.assertEqual(economy["median_final_money"], 0.0)
        self.assertEqual(economy["rest"]["wasted_healing"], 3)

        routes = report["routes"]
        self.assertEqual(routes["selected_by_route"], {"Shop": 1})
        self.assertEqual(routes["invalid_selection_count"], 0)

        pockets = report["pockets"]
        self.assertEqual(pockets["enemy"]["controlled_count"], 1)
        self.assertEqual(pockets["enemy"]["returned_count"], 1)
        self.assertEqual(pockets["enemy"]["live_finisher_count"], 1)
        self.assertEqual(
            pockets["enemy"]["post_defeat_finisher_count"],
            1,
        )
        self.assertEqual(pockets["player"]["pocket_count"], 1)
        self.assertEqual(pockets["player"]["total_damage"], 3)
        self.assertEqual(pockets["tactical_pocket_run_rate"], 1.0)

    def test_legacy_run_without_optional_telemetry_is_safe(self) -> None:
        report = calculate_system_metrics([
            (Path("run_legacy.json"), {"stages": []}, {}),
        ])

        self.assertEqual(report["sample_count"], 1)
        self.assertEqual(report["build"]["ball_usage"]["mapped_shot_count"], 0)
        self.assertEqual(report["routes"]["decision_count"], 0)
        self.assertEqual(report["pockets"]["tactical_pocket_run_rate"], 0.0)


if __name__ == "__main__":
    unittest.main()
