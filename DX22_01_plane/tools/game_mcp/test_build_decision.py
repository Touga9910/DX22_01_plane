from __future__ import annotations

import unittest
from pathlib import Path

from build_decision import (
    BALL_ROLES,
    build_decision_snapshot,
    evaluate_ball_choices,
    evaluate_build_hypotheses,
    evaluate_clear_reward,
    evaluate_relic_choices,
    evaluate_risk_tradeoff,
    evaluate_shop_ball_removal,
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
    upgrade_level: int = 0,
) -> dict:
    return {
        "index": index,
        "definition_id": definition_id,
        "instance_id": instance_id,
        "can_upgrade": can_upgrade,
        "upgrade_level": upgrade_level,
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

    def test_all_runtime_balls_have_explicit_roles(self) -> None:
        catalog_path = Path(__file__).resolve().parents[2] / "assets/data/player_status.json"
        import json
        runtime_ids = {
            entry["id"]
            for entry in json.loads(catalog_path.read_text(encoding="utf-8"))["balls"]
        }
        self.assertEqual(runtime_ids, set(BALL_ROLES))

    def test_generator_then_finisher_then_support_priority_is_dynamic(self) -> None:
        offers = [
            ball(0, "player_pierce"),
            ball(1, "player_trace_driver"),
            ball(2, "player_refractive_pierce"),
        ]
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 50},
            "deck_balls": [ball(0, "player_standard", instance_id=10)],
            "offered_balls": offers,
        }
        first = evaluate_ball_choices(state, self.profiles, "pierce")
        self.assertEqual(first["recommended"]["definition_id"], "player_pierce")
        self.assertGreater(
            first["recommended"]["breakdown"]["missing_role_bonus"], 0,
        )

        state["deck_balls"].append(ball(1, "player_pierce", instance_id=11))
        second = evaluate_ball_choices(state, self.profiles, "pierce")
        self.assertEqual(second["recommended"]["definition_id"], "player_trace_driver")
        generator = next(
            choice for choice in second["choices"]
            if choice["definition_id"] == "player_pierce"
        )
        self.assertLess(
            generator["breakdown"]["duplicate_generator_penalty"], 0,
        )

    def test_cross_axis_generator_is_not_undervalued(self) -> None:
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 50},
            "deck_balls": [ball(0, "player_standard", instance_id=1)],
            "offered_balls": [
                ball(0, "player_origin_anchor"),
                ball(1, "player_standard"),
            ],
        }
        result = evaluate_ball_choices(state, self.profiles, "pierce")
        cross_axis = next(
            choice for choice in result["choices"]
            if choice["definition_id"] == "player_origin_anchor"
        )
        self.assertGreater(
            cross_axis["breakdown"]["missing_role_bonus"], 0,
        )

    def test_shop_removal_prefers_unupgraded_standard_and_preserves_synergy(self) -> None:
        deck = [
            ball(0, "player_standard", instance_id=1, upgrade_level=2),
            ball(1, "player_standard", instance_id=2),
            ball(2, "player_standard", instance_id=3),
            ball(3, "player_standard", instance_id=4),
            ball(4, "player_pierce", instance_id=5),
            ball(5, "player_trace_driver", instance_id=6),
        ]
        state = {
            "scene": "shop",
            "available_actions": ["remove_ball", "continue_to_battle"],
            "player": {"current_hp": 50, "max_hp": 50, "money": 50},
            "deck_balls": deck,
            "relics": [],
        }
        result = evaluate_shop_ball_removal(state, self.profiles, "pierce")
        self.assertEqual(result["recommended"]["definition_id"], "player_standard")
        self.assertNotEqual(result["recommended"]["instance_id"], 1)
        protected = {
            candidate["definition_id"]: candidate["protected"]
            for candidate in result["candidates"]
            if candidate["definition_id"] in {"player_pierce", "player_trace_driver"}
        }
        self.assertEqual(protected, {"player_pierce": True, "player_trace_driver": True})

    def test_shop_removal_obeys_money_and_minimum_deck(self) -> None:
        state = {
            "scene": "shop",
            "available_actions": ["continue_to_battle"],
            "player": {"current_hp": 50, "max_hp": 50, "money": 14},
            "deck_balls": [
                ball(index, "player_standard", instance_id=index + 1)
                for index in range(6)
            ],
            "relics": [],
        }
        result = evaluate_shop_ball_removal(state, self.profiles, "standard")
        self.assertIsNone(result["recommended"])
        self.assertEqual(result["skip_reason"], "insufficient_money_or_action_unavailable")
        state["player"]["money"] = 50
        state["deck_balls"] = state["deck_balls"][:5]
        result = evaluate_shop_ball_removal(state, self.profiles, "standard")
        self.assertEqual(result["skip_reason"], "minimum_deck_size")

    def test_shop_removal_runs_for_all_five_build_profiles(self) -> None:
        protected_pairs = {
            "standard": (),
            "heavy": ("player_heavy", "player_chain_impact"),
            "pierce": ("player_pierce", "player_trace_driver"),
            "bounce": ("player_cushion_charge", "player_bounce"),
            "anchor": ("player_anchor", "player_anchor_finisher"),
        }
        for profile_id, pair in protected_pairs.items():
            with self.subTest(profile_id=profile_id):
                deck = [
                    ball(index, "player_standard", instance_id=index + 1)
                    for index in range(6)
                ]
                deck.extend(
                    ball(6 + offset, definition_id, instance_id=20 + offset)
                    for offset, definition_id in enumerate(pair)
                )
                state = {
                    "scene": "shop",
                    "available_actions": ["remove_ball", "continue_to_battle"],
                    "player": {"current_hp": 50, "max_hp": 50, "money": 50},
                    "deck_balls": deck,
                    "relics": [],
                }
                result = evaluate_shop_ball_removal(
                    state, self.profiles, profile_id,
                )
                self.assertEqual(
                    result["recommended"]["definition_id"],
                    "player_standard",
                )
                protected_ids = {
                    candidate["definition_id"]
                    for candidate in result["candidates"]
                    if candidate["protected"]
                }
                self.assertTrue(set(pair).issubset(protected_ids))

    def test_requested_build_does_not_switch_to_existing_heavy_stack(self) -> None:
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
        self.assertEqual(hypotheses[0]["id"], "standard")
        result = evaluate_ball_choices(
            state,
            self.profiles,
            "standard",
        )
        self.assertEqual(
            result["recommended"]["definition_id"],
            "player_standard",
        )
        self.assertEqual(
            result["recommended"]["chosen_build_id"],
            "standard",
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
            "bounce",
        )
        self.assertEqual(result["recommended"]["index"], 1)
        self.assertEqual(
            result["recommended"]["canonical_name"],
            "Bank Shot",
        )

    def test_midboss_recommendation_only_uses_current_free_offers(self) -> None:
        state = {
            "scene": "battle", "available_actions": ["choose_relic"],
            "player": {"current_hp": 50, "max_hp": 50, "money": 0},
            "deck_balls": [],
            "relics": [
                {"index": 0, "name": "攻撃コア", "price": 0, "owned": False,
                 "midboss_offered": False},
                {"index": 1, "name": "衝撃加速装置", "price": 30, "owned": False,
                 "midboss_offered": True},
                {"index": 2, "name": "バンクショット", "price": 30, "owned": False,
                 "midboss_offered": True},
                {"index": 3, "name": "緊急修理キット", "price": 30, "owned": False,
                 "midboss_offered": True},
                {"index": 4, "name": "防御コア", "price": 0, "owned": True,
                 "midboss_offered": True},
            ],
        }
        result = evaluate_relic_choices(state, self.profiles, "standard")
        self.assertEqual(result["selection_context"], "midboss_reward")
        self.assertIn(result["recommended"]["index"], {1, 2, 3})
        self.assertEqual({choice["index"] for choice in result["offered_choices"]}, {1, 2, 3})
        self.assertIn(0, {choice["index"] for choice in result["global_evaluation"]})
        self.assertEqual(result["recommended"]["breakdown"]["price_pressure"], 0)

    def test_shop_recommendation_requires_offer_and_affordability(self) -> None:
        state = {
            "scene": "shop", "available_actions": ["buy_relic", "continue_to_battle"],
            "player": {"current_hp": 50, "max_hp": 50, "money": 20},
            "deck_balls": [],
            "relics": [
                {"index": 0, "name": "攻撃コア", "price": 10, "owned": False,
                 "shop_offered": False},
                {"index": 1, "name": "バンクショット", "price": 25, "owned": False,
                 "shop_offered": True},
                {"index": 2, "name": "緊急修理キット", "price": 20, "owned": False,
                 "shop_offered": True},
                {"index": 3, "name": "防御コア", "price": 10, "owned": True,
                 "shop_offered": True},
            ],
        }
        result = evaluate_relic_choices(state, self.profiles, "standard")
        self.assertEqual(result["selection_context"], "shop")
        self.assertEqual(result["recommended"]["index"], 2)
        self.assertEqual([choice["index"] for choice in result["offered_choices"]], [2])
        self.assertIn(0, {choice["index"] for choice in result["global_evaluation"]})
        state["available_actions"] = ["continue_to_battle"]
        self.assertIsNone(evaluate_relic_choices(state, self.profiles, "standard")["recommended"])

    def test_low_hp_adds_survival_value_to_anchor(self) -> None:
        state = {
            "player": {"current_hp": 10, "max_hp": 50, "money": 0},
            "deck_balls": [ball(2, "player_standard", instance_id=1)],
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

    def test_requested_build_missing_core_beats_unrelated_anchor(self) -> None:
        starting_deck = [
            ball(index, "player_standard", instance_id=index + 1)
            for index in range(6)
        ] + [ball(6, "player_heavy", mass=4.0, instance_id=7)]
        expected = {
            "heavy": "player_chain_impact",
            "pierce": "player_pierce",
            "bounce": "player_bounce",
            "anchor": "player_anchor",
        }
        for profile_id, core_ball in expected.items():
            with self.subTest(profile_id=profile_id):
                state = {
                    "player": {
                        "current_hp": 50,
                        "max_hp": 50,
                        "money": 10,
                    },
                    "deck_balls": starting_deck,
                    "clear_reward_ball_offers": [
                        ball(0, core_ball),
                        ball(1, "player_anchor", defense=2, anchor=True),
                        ball(2, "player_standard"),
                    ],
                    "enemies": [{}, {}, {}],
                }
                snapshot = build_decision_snapshot(
                    state,
                    self.profiles,
                    profile_id,
                )
                self.assertEqual(snapshot["active_build_id"], profile_id)
                self.assertEqual(
                    snapshot["clear_reward"]["recommended"]["reward"],
                    "new_ball",
                )
                self.assertEqual(
                    snapshot["clear_reward"]["recommended"][
                        "definition_id"
                    ],
                    core_ball,
                )
                self.assertEqual(
                    snapshot["clear_reward"]["recommended"]["reason"],
                    "missing_core_part",
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

    def test_clear_reward_new_ball_uses_only_three_random_offers(self) -> None:
        first = ball(0, "player_standard")
        first.update({"offer_index": 0, "catalog_index": 4})
        duplicate = ball(1, "player_standard")
        duplicate.update({"offer_index": 1, "catalog_index": 4})
        third = ball(2, "player_bounce")
        third.update({"offer_index": 2, "catalog_index": 7})
        state = {
            "player": {"current_hp": 50, "max_hp": 50, "money": 0},
            "deck_balls": [],
            "clear_reward_ball_offers": [first, duplicate, third],
            # This stronger catalog entry is intentionally not offered.
            "catalog_balls": [
                ball(9, "player_anchor", attack=999, anchor=True),
            ],
        }

        result = evaluate_clear_reward(state, self.profiles, "standard")
        new_ball = next(
            option
            for option in result["options"]
            if option["reward"] == "new_ball"
        )

        self.assertIn(new_ball["offer_index"], {0, 1, 2})
        self.assertIn(new_ball["catalog_index"], {4, 7})
        self.assertNotEqual(new_ball["catalog_index"], 9)


if __name__ == "__main__":
    unittest.main()
