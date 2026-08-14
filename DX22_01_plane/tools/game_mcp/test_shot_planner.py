from __future__ import annotations

import unittest
from pathlib import Path
from unittest.mock import patch

from bridge_store import GameBridgeError
from shot_planner import (
    PlayerProfileController,
    build_tactical_shot_context,
    ensure_enemy_target_ids,
    evaluate_tactical_shot_options,
    load_player_profiles,
    plan_targeted_shot,
    resolve_target_id_argument,
)


PROFILES_PATH = (
    Path(__file__).resolve().parent / "player_profiles.json"
)


class ZeroErrorRandom:
    def triangular(
        self,
        low: float,
        high: float,
        mode: float,
    ) -> float:
        return mode


class MaximumErrorRandom:
    def triangular(
        self,
        low: float,
        high: float,
        mode: float,
    ) -> float:
        return high


class MinimumErrorRandom:
    def triangular(
        self,
        low: float,
        high: float,
        mode: float,
    ) -> float:
        return low


class ShotPlannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.profiles = load_player_profiles(PROFILES_PATH)

    def setUp(self) -> None:
        self.random_patch = patch(
            "shot_planner._HUMAN_ERROR_RANDOM",
            ZeroErrorRandom(),
        )
        self.random_patch.start()

    def tearDown(self) -> None:
        self.random_patch.stop()

    @staticmethod
    def make_state(defeated: bool = False) -> dict:
        return {
            "player": {
                "position": {"x": 0.0, "y": 1.0, "z": 0.0},
                "radius": 2.4,
            },
            "enemies": [
                {
                    "target_id": "enemy:0",
                    "enemy_id": "enemy-a",
                    "defeated": defeated,
                    "position": {
                        "x": 0.0,
                        "y": 1.0,
                        "z": 10.0,
                    },
                    "radius": 2.4,
                    "pocket_finisher_eligible": True,
                }
            ],
            "table": {
                "pockets": [
                    {
                        "index": 0,
                        "position": {
                            "x": 10.0,
                            "y": 1.0,
                            "z": 10.0,
                        },
                        "radius": 3.0,
                    }
                ],
                "walls": [
                    {
                        "start": {
                            "x": 10.0,
                            "y": 0.0,
                            "z": -20.0,
                        },
                        "end": {
                            "x": 10.0,
                            "y": 0.0,
                            "z": 20.0,
                        },
                    }
                ]
            },
        }

    def test_direct_shot_aims_at_required_live_enemy(self) -> None:
        plan = plan_targeted_shot(
            self.make_state(),
            "enemy:0",
            4.0,
            "direct",
            -1,
            self.profiles["beginner"],
        )

        arguments = plan["arguments"]
        self.assertAlmostEqual(arguments["direction_x"], 0.0)
        self.assertAlmostEqual(arguments["direction_z"], 1.0)
        self.assertEqual(arguments["target_id"], "enemy:0")
        self.assertEqual(
            plan["shot_plan"]["shot_type"],
            "direct",
        )

    def test_bank_shot_uses_reflected_target_geometry(self) -> None:
        plan = plan_targeted_shot(
            self.make_state(),
            "enemy:0",
            6.0,
            "bank",
            -1,
            self.profiles["advanced"],
        )

        arguments = plan["arguments"]
        self.assertAlmostEqual(
            arguments["direction_x"],
            0.8944271909,
        )
        self.assertAlmostEqual(
            arguments["direction_z"],
            0.4472135955,
        )
        self.assertEqual(arguments["wall_index"], 0)
        contact = plan["shot_plan"]["wall_contact_point"]
        self.assertAlmostEqual(contact["x"], 10.0)
        self.assertAlmostEqual(contact["z"], 5.0)

    def test_pocket_goal_aims_behind_enemy_toward_pocket(self) -> None:
        plan = plan_targeted_shot(
            self.make_state(),
            "enemy:0",
            5.0,
            "direct",
            -1,
            self.profiles["intermediate"],
            shot_goal="pocket",
            pocket_index=0,
        )

        self.assertLess(plan["arguments"]["direction_x"], 0.0)
        self.assertGreater(plan["arguments"]["direction_z"], 0.0)
        self.assertEqual(plan["arguments"]["shot_goal"], "pocket")
        self.assertEqual(plan["shot_plan"]["pocket_index"], 0)
        self.assertEqual(
            plan["shot_plan"]["intended_outcome"],
            "finisher",
        )

    def test_auto_goal_selects_feasible_pocket_finisher(self) -> None:
        state = self.make_state()
        state["table"]["pockets"][0]["position"] = {
            "x": 0.0,
            "y": 1.0,
            "z": 30.0,
        }
        plan = plan_targeted_shot(
            state,
            "enemy:0",
            5.0,
            "direct",
            -1,
            self.profiles["intermediate"],
            shot_goal="auto",
        )

        self.assertEqual(plan["arguments"]["shot_goal"], "pocket")
        evaluation = plan["shot_plan"]["tactical_evaluation"]
        self.assertEqual(evaluation["recommended_goal"], "pocket")
        self.assertEqual(
            evaluation["reason"],
            "pocket_finisher_has_higher_value",
        )

    def test_auto_goal_rejects_pocket_with_bad_approach_angle(self) -> None:
        evaluation = evaluate_tactical_shot_options(
            self.make_state(),
            "enemy:0",
            5.0,
            self.profiles["intermediate"],
        )

        self.assertEqual(evaluation["recommended_goal"], "damage")
        self.assertEqual(
            evaluation["reason"],
            "no_feasible_pocket_route",
        )
        self.assertFalse(
            evaluation["pocket_options"][0]["feasible"]
        )

    def test_auto_goal_penalizes_player_path_into_another_pocket(self) -> None:
        state = self.make_state()
        state["player"].update({
            "current_hp": 8,
            "max_hp": 50,
            "attack": 2,
        })
        state["enemies"][0].update({
            "hp": 2,
            "max_hp": 10,
            "attack": 2,
            "defense": 0,
        })
        state["pocket_rules"] = {
            "player": {"damage_amount": 3}
        }
        state["table"]["pockets"] = [
            {
                "index": 0,
                "position": {"x": 10.0, "y": 1.0, "z": 30.0},
                "radius": 3.0,
            },
            {
                "index": 1,
                "position": {"x": -10.0, "y": 1.0, "z": 10.0},
                "radius": 3.0,
            },
        ]

        evaluation = evaluate_tactical_shot_options(
            state,
            "enemy:0",
            5.0,
            self.profiles["intermediate"],
            pocket_index=0,
        )

        self.assertEqual(evaluation["recommended_goal"], "damage")
        self.assertEqual(
            evaluation["reason"],
            "self_pocket_risk_is_too_high",
        )
        self.assertEqual(
            evaluation["best_pocket_option"]["self_pocket_risk"],
            1.0,
        )

    def test_auto_goal_uses_pocket_to_control_dangerous_enemy(self) -> None:
        state = self.make_state()
        enemy = state["enemies"][0]
        enemy["pocket_finisher_eligible"] = False
        enemy["hp"] = 12
        enemy["max_hp"] = 12
        enemy["attack"] = 8
        enemy["defense"] = 0
        enemy["can_attack_this_turn"] = True
        state["player"].update({
            "current_hp": 10,
            "max_hp": 50,
            "attack": 2,
        })
        state["pocket_rules"] = {
            "player": {"damage_amount": 3}
        }
        state["table"]["pockets"][0]["position"] = {
            "x": 0.0,
            "y": 1.0,
            "z": 30.0,
        }

        evaluation = evaluate_tactical_shot_options(
            state,
            "enemy:0",
            5.0,
            self.profiles["intermediate"],
        )
        self.assertEqual(evaluation["recommended_goal"], "pocket")
        self.assertEqual(
            evaluation["reason"],
            "pocket_control_prevents_dangerous_attack",
        )

    def test_state_context_recommends_target_and_goal(self) -> None:
        state = self.make_state()
        state["table"]["pockets"][0]["position"] = {
            "x": 0.0,
            "y": 1.0,
            "z": 30.0,
        }
        context = build_tactical_shot_context(
            state,
            self.profiles["intermediate"],
        )

        self.assertEqual(context["recommended_target_id"], "enemy:0")
        self.assertEqual(context["recommended_goal"], "pocket")
        self.assertEqual(len(context["recommendations"]), 1)

    def test_pocketed_enemy_cannot_be_targeted(self) -> None:
        state = self.make_state()
        state["enemies"][0]["pocketed"] = True
        with self.assertRaises(GameBridgeError):
            plan_targeted_shot(
                state,
                "enemy:0",
                4.0,
                "direct",
                -1,
                self.profiles["intermediate"],
            )

    def test_human_error_is_bounded_and_reported(self) -> None:
        plan = plan_targeted_shot(
            self.make_state(),
            "enemy:0",
            4.0,
            "direct",
            -1,
            self.profiles["beginner"],
            MaximumErrorRandom(),
        )

        arguments = plan["arguments"]
        error = plan["shot_plan"]["human_error"]
        self.assertAlmostEqual(error["aim_error_limit"], 0.048)
        self.assertAlmostEqual(error["lateral_aim_error"], 0.048)
        self.assertAlmostEqual(error["actual_aim_point"]["x"], 0.048)
        self.assertAlmostEqual(error["actual_aim_point"]["z"], 10.0)
        self.assertAlmostEqual(error["requested_power"], 4.0)
        self.assertAlmostEqual(error["actual_power"], 4.4)
        self.assertAlmostEqual(arguments["power"], 4.4)
        self.assertGreater(arguments["direction_x"], 0.0)

    def test_power_error_changes_with_player_level(self) -> None:
        expected_power = {
            "beginner": 4.4,
            "intermediate": 4.2,
            "advanced": 4.08,
        }
        for level, expected in expected_power.items():
            with self.subTest(level=level):
                plan = plan_targeted_shot(
                    self.make_state(),
                    "enemy:0",
                    4.0,
                    "direct",
                    -1,
                    self.profiles[level],
                    MaximumErrorRandom(),
                )
                self.assertAlmostEqual(
                    plan["arguments"]["power"],
                    expected,
                )

    def test_different_error_samples_change_the_same_shot(self) -> None:
        plans = [
            plan_targeted_shot(
                self.make_state(),
                "enemy:0",
                4.0,
                "direct",
                -1,
                self.profiles["intermediate"],
                random_source,
            )
            for random_source in (
                MinimumErrorRandom(),
                MaximumErrorRandom(),
            )
        ]

        self.assertLess(plans[0]["arguments"]["direction_x"], 0.0)
        self.assertGreater(plans[1]["arguments"]["direction_x"], 0.0)
        self.assertAlmostEqual(plans[0]["arguments"]["power"], 3.8)
        self.assertAlmostEqual(plans[1]["arguments"]["power"], 4.2)

    def test_beginner_cannot_use_bank_shot(self) -> None:
        with self.assertRaises(GameBridgeError):
            plan_targeted_shot(
                self.make_state(),
                "enemy:0",
                4.0,
                "bank",
                -1,
                self.profiles["beginner"],
            )

    def test_defeated_or_unknown_enemy_cannot_be_targeted(self) -> None:
        for state, enemy_id in (
            (self.make_state(defeated=True), "enemy:0"),
            (self.make_state(), "missing"),
        ):
            with self.subTest(enemy_id=enemy_id):
                with self.assertRaises(GameBridgeError):
                    plan_targeted_shot(
                        state,
                        enemy_id,
                        4.0,
                        "direct",
                        -1,
                        self.profiles["intermediate"],
                    )

    def test_duplicate_enemy_ids_use_unique_target_id(self) -> None:
        state = self.make_state()
        state["enemies"].append(
            {
                "target_id": "enemy:1",
                "enemy_id": "enemy-a",
                "defeated": False,
                "position": {
                    "x": 10.0,
                    "y": 1.0,
                    "z": 0.0,
                },
            }
        )

        plan = plan_targeted_shot(
            state,
            "enemy:1",
            4.0,
            "direct",
            -1,
            self.profiles["intermediate"],
        )
        self.assertEqual(
            plan["shot_plan"]["target_id"],
            "enemy:1",
        )
        self.assertEqual(
            plan["shot_plan"]["enemy_id"],
            "enemy-a",
        )
        self.assertAlmostEqual(
            plan["arguments"]["direction_x"],
            1.0,
        )
        self.assertAlmostEqual(
            plan["arguments"]["direction_z"],
            0.0,
        )

        with self.assertRaisesRegex(
            GameBridgeError,
            "enemy_idが重複",
        ):
            plan_targeted_shot(
                state,
                "enemy-a",
                4.0,
                "direct",
                -1,
                self.profiles["intermediate"],
            )

    def test_old_game_state_gets_target_ids_without_restart(self) -> None:
        state = self.make_state()
        state["enemies"][0].pop("target_id")
        state["enemies"].append(
            {
                "enemy_id": "enemy-a",
                "defeated": False,
                "position": {
                    "x": 10.0,
                    "y": 1.0,
                    "z": 0.0,
                },
            }
        )

        ensure_enemy_target_ids(state)
        self.assertEqual(
            [
                enemy["target_id"]
                for enemy in state["enemies"]
            ],
            ["enemy:0", "enemy:1"],
        )
        plan = plan_targeted_shot(
            state,
            "enemy:1",
            4.0,
            "direct",
            -1,
            self.profiles["intermediate"],
        )
        self.assertEqual(
            plan["shot_plan"]["target_id"],
            "enemy:1",
        )

    def test_new_and_legacy_target_arguments_are_compatible(
        self,
    ) -> None:
        self.assertEqual(
            resolve_target_id_argument("enemy:1", ""),
            "enemy:1",
        )
        self.assertEqual(
            resolve_target_id_argument("", "enemy:1"),
            "enemy:1",
        )
        self.assertEqual(
            resolve_target_id_argument(
                "enemy:1",
                "enemy:1",
            ),
            "enemy:1",
        )
        with self.assertRaises(GameBridgeError):
            resolve_target_id_argument("enemy:0", "enemy:1")
        with self.assertRaises(GameBridgeError):
            resolve_target_id_argument("", "")

    def test_profile_controller_switches_level(self) -> None:
        controller = PlayerProfileController(
            self.profiles,
            "intermediate",
        )
        self.assertEqual(
            controller.snapshot()["player_level"],
            "intermediate",
        )
        changed = controller.set_level("advanced")
        self.assertEqual(changed["player_level"], "advanced")
        self.assertTrue(changed["allow_bank_shots"])


if __name__ == "__main__":
    unittest.main()
