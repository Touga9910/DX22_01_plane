from __future__ import annotations

import unittest
from pathlib import Path

from build_profiles import load_build_profiles
from collect_fixed_balance_runs import (
    catalog_candidate,
    choose_shot_type,
    offered_ball_score,
    upgrade_candidate,
)


class BuildCollectorDecisionTests(unittest.TestCase):
    def setUp(self) -> None:
        path = Path(__file__).resolve().parent / "build_profiles.json"
        self.profiles, _ = load_build_profiles(path)

    def test_each_build_selects_its_priority_catalog_ball(self) -> None:
        catalog = [
            {"index": index, "definition_id": definition_id}
            for index, definition_id in enumerate((
                "player_standard",
                "player_heavy",
                "player_pierce",
                "player_bounce",
                "player_anchor",
            ))
        ]
        state = {"catalog_balls": catalog}
        expected = {
            "standard": "player_standard",
            "heavy": "player_heavy",
            "pierce": "player_pierce",
            "bounce": "player_bounce",
            "anchor": "player_anchor",
        }
        for profile_id, definition_id in expected.items():
            with self.subTest(profile_id=profile_id):
                selected = catalog_candidate(
                    state,
                    self.profiles[profile_id],
                )
                self.assertIsNotNone(selected)
                self.assertEqual(selected["definition_id"], definition_id)

    def test_upgrade_priority_precedes_upgrade_level(self) -> None:
        state = {
            "deck_balls": [
                {
                    "instance_id": "standard-low",
                    "definition_id": "player_standard",
                    "upgrade_level": 0,
                    "can_upgrade": True,
                    "status": {"attack": 1},
                },
                {
                    "instance_id": "heavy-high",
                    "definition_id": "player_heavy",
                    "upgrade_level": 1,
                    "can_upgrade": True,
                    "status": {"attack": 2},
                },
            ]
        }
        selected = upgrade_candidate(state, self.profiles["heavy"])
        self.assertEqual(selected["instance_id"], "heavy-high")

    def test_pierce_score_rewards_multi_enemy_pierce(self) -> None:
        offer = {
            "definition_id": "player_pierce",
            "status": {
                "attack": 1,
                "defense": 0,
                "mass": 1,
                "restitution": 0,
                "pierce": True,
            },
        }
        policy = self.profiles["pierce"]
        self.assertGreater(
            offered_ball_score(offer, policy, 2),
            offered_ball_score(offer, policy, 1),
        )

    def test_bounce_uses_bank_only_when_skill_allows_it(self) -> None:
        state = {"mcp_control": {"allow_bank_shots": True}}
        self.assertEqual(
            choose_shot_type(state, self.profiles["bounce"], "damage", 0),
            "bank",
        )
        state["mcp_control"]["allow_bank_shots"] = False
        self.assertEqual(
            choose_shot_type(state, self.profiles["bounce"], "damage", 0),
            "direct",
        )


if __name__ == "__main__":
    unittest.main()
