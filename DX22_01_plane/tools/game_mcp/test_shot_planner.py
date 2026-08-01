from __future__ import annotations

import unittest
from pathlib import Path

from bridge_store import GameBridgeError
from shot_planner import (
    PlayerProfileController,
    ensure_enemy_target_ids,
    load_player_profiles,
    plan_targeted_shot,
    resolve_target_id_argument,
)


PROFILES_PATH = (
    Path(__file__).resolve().parent / "player_profiles.json"
)


class ShotPlannerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.profiles = load_player_profiles(PROFILES_PATH)

    @staticmethod
    def make_state(defeated: bool = False) -> dict:
        return {
            "player": {
                "position": {"x": 0.0, "y": 1.0, "z": 0.0},
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
                }
            ],
            "table": {
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
