from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path

from bridge_store import GameBridgeError, GameBridgeStore
from server import (
    build_dynamic_wanted_reward_order,
    build_stage_choice_context,
    create_server,
    resolve_mcp_relic_choice,
    resolve_mcp_route_choice,
    resolve_mcp_stage_choice,
)
from shot_planner import load_player_profiles


class ServerSchemaTests(unittest.TestCase):
    def _list_tools(self):
        profiles_path = (
            Path(__file__).resolve().parent
            / "player_profiles.json"
        )
        with tempfile.TemporaryDirectory() as directory:
            server = create_server(
                GameBridgeStore(Path(directory)),
                "127.0.0.1",
                8765,
                load_player_profiles(profiles_path),
                "intermediate",
            )
            return asyncio.run(server.list_tools())

    def test_fire_shot_accepts_new_and_legacy_target_fields(
        self,
    ) -> None:
        tools = self._list_tools()
        fire_shot = next(
            tool for tool in tools if tool.name == "fire_shot"
        )
        properties = fire_shot.inputSchema["properties"]
        required = fire_shot.inputSchema["required"]
        self.assertIn("target_id", properties)
        self.assertIn("target_enemy_id", properties)
        self.assertIn("shot_goal", properties)
        self.assertIn("pocket_index", properties)
        self.assertEqual(
            set(properties["shot_goal"]["enum"]),
            {"auto", "damage", "pocket"},
        )
        self.assertEqual(properties["shot_goal"]["default"], "auto")
        self.assertIn("power", required)

    def test_dynamic_balance_tool_exposes_runtime_controls(
        self,
    ) -> None:
        tools = self._list_tools()
        dynamic_balance = next(
            tool
            for tool in tools
            if tool.name == "set_dynamic_balance"
        )
        properties = dynamic_balance.inputSchema["properties"]
        required = dynamic_balance.inputSchema["required"]
        self.assertIn("enabled", properties)
        self.assertIn("reset_level", properties)
        self.assertIn("level", properties)
        self.assertIn("enabled", required)

    def test_choose_destination_requires_wanted_reward_order(self) -> None:
        tools = self._list_tools()
        choose_destination = next(
            tool for tool in tools if tool.name == "choose_destination"
        )
        properties = choose_destination.inputSchema["properties"]
        required = choose_destination.inputSchema["required"]
        self.assertIn("wanted_rewards", properties)
        self.assertIn("wanted_rewards", required)
        self.assertIn("route_index", properties)
        self.assertNotIn("route_index", required)
        self.assertNotIn("destination", properties)
        self.assertNotIn("stage_type", properties)
        self.assertIn("route_options", choose_destination.description)
        self.assertIn("欲しい順", choose_destination.description)

    @staticmethod
    def _route_state(hp: int, money: int = 0) -> dict:
        return {
            "player": {"current_hp": hp, "max_hp": 50, "money": money},
            "route_options": [
                {"route_index": 0, "destination": "shop"},
                {"route_index": 1, "destination": "battle"},
                {"route_index": 2, "destination": "rest"},
            ],
            "relics": [
                {"owned": False, "price": 20},
            ],
            "deck_rule": {"can_remove": False},
            "rest_heal": {"available": True},
        }

    @staticmethod
    def _route_profile() -> dict:
        return {
            "route_policy": {
                "low_hp_rest_threshold": 25,
                "force_low_hp_rest": True,
                "avoid_unactionable_shop": True,
            }
        }

    def test_low_hp_route_policy_forces_offered_rest(self) -> None:
        decision = resolve_mcp_route_choice(
            self._route_state(22),
            1,
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 2)
        self.assertTrue(decision["overridden"])
        self.assertEqual(decision["reason"], "low_hp_rest_priority")

    def test_low_hp_route_policy_respects_heal_cooldown(self) -> None:
        state = self._route_state(22)
        state["rest_heal"] = {
            "available": False,
            "cooldown_remaining": 1,
        }
        decision = resolve_mcp_route_choice(
            state,
            1,
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 1)
        self.assertFalse(decision["overridden"])

    def test_unactionable_shop_redirects_to_battle(self) -> None:
        decision = resolve_mcp_route_choice(
            self._route_state(50),
            0,
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 1)
        self.assertEqual(
            decision["reason"],
            "unactionable_shop_redirected_to_battle",
        )

    def test_affordable_shop_remains_selectable(self) -> None:
        decision = resolve_mcp_route_choice(
            self._route_state(50, money=20),
            0,
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 0)
        self.assertFalse(decision["overridden"])

    @staticmethod
    def _stage_choice_state(
        destinations: list[str],
        hp: int = 50,
        money: int = 20,
        can_upgrade: bool = True,
    ) -> dict:
        return {
            "player": {
                "current_hp": hp,
                "max_hp": 50,
                "money": money,
            },
            "route_options": [
                {
                    "route_index": index,
                    "destination": destination,
                }
                for index, destination in enumerate(destinations)
            ],
            "deck_balls": [
                {
                    "can_upgrade": can_upgrade,
                    "status": {"attack": 1},
                }
            ],
            "catalog_balls": [{"index": 0}],
            "relics": [
                {
                    "index": 0,
                    "name": "Power Core",
                    "owned": False,
                    "price": 20,
                }
            ],
            "deck_rule": {"can_remove": False},
            "rest_heal": {"available": True},
        }

    @staticmethod
    def _wanted_rewards(*first: str) -> list[str]:
        return [
            *first,
            *(
                reward
                for reward in (
                    "money",
                    "new_ball",
                    "ball_upgrade",
                    "hp_recovery",
                    "relic",
                )
                if reward not in first
            ),
        ]

    def test_stage_choice_context_exposes_current_need_signals(
        self,
    ) -> None:
        context = build_stage_choice_context(
            self._stage_choice_state(
                ["battle", "rest", "shop"],
                hp=20,
                money=0,
            ),
            self._route_profile(),
        )

        self.assertTrue(context["need_signals"]["hp_recovery"])
        self.assertTrue(context["need_signals"]["money"])
        self.assertFalse(context["need_signals"]["relic"])
        self.assertEqual(
            context["reward_route_matches"]["hp_recovery"],
            [1],
        )
        self.assertEqual(
            context["recommended_wanted_rewards"][:2],
            ["hp_recovery", "ball_upgrade"],
        )

    def test_need_signals_reorder_fixed_wanted_rewards(self) -> None:
        effective = build_dynamic_wanted_reward_order(
            self._wanted_rewards(),
            {
                "money": False,
                "new_ball": True,
                "ball_upgrade": False,
                "hp_recovery": True,
                "relic": True,
            },
        )

        self.assertEqual(
            effective,
            [
                "hp_recovery",
                "relic",
                "new_ball",
                "money",
                "ball_upgrade",
            ],
        )

    def test_current_needs_override_stale_requested_order(
        self,
    ) -> None:
        decision = resolve_mcp_stage_choice(
            self._stage_choice_state(
                ["shop", "battle", "rest"],
                money=0,
            ),
            self._wanted_rewards("relic", "money"),
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 2)
        self.assertEqual(
            decision["matched_wanted_reward"],
            "ball_upgrade",
        )
        self.assertEqual(
            decision["effective_wanted_rewards"][0],
            "ball_upgrade",
        )

    def test_missing_recovery_route_uses_next_available_want(
        self,
    ) -> None:
        decision = resolve_mcp_stage_choice(
            self._stage_choice_state(
                ["shop", "battle", "battle"],
                hp=20,
            ),
            self._wanted_rewards("hp_recovery", "relic"),
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 0)
        self.assertEqual(decision["matched_wanted_reward"], "relic")
        self.assertEqual(
            decision["skipped_wanted_rewards"],
            ["hp_recovery"],
        )

    def test_ball_upgrade_prefers_immediate_rest_reward(self) -> None:
        decision = resolve_mcp_stage_choice(
            self._stage_choice_state(
                ["battle", "rest", "shop"],
                money=0,
            ),
            self._wanted_rewards("ball_upgrade"),
            self._route_profile(),
        )

        self.assertEqual(decision["effective_route_index"], 1)
        self.assertEqual(
            decision["matched_wanted_reward"],
            "ball_upgrade",
        )

    def test_low_hp_safety_still_overrides_reward_order(self) -> None:
        decision = resolve_mcp_stage_choice(
            self._stage_choice_state(
                ["battle", "shop", "rest"],
                hp=20,
            ),
            self._wanted_rewards("money"),
            self._route_profile(),
        )

        self.assertEqual(decision["priority_route_index"], 2)
        self.assertEqual(decision["effective_route_index"], 2)
        self.assertEqual(decision["reason"], "low_hp_rest_priority")

    def test_stage_choice_rejects_incomplete_want_order(self) -> None:
        with self.assertRaises(GameBridgeError):
            resolve_mcp_stage_choice(
                self._stage_choice_state(["battle", "rest", "shop"]),
                ["money", "relic"],
                self._route_profile(),
            )

    @staticmethod
    def _relic_state(
        hp: int,
        attacks: list[int],
        owned_names: tuple[str, ...] = (),
        attack_bonus: int = 0,
    ) -> dict:
        names = (
            "Power Core",
            "Guard Core",
            "Impact Accelerator",
            "Bank Shot",
            "Emergency Repair Kit",
        )
        return {
            "scene": "shop",
            "player": {
                "current_hp": hp,
                "max_hp": 50,
                "money": 20,
            },
            "deck_balls": [
                {"status": {"attack": attack}}
                for attack in attacks
            ],
            "relic_effects": {
                "all_ball_attack_bonus": attack_bonus,
            },
            "relics": [
                {
                    "index": index,
                    "name": name,
                    "price": 20,
                    "owned": name in owned_names,
                }
                for index, name in enumerate(names)
            ],
        }

    @staticmethod
    def _relic_profile() -> dict:
        return {
            "relic_policy": {
                "low_hp_ratio": 0.5,
                "minimum_average_attack": 2.0,
                "low_hp_priority": [
                    "Emergency Repair Kit",
                    "Guard Core",
                ],
                "attack_priority": [
                    "Power Core",
                    "Impact Accelerator",
                    "Bank Shot",
                ],
            }
        }

    def test_low_hp_prioritizes_recovery_relic(self) -> None:
        decision = resolve_mcp_relic_choice(
            self._relic_state(20, [1, 2]),
            0,
            self._relic_profile(),
        )

        self.assertEqual(decision["effective_relic_index"], 4)
        self.assertTrue(decision["overridden"])
        self.assertEqual(
            decision["reason"],
            "low_hp_survival_priority",
        )

    def test_low_hp_falls_back_to_defense_relic(self) -> None:
        decision = resolve_mcp_relic_choice(
            self._relic_state(
                20,
                [1, 2],
                owned_names=("Emergency Repair Kit",),
            ),
            0,
            self._relic_profile(),
        )

        self.assertEqual(decision["effective_relic_index"], 1)
        self.assertEqual(
            decision["effective_relic_name"],
            "Guard Core",
        )

    def test_low_attack_prioritizes_attack_relic(self) -> None:
        decision = resolve_mcp_relic_choice(
            self._relic_state(50, [1, 2]),
            1,
            self._relic_profile(),
        )

        self.assertEqual(decision["effective_relic_index"], 0)
        self.assertEqual(decision["reason"], "low_attack_priority")
        self.assertAlmostEqual(
            decision["average_deck_attack"],
            1.5,
        )

    def test_sufficient_attack_keeps_requested_relic(self) -> None:
        decision = resolve_mcp_relic_choice(
            self._relic_state(50, [1, 2], attack_bonus=1),
            3,
            self._relic_profile(),
        )

        self.assertEqual(decision["effective_relic_index"], 3)
        self.assertFalse(decision["overridden"])
        self.assertEqual(
            decision["reason"],
            "requested_relic_allowed",
        )

    def test_stage_layout_tools_expose_composition_and_positions(
        self,
    ) -> None:
        tools = self._list_tools()
        set_layout = next(
            tool
            for tool in tools
            if tool.name == "set_next_stage_layout"
        )
        clear_layout = next(
            tool
            for tool in tools
            if tool.name == "clear_next_stage_layout"
        )
        properties = set_layout.inputSchema["properties"]
        required = set_layout.inputSchema["required"]
        self.assertIn("enemies", properties)
        self.assertIn("stage_type", properties)
        self.assertIn("difficulty", properties)
        self.assertIn("par", properties)
        self.assertIn("layout_id", properties)
        self.assertIn("enemies", required)
        self.assertIn(
            "現在進行中の戦闘には影響しません",
            set_layout.description,
        )
        self.assertIn(
            "通常のステージ抽選",
            clear_layout.description,
        )

    def test_ball_tools_describe_current_deck_rules(
        self,
    ) -> None:
        tools = self._list_tools()
        upgrade_ball = next(
            tool for tool in tools if tool.name == "upgrade_ball"
        )
        remove_ball = next(
            tool for tool in tools if tool.name == "remove_ball"
        )
        self.assertIn("can_upgrade", upgrade_ball.description)
        self.assertIn("最低5個", remove_ball.description)

    def test_buy_relic_accepts_catalog_index(self) -> None:
        tools = self._list_tools()
        buy_relic = next(
            tool for tool in tools if tool.name == "buy_relic"
        )
        properties = buy_relic.inputSchema["properties"]
        required = buy_relic.inputSchema["required"]
        self.assertIn("relic_index", properties)
        self.assertIn("relic_index", required)
        self.assertIn("HP", buy_relic.description)
        self.assertIn("平均attack", buy_relic.description)


if __name__ == "__main__":
    unittest.main()
