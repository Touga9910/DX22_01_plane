from __future__ import annotations

import asyncio
import copy
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from build_profiles import load_build_profiles
from collect_fixed_balance_runs import (
    catalog_candidate,
    choose_shot_type,
    midboss_relic_candidate,
    offered_ball_score,
    play_current_run,
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

    def midboss_state(self) -> dict:
        return {
            "scene": "battle",
            "game_state": "clear_reward",
            "available_actions": ["choose_relic"],
            "player": {"current_hp": 13, "max_hp": 50, "money": 0},
            "relics": [
                {"index": 0, "name": "Power Core", "price": 0},
                {"index": 1, "name": "Guard Core", "price": 0,
                 "owned": True, "midboss_offered": True},
                {"index": 4, "name": "緊急修理キット", "price": 20,
                 "midboss_offered": True},
                {"index": 5, "name": "精密照準器", "price": 18,
                 "midboss_offered": True},
                {"index": 7, "name": "貫通過給機", "price": 22,
                 "midboss_offered": True},
            ],
        }

    def test_midboss_reward_is_free_and_prefers_survival_at_low_hp(self) -> None:
        state = self.midboss_state()
        original = copy.deepcopy(state)
        choice = midboss_relic_candidate(state, self.profiles, "standard")
        self.assertEqual(choice["index"], 4)
        self.assertEqual(choice["breakdown"]["price_pressure"], 0)
        self.assertEqual(state, original)

    def test_midboss_reward_excludes_unoffered_and_owned_relics(self) -> None:
        state = self.midboss_state()
        state["relics"] = state["relics"][:2]
        self.assertIsNone(
            midboss_relic_candidate(state, self.profiles, "standard"),
        )

    def test_collector_selects_midboss_relic_before_normal_reward(self) -> None:
        state = self.midboss_state()
        # Even if both are exposed, the midboss reward must be resolved first.
        state["available_actions"].append("choose_reward")
        normal_reward = {
            "scene": "battle", "available_actions": ["choose_reward"],
            "build_decision": {"clear_reward": {"recommended": {
                "reward": "extra_money", "score": 1,
            }}},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [state, normal_reward, {"scene": "result"}]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard",
                    self.profiles["standard"], self.profiles, "test", 3, 1,
                ))
        self.assertTrue(result["completed"])
        self.assertEqual(result["actions"], 2)
        self.assertEqual(result["transient_errors"], 0)
        self.assertEqual([item.args[1] for item in send.await_args_list],
                         ["choose_relic", "choose_reward"])
        self.assertEqual(send.await_args_list[0].args[2]["relic_index"], 4)

    def test_collector_uses_boss_plan_without_legacy_enemy_targeting(self):
        state = {
            "scene": "battle", "boss_state": {"hp": 60},
            "available_actions": ["select_ball", "fire_shot", "fire_boss_shot"],
            "boss_shot_choices": {"state_key": "current", "recommended": {"candidate_id": "0:4"}},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [state, {"scene": "result"}]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard", self.profiles["standard"],
                    self.profiles, "test", 2, 1))
        self.assertEqual(result["shots"], 1)
        self.assertEqual([item.args[1] for item in send.await_args_list], ["fire_boss_shot"])
        self.assertEqual(send.await_args_list[0].args[2], {"candidate_id": "0:4", "state_key": "current"})

    def test_collector_refreshes_target_after_selecting_ball(self):
        before = {
            "scene": "battle", "game_state": "aiming_direction",
            "available_actions": ["select_ball", "fire_shot"],
            "enemies": [{"target_id": "enemy:old", "attack": 1}],
            "offered_balls": [{"index": 1, "definition_id": "player_pierce"}],
            "build_decision": {"offered_ball_choices": {"recommended": {"index": 1}}},
            "shot_tactics": {"recommended_target_id": "enemy:old"},
        }
        after = {
            "available_actions": ["fire_shot"],
            "shot_tactics": {"recommended_target_id": "enemy:new"},
        }
        with patch("collect_fixed_balance_runs.get_state", new_callable=AsyncMock) as read:
            with patch("collect_fixed_balance_runs.call", new_callable=AsyncMock) as send:
                read.side_effect = [before, after, {"scene": "result"}]
                send.return_value = {"ok": True}
                result = asyncio.run(play_current_run(
                    object(), "intermediate", "standard", self.profiles["standard"],
                    self.profiles, "test", 2, 1,
                ))
        self.assertTrue(result["completed"])
        self.assertEqual(send.await_args_list[0].args[1], "select_ball")
        fired = send.await_args_list[1].args[2]
        self.assertEqual(fired["target_id"], "enemy:new")
        self.assertEqual(fired["power_mode"], "auto")
        self.assertEqual(fired["shot_type"], "auto")


if __name__ == "__main__":
    unittest.main()
