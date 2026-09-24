from __future__ import annotations

import asyncio
import copy
import unittest
from pathlib import Path

from server import create_server
from shot_planner import load_player_profiles


def candidate(candidate_id: str, contact_kind: str, *, offer_index: int = 0,
              target_id: str = "boss") -> dict:
    return {
        "candidate_id": candidate_id, "offer_index": offer_index,
        "target_id": target_id, "contact_kind": contact_kind,
        "power": 4.0, "score": 10.0, "prediction_complete": True,
    }


class BossStore:
    def __init__(self, candidates: list[dict]):
        self.commands = []
        self.candidates = candidates
        self.state = {
            "scene": "battle", "game_state": "aiming_direction", "sequence": 1,
            "available_actions": ["select_ball", "fire_shot", "evaluate_boss_shots", "fire_boss_shot"],
            "boss_state": {"target_id": "enemy:0", "hp": 60},
            "enemies": [{"target_id": "enemy:0", "enemy_id": "enemy_boss", "hp": 60,
                         "position": {"x": 0, "z": 20}, "radius": 3}],
            "break_balls": [], "relics": [], "catalog_balls": [],
            "offered_balls": [{"index": 0, "instance_id": 7,
                               "definition_id": "player_standard", "selected": True}],
            "deck_balls": [],
            "player": {"current_hp": 40, "max_hp": 50, "money": 0,
                       "position": {"x": 0, "z": 0}},
            "table": {"walls": [], "pockets": []},
        }

    def read_state(self):
        return copy.deepcopy(self.state)

    def submit_command(self, action, arguments):
        self.commands.append((action, arguments))
        if action == "evaluate_boss_shots":
            return {"ok": True, "evaluation": {
                "model": "boss_shared_ccd_v1", "state_key": "current-key",
                "recommended": self.candidates[0] if self.candidates else None,
                "choices": self.candidates,
                "offer_choices": self.candidates[:1],
            }}
        return {"ok": True, "action": action}


class BossMcpContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.players = load_player_profiles(Path(__file__).parent / "player_profiles.json")

    def make_server(self, candidates):
        store = BossStore(candidates)
        return create_server(store, "127.0.0.1", 8765, self.players, "intermediate"), store

    def test_exposed_boss_actions_can_be_called_as_mcp_tools(self):
        server, store = self.make_server([candidate("0:43", "bank")])

        async def run():
            tools = {tool.name for tool in await server.list_tools()}
            _, read = await server.call_tool("get_game_state", {})
            _, evaluation = await server.call_tool("evaluate_boss_shots", {})
            plan = evaluation["result"]["recommended"]
            _, fired = await server.call_tool("fire_boss_shot", {
                "candidate_id": plan["candidate_id"],
                "state_key": evaluation["result"]["state_key"],
            })
            return tools, read["result"], evaluation["result"], fired["result"]

        tools, state, evaluation, fired = asyncio.run(run())
        self.assertTrue(set(state["available_actions"]).issubset(tools))
        self.assertEqual(state["shot_tactics"]["use_tool"], "fire_boss_shot")
        self.assertEqual(state["recommended_action"], {
            "action": "fire_boss_shot", "candidate_id": "0:43", "state_key": "current-key",
        })
        self.assertEqual(state["boss_state"]["target_id"], "enemy:0")
        self.assertEqual(state["build_shot_choices"]["recommended"]["target_id"], "enemy:0")
        self.assertEqual(evaluation["recommended"]["simulation_target_id"], "boss")
        self.assertTrue(fired["ok"])
        self.assertEqual(store.commands[-1][0], "fire_boss_shot")

    def test_moving_boss_does_not_advertise_a_current_shot_action(self):
        server, store = self.make_server([candidate("0:43", "bank")])
        store.state["game_state"] = "balls_moving"
        store.state["available_actions"] = ["set_next_stage_layout"]

        async def run():
            _, read = await server.call_tool("get_game_state", {})
            return read["result"]

        state = asyncio.run(run())
        self.assertIsNone(state["shot_tactics"]["use_tool"])
        self.assertIsNone(state["recommended_action"])
        self.assertIsNone(state["boss_shot_choices"])

    def test_legacy_direct_and_bank_fire_matching_candidate(self):
        for kind in ("direct", "bank"):
            with self.subTest(kind=kind):
                server, store = self.make_server([candidate("0:43", kind)])

                async def run():
                    _, result = await server.call_tool("fire_shot", {
                        "target_id": "enemy:0", "shot_type": kind,
                    })
                    return result["result"]

                result = asyncio.run(run())
                self.assertTrue(result["ok"])
                self.assertEqual(result["shot_plan"]["shot_type"], kind)
                self.assertEqual(store.commands[-1][0], "fire_boss_shot")

    def test_bank_only_direct_request_returns_retryable_plan(self):
        server, store = self.make_server([candidate("0:43", "bank")])

        async def run():
            _, result = await server.call_tool("fire_shot", {
                "target_id": "enemy:0", "shot_type": "direct",
            })
            return result["result"]

        result = asyncio.run(run())
        self.assertFalse(result["ok"])
        self.assertEqual(result["reason"], "requested_boss_plan_unavailable")
        self.assertTrue(result["retryable"])
        self.assertEqual(result["recommended_plan"]["shot_type"], "bank")
        self.assertEqual(result["recommended_plan"]["candidate_id"], "0:43")
        self.assertNotIn("fire_boss_shot", [action for action, _ in store.commands])

    def test_legacy_auto_uses_recommended_bank_plan(self):
        server, store = self.make_server([candidate("0:43", "bank")])

        async def run():
            _, result = await server.call_tool("fire_shot", {"target_id": "enemy:0"})
            return result["result"]

        result = asyncio.run(run())
        self.assertTrue(result["ok"])
        self.assertEqual(result["shot_plan"]["shot_type"], "bank")
        self.assertEqual(store.commands[-1][1]["candidate_id"], "0:43")

    def test_legacy_auto_uses_global_recommendation_even_for_another_offer(self):
        server, store = self.make_server([
            candidate("1:4", "bank", offer_index=1),
            candidate("0:2", "direct", offer_index=0),
        ])

        async def run():
            _, result = await server.call_tool("fire_shot", {"target_id": "enemy:0"})
            return result["result"]

        result = asyncio.run(run())
        self.assertTrue(result["ok"])
        self.assertEqual(result["shot_plan"]["boss_evaluation"]["candidate_id"], "1:4")
        self.assertEqual(store.commands[-1][1]["candidate_id"], "1:4")

    def test_reposition_candidate_has_no_entity_target_id(self):
        server, _ = self.make_server([candidate("0:9", "reposition", target_id="position")])

        async def run():
            _, result = await server.call_tool("evaluate_boss_shots", {})
            return result["result"]["recommended"]

        plan = asyncio.run(run())
        self.assertIsNone(plan["target_id"])
        self.assertEqual(plan["simulation_target_id"], "position")


if __name__ == "__main__":
    unittest.main()
