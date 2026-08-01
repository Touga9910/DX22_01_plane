from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path

from bridge_store import GameBridgeStore
from server import create_server
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


if __name__ == "__main__":
    unittest.main()
