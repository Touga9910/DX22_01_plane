from __future__ import annotations

import unittest
from pathlib import Path

from build_decision import (
    build_decision_snapshot,
    evaluate_ball_choices,
    evaluate_build_hypotheses,
    evaluate_clear_reward,
    evaluate_relic_choices,
    evaluate_risk_tradeoff,
)
from build_profiles import load_build_profiles


def ball(
    index: int,
    definition_id: str,
    *,
    attack: int = 1,
    defense: int = 0,
    mass: float = 1.0,
    restitution: float = 0.0,
    pierce: bool = False,
    anchor: bool = False,
    instance_id: int = 0,
    can_upgrade: bool = True,
) -> dict:
    return {
        "index": index,
        "definition_id": definition_id,
        "instance_id": instance_id,
        "can_upgrade": can_upgrade,
        "upgrade_level": 0,
        "status": {
            "attack": attack,
            "defense": defense,
            "mass": mass,
            "restitution": restitution,
            "pierce": pierce,
            "anchor": anchor,
        },
    }


class BuildDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        path = Path(__file__).resolve().parent / "build_profiles.json"
        self.profiles, _ = load_build_profiles(path)

    def test_early_run_prefers_flexible_standalone_ball(self) -> None:
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 0},
            "deck_balls": [],
            "offered_balls": [
                ball(0, "player_bounce"),
                ball(1, "player_standard"),
            ],
        }
        result = evaluate_ball_choices(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(
            result["recommended"]["definition_id"],
            "player_standard",
        )
        self.assertGreater(
            result["recommended"]["breakdown"]["flexibility"],
            0.0,
        )

    def test_completed_heavy_core_changes_active_hypothesis(self) -> None:
        heavy_a = ball(
            0,
            "player_heavy",
            mass=4.0,
            instance_id=1,
        )
        heavy_b = ball(
            1,
            "player_heavy",
            mass=4.0,
            instance_id=2,
        )
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 0},
            "deck_balls": [heavy_a, heavy_b],
            "offered_balls": [
                ball(0, "player_standard"),
                ball(1, "player_heavy", mass=4.0),
            ],
        }
        hypotheses = evaluate_build_hypotheses(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(hypotheses[0]["id"], "heavy")
        result = evaluate_ball_choices(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(
            result["recommended"]["definition_id"],
            "player_heavy",
        )
        self.assertEqual(
            result["recommended"]["chosen_build_id"],
            "heavy",
        )

    def test_japanese_bank_shot_completes_bounce_build(self) -> None:
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 50},
            "deck_balls": [
                ball(0, "player_bounce", instance_id=1),
                ball(1, "player_bounce", instance_id=2),
            ],
            "relics": [
                {
                    "index": 0,
                    "name": "攻撃コア",
                    "price": 20,
                    "owned": False,
                },
                {
                    "index": 1,
                    "name": "バンクショット",
                    "price": 20,
                    "owned": False,
                },
            ],
        }
        result = evaluate_relic_choices(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(result["recommended"]["index"], 1)
        self.assertEqual(
            result["recommended"]["canonical_name"],
            "Bank Shot",
        )

    def test_low_hp_adds_survival_value_to_anchor(self) -> None:
        state = {
            "player": {"current_hp": 10, "max_hp": 50, "money": 0},
            "deck_balls": [],
            "offered_balls": [
                ball(0, "player_standard"),
                ball(
                    1,
                    "player_anchor",
                    defense=2,
                    anchor=True,
                ),
            ],
        }
        result = evaluate_ball_choices(
            state,
            self.profiles,
            "standard",
        )
        anchor_choice = next(
            choice
            for choice in result["choices"]
            if choice["definition_id"] == "player_anchor"
        )
        self.assertGreater(anchor_choice["breakdown"]["survival"], 0.0)
        self.assertEqual(
            result["recommended"]["definition_id"],
            "player_anchor",
        )

    def test_risk_is_rejected_below_survival_floor(self) -> None:
        state = {
            "player": {"current_hp": 10, "max_hp": 50, "money": 0},
        }
        result = evaluate_risk_tradeoff(
            state,
            benefit_score=100.0,
            hp_cost=5,
            floors_to_recovery=2,
        )
        self.assertFalse(result["accept"])
        self.assertFalse(result["survival_floor_met"])

    def test_snapshot_contains_explainable_recommendations(self) -> None:
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 20},
            "deck_balls": [
                ball(0, "player_standard", instance_id=1),
            ],
            "offered_balls": [ball(0, "player_standard")],
            "catalog_balls": [ball(0, "player_standard")],
            "relics": [],
        }
        snapshot = build_decision_snapshot(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(snapshot["preferred_build_id"], "standard")
        self.assertTrue(snapshot["hypotheses"])
        self.assertIn("clear_reward", snapshot)
        self.assertIn(
            "breakdown",
            snapshot["offered_ball_choices"]["recommended"],
        )

    def test_clear_reward_upgrade_requires_level_based_money(self) -> None:
        level_zero = ball(
            0,
            "player_standard",
            instance_id=1,
        )
        level_one = ball(
            1,
            "player_heavy",
            instance_id=2,
        )
        level_one["upgrade_level"] = 1
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 15},
            "deck_balls": [level_zero, level_one],
            "catalog_balls": [],
        }

        result = evaluate_clear_reward(state, self.profiles, "standard")
        upgrades = [
            option
            for option in result["options"]
            if option["reward"] == "upgrade_ball"
        ]

        self.assertEqual(len(upgrades), 1)
        self.assertEqual(upgrades[0]["instance_id"], 1)
        self.assertEqual(upgrades[0]["upgrade_cost"], 15)

        state["player"]["money"] = 14
        result = evaluate_clear_reward(state, self.profiles, "standard")
        self.assertFalse(
            any(
                option["reward"] == "upgrade_ball"
                for option in result["options"]
            )
        )


if __name__ == "__main__":
    unittest.main()
