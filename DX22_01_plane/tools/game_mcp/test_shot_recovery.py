from __future__ import annotations

import asyncio
import copy
import unittest
from pathlib import Path
from unittest.mock import patch

from build_shot_evaluator import build_joint_shot_context, evaluate_build_shots
from build_profiles import compose_control_profile, load_build_profiles, profile_settings_hash
from server import create_server
from collect_fixed_balance_runs import play_current_run
from shot_planner import build_tactical_shot_context, load_player_profiles
from shot_recovery import fallback_shot_candidates, recommended_action


def ball(index=0, *, friction=.02, selected=True):
    return {"index": index, "instance_id": index + 7,
            "definition_id": "player_standard" if friction == .02 else "player_anchor",
            "selected": selected,
            "status": {"attack": 2, "mass": 2, "radius": 2.4,
                       "friction": friction, "restitution": .8,
                       "pierce": False, "anchor": False}}


def enemy(index, z):
    return {"target_id": f"enemy:{index}", "enemy_id": "enemy_normal",
            "position": {"x": 0, "z": z}, "radius": 2.4,
            "hp": 8, "max_hp": 8, "attack": 1, "defense": 0}


def battle(balls, enemies):
    return {"scene": "battle", "game_state": "aiming_direction", "sequence": 1,
            "available_actions": ["select_ball", "fire_shot"],
            "player": {"position": {"x": 0, "z": 0}, "current_hp": 40,
                       "max_hp": 50, "money": 0},
            "offered_balls": balls, "deck_balls": balls,
            "catalog_balls": [], "enemies": enemies, "relics": [],
            "table": {"walls": [], "pockets": []}}


class FakeStore:
    def __init__(self, snapshot):
        self.snapshot = copy.deepcopy(snapshot)
        self.commands = []

    def read_state(self):
        return copy.deepcopy(self.snapshot)

    def submit_command(self, action, arguments):
        self.commands.append((action, arguments))
        if action == "select_ball":
            for offer in self.snapshot["offered_balls"]:
                offer["selected"] = offer["index"] == arguments["offer_index"]
            self.snapshot["sequence"] += 1
        return {"ok": True, "action": action}


class ShotRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).parent
        cls.players = load_player_profiles(root / "player_profiles.json")
        cls.builds, _ = load_build_profiles(root / "build_profiles.json")
        build = dict(cls.builds["standard"], id="standard",
                     settings_hash=profile_settings_hash(cls.builds["standard"]))
        cls.intermediate = compose_control_profile(cls.players["intermediate"], build)
        cls.beginner = compose_control_profile(cls.players["beginner"], build)

    def make_server(self, snapshot):
        store = FakeStore(snapshot)
        server = create_server(store, "127.0.0.1", 8765, self.players,
                               "intermediate", self.builds, "standard")
        return server, store

    def test_blocked_target_is_not_recommended_and_other_target_is_used(self):
        state = battle([ball()], [enemy(0, 10), enemy(1, 30)])
        tactics = build_tactical_shot_context(state, self.intermediate)
        self.assertEqual(tactics["recommended_target_id"], "enemy:0")
        self.assertTrue(tactics["recommendations"][0]["reachable"])
        self.assertFalse(next(c for c in tactics["recommendations"]
                              if c["target_id"] == "enemy:1")["reachable"])
        self.assertEqual(fallback_shot_candidates(
            state, self.intermediate, "enemy:1", "damage", "direct",
        )[0]["target_id"], "enemy:0")
        server, store = self.make_server(state)

        async def fire():
            _, result = await server.call_tool("fire_shot", {
                "target_id": "enemy:1", "shot_goal": "damage", "shot_type": "direct",
            })
            return result["result"]

        result = asyncio.run(fire())
        self.assertTrue(result["ok"])
        self.assertEqual(result["shot_plan"]["target_id"], "enemy:0")
        self.assertTrue(result["shot_plan"]["fallback"]["changed"])
        self.assertEqual(store.commands[-1][0], "fire_shot")

    def test_pocket_can_beat_damage(self):
        state = battle([ball()], [enemy(0, 15)])
        state["player"]["current_hp"] = 4
        state["enemies"][0].update(hp=30, max_hp=30, attack=8)
        state["table"]["pockets"] = [{"index": 0, "position": {"x": 0, "z": 25}, "radius": 3}]
        choices = fallback_shot_candidates(state, self.intermediate, "enemy:0")
        self.assertEqual(choices[0]["shot_goal"], "pocket")

    def test_bank_route_is_available_only_when_level_allows_it(self):
        state = battle([ball()], [enemy(0, 10), enemy(1, 30)])
        state["table"]["walls"] = [{"start": {"x": 15, "z": -20},
                                     "end": {"x": 15, "z": 50}}]
        self.assertFalse(evaluate_build_shots(
            state, self.intermediate, target_id="enemy:1", shot_type="direct"))
        self.assertTrue(evaluate_build_shots(
            state, self.intermediate, target_id="enemy:1", shot_type="bank"))
        self.assertFalse(evaluate_build_shots(
            state, self.beginner, target_id="enemy:1", shot_type="bank"))
        fallback = fallback_shot_candidates(
            state, self.intermediate, "enemy:1", "damage", "direct",
        )
        self.assertEqual((fallback[0]["target_id"], fallback[0]["shot_type"]),
                         ("enemy:0", "direct"))
        self.assertTrue(any(c["target_id"] == "enemy:1" and c["shot_type"] == "bank"
                            for c in fallback))

    def test_unreachable_selected_ball_switches_and_recomputes(self):
        state = battle([ball(friction=.5), ball(1, selected=False)], [enemy(0, 200)])
        tactics = build_tactical_shot_context(state, self.intermediate)
        joint = build_joint_shot_context(state, self.intermediate)
        self.assertIsNone(tactics["recommended_target_id"])
        self.assertEqual(recommended_action(state, tactics, joint)["ball_index"], 1)
        server, store = self.make_server(state)

        async def select_and_refresh():
            _, before = await server.call_tool("get_game_state", {})
            action = before["result"]["recommended_action"]
            _, selected = await server.call_tool("select_ball", {"offer_index": action["ball_index"]})
            _, after = await server.call_tool("get_game_state", {})
            _, duplicate = await server.call_tool("select_ball", {"offer_index": action["ball_index"]})
            return before["result"], selected["result"], after["result"], duplicate["result"]

        before, selected, after, duplicate = asyncio.run(select_and_refresh())
        self.assertIsNone(before["shot_tactics"]["recommended_target_id"])
        self.assertTrue(selected["shot_tactics_refresh_required"])
        self.assertEqual(after["shot_tactics"]["selected_ball_index"], 1)
        self.assertEqual(after["shot_tactics"]["recommended_target_id"], "enemy:0")
        self.assertEqual(after["recommended_action"]["action"], "fire_shot")
        self.assertEqual(duplicate["status"], "already_selected")
        self.assertEqual(len(store.commands), 1)

    def test_all_balls_unreachable_returns_explicit_result(self):
        second = ball(1, friction=.5, selected=False)
        state = battle([ball(friction=.5), second], [enemy(0, 200)])
        server, store = self.make_server(state)

        async def inspect():
            _, read = await server.call_tool("get_game_state", {})
            _, fired = await server.call_tool("fire_shot", {"target_id": "enemy:0"})
            return read["result"], fired["result"]

        read, fired = asyncio.run(inspect())
        self.assertIsNone(read["shot_tactics"]["recommended_target_id"])
        self.assertEqual(read["recommended_action"]["reason"], "no_reachable_shot")
        self.assertEqual(fired["reason"], "no_reachable_shot")
        self.assertFalse(fired["retryable"])
        self.assertEqual(store.commands, [])

    def test_unreachable_shot_returns_next_ball_without_tool_error(self):
        state = battle([ball(friction=.5), ball(1, selected=False)], [enemy(0, 200)])
        server, store = self.make_server(state)

        async def fire():
            _, result = await server.call_tool("fire_shot", {"target_id": "enemy:0"})
            return result["result"]

        result = asyncio.run(fire())
        self.assertFalse(result["ok"])
        self.assertEqual(result["reason"], "unreachable_shot")
        self.assertTrue(result["retryable"])
        self.assertEqual(result["next_action"]["action"], "select_ball")
        self.assertEqual(result["next_action"]["ball_index"], 1)
        self.assertEqual(store.commands, [])

    def test_simulation_state_is_retryable_without_firing(self):
        state = battle([ball()], [enemy(0, 10)])
        state["game_state"] = "ball_moving"
        state["available_actions"] = ["set_next_stage_layout"]
        server, store = self.make_server(state)

        async def inspect():
            _, read = await server.call_tool("get_game_state", {})
            _, fired = await server.call_tool("fire_shot", {"target_id": "enemy:0"})
            return read["result"], fired["result"]

        read, fired = asyncio.run(inspect())
        self.assertIsNone(read["recommended_action"])
        self.assertEqual(fired["reason"], "state_not_ready")
        self.assertTrue(fired["retryable"])
        self.assertEqual(store.commands, [])

    def test_autoplay_refreshes_after_selection_and_crosses_two_stages(self):
        base = battle([ball(), ball(1, selected=False)], [enemy(0, 15)])
        base["build_decision"] = {"active_build_id": "standard"}
        base["balance_validation"] = {"cleared_stage_count": 0}
        first = copy.deepcopy(base)
        first["recommended_action"] = {"action": "select_ball", "ball_index": 1}
        first["shot_tactics"] = {"recommendations": []}
        second = copy.deepcopy(base)
        second["sequence"] = 2
        second["offered_balls"][0]["selected"] = False
        second["offered_balls"][1]["selected"] = True
        second["recommended_action"] = {"action": "fire_shot", "target_id": "enemy:0",
                                         "shot_goal": "damage", "shot_type": "direct"}
        second["shot_tactics"] = {"recommendations": [{"target_id": "enemy:0", "reachable": True}]}
        simulating = copy.deepcopy(second)
        simulating["game_state"] = "ball_moving"
        simulating["available_actions"] = ["set_next_stage_layout"]
        simulating["recommended_action"] = None
        stage_two = copy.deepcopy(second)
        stage_two["sequence"] = 4
        stage_two["balance_validation"]["cleared_stage_count"] = 1
        finished = copy.deepcopy(stage_two)
        finished["scene"] = "result"
        finished["run_progress"] = {"final_boss_defeated": True}
        finished["balance_validation"]["cleared_stage_count"] = 2
        states = iter([first, second, second, simulating, stage_two, finished])
        calls = []

        async def fake_get_state(_session):
            return next(states)

        async def fake_call(_session, name, arguments=None):
            calls.append((name, arguments or {}))
            return {"ok": True}

        async def fake_sleep(_seconds):
            return None

        with patch("collect_fixed_balance_runs.get_state", fake_get_state), \
             patch("collect_fixed_balance_runs.call", fake_call), \
             patch("collect_fixed_balance_runs.asyncio.sleep", fake_sleep):
            result = asyncio.run(play_current_run(
                object(), "intermediate", "standard", self.builds["standard"],
                self.builds, "test-hash", 12, 1,
            ))
        self.assertTrue(result["completed"])
        self.assertEqual(result["cleared_stages"], 2)
        self.assertEqual([name for name, _ in calls],
                         ["select_ball", "fire_shot", "fire_shot"])
        self.assertEqual(calls[0][1]["offer_index"], 1)
        self.assertEqual(calls[1][1]["target_id"], "enemy:0")


if __name__ == "__main__":
    unittest.main()
