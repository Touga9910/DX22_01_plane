from __future__ import annotations

import copy
import asyncio
import random
import unittest
from pathlib import Path

from build_profiles import compose_control_profile, load_build_profiles, profile_settings_hash
from build_shot_evaluator import build_joint_shot_context, evaluate_build_shots
from shot_planner import load_player_profiles, plan_build_shot
from bridge_store import GameBridgeError
from server import create_server


def ball(kind="standard", **changes):
    stats = dict(attack=2, defense=0, mass=2, radius=2.4, friction=.02,
                 restitution=.8, pierce=False, anchor=False)
    if kind == "pierce":
        stats.update(attack=1, pierce=True, pierceMaxUses=2, pierceSpeedRetention=.75)
    if kind == "heavy":
        stats.update(mass=6, knockbackTransfer=1.3)
    if kind == "anchor":
        stats.update(mass=6, defense=2, friction=.06, restitution=.35,
                     anchor=True, anchorBrakeMultiplier=1.4, anchorStopSpeedSquared=.06)
    if kind == "bounce":
        stats.update(restitution=1.0)
    stats.update(changes)
    return {"index": 0, "instance_id": 1, "definition_id": "player_" + kind,
            "selected": True, "status": stats}


def enemy(index, x, z, hp=8, attack=1, **kwargs):
    return dict(target_id=f"enemy:{index}", enemy_id="enemy_normal",
                position={"x": x, "z": z}, radius=2.4, hp=hp, max_hp=hp,
                attack=attack, defense=0, mass=1, friction=.02, **kwargs)


def state(current_ball, enemies):
    return {"player": {"position": {"x": 0, "z": 0}, "current_hp": 40, "max_hp": 50},
            "offered_balls": [current_ball], "enemies": enemies,
            "relics": [], "table": {"walls": [], "pockets": []}}


class BuildShotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).parent
        cls.profiles, _ = load_build_profiles(root / "build_profiles.json")
        cls.players = load_player_profiles(root / "player_profiles.json")

    def profile(self, kind):
        build = dict(self.profiles[kind], id=kind, settings_hash=profile_settings_hash(self.profiles[kind]))
        return compose_control_profile(self.players["intermediate"], build)

    def test_minion_stack_cost_respects_trace_count(self):
        driver = ball()
        driver["definition_id"] = "player_trace_driver"
        driver["status"].update(pierceStackConsumeLimit=9, attack=1)
        s = state(driver, [enemy(0, 0, 15)])
        s["ball_synergies"] = {"pierce_stacks": 9, "pierce_traces": []}
        without_trace = evaluate_build_shots(
            s, self.profile("pierce"), shot_type="direct")[0]
        self.assertEqual(
            without_trace["special_ability_estimates"]["pierce_stacks_consumed"], 0)

        s["ball_synergies"]["pierce_traces"] = [{
            "start": [0, 0, 0], "direction": [0, 0, 1]}]
        with_one_trace = evaluate_build_shots(
            s, self.profile("pierce"), shot_type="direct")[0]
        self.assertEqual(
            with_one_trace["special_ability_estimates"]["pierce_stacks_consumed"], 3)

    def test_pierce_selects_front_of_line_and_counts_followups(self):
        s = state(ball("pierce"), [enemy(0, 0, 15), enemy(1, 0, 30), enemy(2, 0, 45), enemy(3, 25, 15)])
        c = evaluate_build_shots(s, self.profile("pierce"), shot_type="direct")[0]
        self.assertEqual(c["target_id"], "enemy:0")
        self.assertEqual(c["metrics"]["pierce_followups"], 2)
        self.assertEqual(set(c["expected_damage_by_target"]), {"enemy:0", "enemy:1", "enemy:2"})

    def test_pierce_limit_and_retention_change_reachable_hits(self):
        s = state(ball("pierce", pierceMaxUses=1), [enemy(0, 0, 15), enemy(1, 0, 35), enemy(2, 0, 70)])
        limited = evaluate_build_shots(s, self.profile("pierce"), shot_type="direct", fixed_power=3)[0]
        self.assertEqual(limited["metrics"]["pierce_followups"], 1)
        s["offered_balls"][0]["status"]["pierceSpeedRetention"] = .05
        slowed = evaluate_build_shots(s, self.profile("pierce"), shot_type="direct", fixed_power=3)[0]
        self.assertEqual(slowed["metrics"]["pierce_followups"], 0)
        s["relics"] = [{"index": 7, "owned": True}]
        charged = evaluate_build_shots(s, self.profile("pierce"), shot_type="direct", fixed_power=3)[0]
        self.assertEqual(charged["metrics"]["pierce_followups"], 2)

    def test_scattered_enemies_do_not_receive_line_bonus(self):
        s = state(ball("pierce"), [enemy(0, 0, 15), enemy(1, 25, 30), enemy(2, -25, 45)])
        c = evaluate_build_shots(s, self.profile("pierce"), target_id="enemy:0", shot_type="direct")[0]
        self.assertEqual(c["metrics"]["pierce_followups"], 0)

    def test_heavy_aims_off_center_to_push_enemy_into_another(self):
        s = state(ball("heavy"), [enemy(0, 0, 20), enemy(1, 12, 40)])
        c = evaluate_build_shots(s, self.profile("heavy"), target_id="enemy:0", shot_type="direct")[0]
        self.assertEqual(c["metrics"]["chain_collisions"], 1)
        self.assertNotAlmostEqual(c["aim_point"]["x"], 0)
        self.assertGreater(c["expected_damage_by_target"].get("enemy:1", 0), 0)

    def test_heavy_transfer_and_mass_limit_push_reach(self):
        s = state(ball("heavy", mass=1, knockbackTransfer=.01), [enemy(0, 0, 20), enemy(1, 0, 70)])
        c = evaluate_build_shots(s, self.profile("heavy"), target_id="enemy:0", shot_type="direct", fixed_power=3)[0]
        self.assertEqual(c["metrics"]["chain_collisions"], 0)
        s["offered_balls"][0]["status"].update(mass=6, knockbackTransfer=1.3)
        c = evaluate_build_shots(s, self.profile("heavy"), target_id="enemy:0", shot_type="direct", fixed_power=3)[0]
        self.assertEqual(c["metrics"]["chain_collisions"], 1)

    def test_anchor_rejects_unreachable_shots_and_keeps_damage_floor(self):
        s = state(ball("anchor", friction=.5, defense=99), [enemy(0, 0, 200)])
        self.assertFalse(evaluate_build_shots(s, self.profile("anchor"), fixed_power=3))
        s["enemies"][0]["position"]["z"] = 6
        c = evaluate_build_shots(s, self.profile("anchor"), fixed_power=3)[0]
        self.assertEqual(c["expected_incoming_damage"], 1)

    def test_anchor_prefers_reachable_low_power_and_reports_stop(self):
        s = state(ball("anchor"), [enemy(0, 0, 15)])
        c = evaluate_build_shots(s, self.profile("anchor"), shot_type="direct")[0]
        self.assertEqual(c["power"], 3)
        self.assertGreater(c["metrics"]["stable_stop"], 0)
        self.assertIn("z", c["estimated_stop_position"])
        self.assertLess(c["estimated_stop_position"]["z"], 15)
        self.assertEqual(c["followup_hits"], 0)

    def test_bank_relic_gain_and_beginner_restriction(self):
        s = state(ball("bounce"), [enemy(0, 0, 20)])
        s["table"]["walls"] = [{"start": {"x": 15, "z": -20}, "end": {"x": 15, "z": 50}}]
        s["relics"] = [{"index": 3, "owned": True}, {"index": 8, "owned": True}]
        c = evaluate_build_shots(s, self.profile("bounce"))[0]
        self.assertEqual(c["shot_type"], "bank")
        self.assertGreater(c["metrics"]["bank_damage_gain"], 0)
        beginner = dict(self.profile("bounce"), allow_bank_shots=False)
        self.assertTrue(all(c["shot_type"] == "direct" for c in evaluate_build_shots(s, beginner)))

    def test_blocked_target_cannot_be_used_as_first_contact(self):
        s = state(ball(), [enemy(0, 0, 10), enemy(1, 0, 30)])
        self.assertFalse(evaluate_build_shots(s, self.profile("standard"), target_id="enemy:1", shot_type="direct"))

    def test_survival_and_pocket_control_for_anchor(self):
        s = state(ball("anchor"), [enemy(0, 0, 15, hp=30, attack=8)])
        s["player"]["current_hp"] = 4
        s["table"]["pockets"] = [{"index": 0, "position": {"x": 0, "z": 25}, "radius": 3}]
        c = evaluate_build_shots(s, self.profile("anchor"), shot_type="direct")[0]
        self.assertEqual(c["shot_goal"], "pocket")
        self.assertEqual(c["expected_incoming_damage"], 0)

    def test_joint_choices_use_layout_instead_of_fixed_ball_priority(self):
        s = state(ball("standard", attack=1), [enemy(0, 0, 15), enemy(1, 0, 30), enemy(2, 0, 45)])
        pierce = ball("pierce")
        pierce.update(index=1, instance_id=2, selected=False)
        s["offered_balls"].append(pierce)
        before = copy.deepcopy(s)
        joint = build_joint_shot_context(s, self.profile("pierce"))
        self.assertEqual(joint["recommended"]["offer_index"], 1)
        self.assertEqual(s, before)

    def test_execution_matches_recommendation_before_seeded_human_error(self):
        s = state(ball("heavy"), [enemy(0, 0, 20), enemy(1, 12, 40)])
        profile = self.profile("heavy")
        expected = evaluate_build_shots(s, profile, target_id="enemy:0")[0]
        a = plan_build_shot(s, "enemy:0", profile, random_source=random.Random(123))
        b = plan_build_shot(s, "enemy:0", profile, random_source=random.Random(123))
        self.assertEqual(a, b)
        self.assertEqual(a["shot_plan"]["build_evaluation"], expected)
        self.assertLess(a["arguments"]["direction_x"], 0)
        self.assertGreater(a["shot_plan"]["human_error"]["power_error_limit_ratio"], 0)

    def test_manual_constraints_and_invalid_target_are_not_overridden(self):
        s = state(ball(), [enemy(0, 0, 20)])
        p = self.profile("standard")
        plan = plan_build_shot(s, "enemy:0", p, power_mode="manual", power=4, shot_type="direct", shot_goal="damage")
        self.assertEqual(plan["shot_plan"]["build_evaluation"]["power"], 4)
        self.assertEqual(plan["shot_plan"]["shot_type"], "direct")
        with self.assertRaises(GameBridgeError):
            plan_build_shot(s, "enemy:missing", p)

    def test_tactical_ai_values_charge_interruption_once(self):
        charging = enemy(0, 0, 15, hp=8, attack=3, can_attack_this_turn=True)
        charging["tactics"] = {
            "charge": {
                "interruptible_now": True,
                "pierce_required_damage": 1,
                "heavy_required_enemy_movement": 4.8,
                "attack_damage_prevented_if_interrupted": 6,
            },
            "board_ability": {"type": "none"},
        }
        s = state(ball("pierce"), [charging])
        c = evaluate_build_shots(
            s, self.profile("pierce"), target_id="enemy:0",
            shot_type="direct", fixed_power=3)[0]
        self.assertEqual(
            c["tactical_outcomes"]["charge_interruptions"], ["enemy:0"])
        self.assertEqual(c["expected_attack_prevention"], 6)
        self.assertEqual(
            c["expected_attack_prevention_by_target"]["enemy:0"]["reason"],
            "charge_interruption")

    def test_tactical_ai_values_resonator_release_and_swarm_drop(self):
        resonator = enemy(0, 0, 15, hp=1, attack=1, can_attack_this_turn=True)
        resonator["tactics"] = {"board_ability": {
            "type": "protection_resonator",
            "resonator_protected_target_count": 2,
            "resonator_damage_reduction": 1,
        }}
        swarm = enemy(1, 10, 20, hp=8, attack=3, can_attack_this_turn=True)
        swarm["tactics"] = {"board_ability": {
            "type": "swarm_amplifier",
            "affected_ally_target_ids": ["enemy:0"],
            "swarm_attack": 3,
            "swarm_attack_after_one_ally_removed": 1,
        }}
        s = state(ball(attack=2), [resonator, swarm])
        c = evaluate_build_shots(
            s, self.profile("standard"), target_id="enemy:0",
            shot_type="direct", fixed_power=3)[0]
        self.assertEqual(c["metrics"]["protection_release"], 2)
        self.assertEqual(
            c["tactical_outcomes"]["swarm_stage_reductions"], ["enemy:1"])
        self.assertIn("防護共鳴を解除", c["reason"])

    def test_existing_ai_variant_disables_new_tactical_bonus(self):
        resonator = enemy(0, 0, 15, hp=1)
        resonator["tactics"] = {"board_ability": {
            "type": "protection_resonator",
            "resonator_protected_target_count": 3,
            "resonator_damage_reduction": 1,
        }}
        s = state(ball(), [resonator])
        s["balance_validation"] = {"tactical_ai": False}
        c = evaluate_build_shots(s, self.profile("standard"))[0]
        self.assertEqual(c["metrics"]["protection_release"], 0)
        self.assertEqual(c["tactical_outcomes"]["protection_releases"], [])

    def test_survival_v2_caps_future_setup_below_reliable_kill(self):
        resonator = enemy(0, -12, 18, hp=20, attack=1)
        resonator["tactics"] = {"board_ability": {
            "type": "protection_resonator",
            "resonator_protected_target_count": 8,
            "resonator_damage_reduction": 1,
        }}
        dangerous = enemy(1, 12, 18, hp=2, attack=3,
                          can_attack_this_turn=True)
        dangerous["tactics"] = {
            "defeat_prevention": {
                "immediate_attack_damage": 3,
                "future_charged_attack_damage": 0,
            },
            "board_ability": {"type": "none"},
        }
        s = state(ball(attack=2), [resonator, dangerous])
        s["balance_validation"] = {
            "tactical_ai": True,
            "tactical_ai_profile": "survival_v2",
        }
        candidates = evaluate_build_shots(
            s, self.profile("standard"), shot_type="direct", fixed_power=3)
        self.assertEqual(candidates[0]["target_id"], "enemy:1")
        setup = next(c for c in candidates if c["target_id"] == "enemy:0")
        self.assertLessEqual(
            setup["future_setup_scored"], setup["future_setup_raw"])

    def test_charge_candidate_generation_and_adoption_are_counted(self):
        charging = enemy(0, 0, 15, hp=8, attack=3,
                         can_attack_this_turn=True)
        charging["tactics"] = {
            "charge": {
                "interruptible_now": True,
                "pierce_required_damage": 1,
                "heavy_required_enemy_movement": 1,
                "attack_damage_prevented_if_interrupted": 6,
            },
            "defeat_prevention": {
                "immediate_attack_damage": 6,
                "future_charged_attack_damage": 0,
            },
            "board_ability": {"type": "none"},
        }
        s = state(ball("heavy"), [charging])
        s["offered_balls"] = [ball(), ball("heavy")]
        s["balance_validation"] = {
            "tactical_ai": True,
            "tactical_ai_profile": "survival_v2",
        }
        context = build_joint_shot_context(s, self.profile("standard"))
        self.assertGreater(
            context["candidate_generation"]["charge_interruption_candidates"], 0)
        self.assertIn(
            "enemy:0",
            context["candidate_generation"]["charge_interruption_target_ids"])
        self.assertTrue(
            context["recommended"]["tactical_outcomes"]["charge_interruptions"])
        self.assertEqual(
            context["recommended"][
                "expected_next_turn_prevention_before_defense"], 6)

    def test_tactical_ai_values_moving_ally_out_of_ability_radius(self):
        target = enemy(0, 0, 15, hp=20, attack=1)
        resonator = enemy(1, 10, 15, hp=8, attack=1)
        resonator["tactics"] = {"board_ability": {
            "type": "protection_resonator",
            "radius": 12,
            "affected_ally_target_ids": ["enemy:0"],
            "resonator_damage_reduction": 1,
        }}
        swarm = enemy(2, -10, 15, hp=8, attack=3, can_attack_this_turn=True)
        swarm["tactics"] = {"board_ability": {
            "type": "swarm_amplifier",
            "radius": 12,
            "affected_ally_target_ids": ["enemy:0"],
            "swarm_attack": 3,
            "swarm_attack_after_one_ally_removed": 1,
        }}
        s = state(ball("heavy", attack=2), [target, resonator, swarm])
        candidates = evaluate_build_shots(
            s, self.profile("heavy"), target_id="enemy:0",
            shot_type="direct", fixed_power=6)
        moved = next(c for c in candidates
                     if c["tactical_outcomes"]["protection_releases"])
        self.assertIn("enemy:0", moved["tactical_outcomes"]["protection_releases"])
        self.assertIn("enemy:2", moved["tactical_outcomes"]["swarm_stage_reductions"])

    def test_standard_values_scope_low_power_and_avoids_overkill_bank_credit(self):
        s = state(ball(), [enemy(0, 0, 15, hp=3)])
        s["relics"] = [{"index": 5, "owned": True}]
        c = evaluate_build_shots(s, self.profile("standard"), shot_type="direct")[0]
        self.assertLessEqual(c["power"], 4)
        self.assertEqual(c["metrics"]["kills"], 1)
        s["relics"] = [{"index": 3, "owned": True}]
        s["enemies"][0]["hp"] = 1
        s["table"]["walls"] = [{"start": {"x": 15, "z": -20}, "end": {"x": 15, "z": 50}}]
        c = evaluate_build_shots(s, self.profile("standard"), shot_type="bank")[0]
        self.assertEqual(c["metrics"]["bank_damage_gain"], 0)

    def test_frontal_guard_and_pocket_risk_are_in_score(self):
        s = state(ball(attack=6), [enemy(0, 0, 15, frontal_damage_multiplier=.5)])
        guarded = evaluate_build_shots(s, self.profile("standard"), shot_type="direct")[0]
        self.assertEqual(guarded["metrics"]["damage"], 3)
        s["player"]["position"]["z"] = 30
        flank = evaluate_build_shots(s, self.profile("standard"), shot_type="direct")[0]
        self.assertEqual(flank["metrics"]["damage"], 6)
        s["table"]["pockets"] = [{"index": 0, "position": {"x": 0, "z": 25}, "radius": 3}]
        risky = evaluate_build_shots(s, self.profile("standard"), shot_type="direct", shot_goal="damage")[0]
        self.assertEqual(risky["self_pocket_risk"], 1)
        self.assertLess(risky["score"], flank["score"])

    def test_mcp_server_publishes_and_executes_the_same_ball_evaluation(self):
        snapshot = state(ball("heavy"), [enemy(0, 0, 20), enemy(1, 12, 40)])
        class Store:
            def read_state(self):
                return copy.deepcopy(snapshot)
            def submit_command(self, action, arguments):
                self.action, self.arguments = action, arguments
                return {"ok": True, "action": action}
        store = Store()
        server = create_server(store, "127.0.0.1", 8765, self.players, "intermediate",
                               initial_build_profile="heavy")
        async def exercise():
            _, read = await server.call_tool("get_game_state", {})
            recommended = read["result"]["build_shot_choices"]["recommended"]
            _, fired = await server.call_tool("fire_shot", {"target_id": recommended["target_id"]})
            return recommended, fired["result"]
        recommended, fired = asyncio.run(exercise())
        self.assertTrue(fired["ok"])
        self.assertEqual(store.action, "fire_shot")
        self.assertEqual(fired["shot_plan"]["build_evaluation"], recommended)
        self.assertIn("score_breakdown", store.arguments["telemetry"]["build_evaluation"])


if __name__ == "__main__":
    unittest.main()
