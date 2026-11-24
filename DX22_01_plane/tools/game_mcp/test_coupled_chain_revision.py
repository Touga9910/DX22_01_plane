import copy
from pathlib import Path
import re
import unittest

from coupled_chain import normal_impact, project
from relic_rules import Relic
from test_build_shot_evaluator import ball, enemy, state
from build_shot_evaluator import evaluate_build_shots


class RelicMappingTests(unittest.TestCase):
    def test_indices_match_game_enum_in_source_order(self):
        source = (Path(__file__).resolve().parents[2] / "GameTypes.h").read_bytes().decode("utf-8", errors="replace")
        block = source.split("enum class RelicType", 1)[1].split("};", 1)[0]
        names = re.findall(r"^\s*(\w+)\s*,", block, re.MULTILINE)
        self.assertEqual(names, [r.name for r in Relic])
        self.assertEqual([r.value for r in Relic], list(range(len(names))))

    def evaluate(self, relic, definition="guard", attack=2):
        value = state(ball(attack=attack), [enemy(0, 0, 20, hp=100)])
        value["offered_balls"][0]["definition_id"] = "player_" + definition
        value["relics"] = [{"index": relic, "owned": True}]
        value["table"]["walls"] = [{"start": {"x": 30, "z": -30}, "end": {"x": 30, "z": 60}}]
        return evaluate_build_shots(value, {"coupled_chain_prediction": False, "allow_bank_shots": True}, shot_type="bank", fixed_power=7)[0]

    def test_emergency_repair_is_not_bank_damage(self):
        self.assertEqual(self.evaluate(Relic.EmergencyRepairKit)["expected_damage_by_target"]["enemy:0"], 2)
        self.assertEqual(self.evaluate(Relic.BankShot)["expected_damage_by_target"]["enemy:0"], 4)

    def test_heavy_core_does_not_grant_standard_scope(self):
        value = state(ball(attack=5), [enemy(0, 0, 15, hp=6)])
        value["relics"] = [{"index": Relic.HeavyBallCore, "owned": True}]
        profile = {"coupled_chain_prediction": False}
        plain = evaluate_build_shots(value, profile, shot_type="direct", fixed_power=3)[0]
        self.assertEqual(plain["predicted_kill_count"], 0)
        self.assertEqual(plain["expected_damage_by_target"]["enemy:0"], 5)
        value["relics"][0]["index"] = Relic.StandardBallScope
        scoped = evaluate_build_shots(value, profile, shot_type="direct", fixed_power=3)[0]
        self.assertEqual(scoped["predicted_kill_count"], 1)


class CoupledMotionTests(unittest.TestCase):
    def setup_chain(self):
        foes = [{**enemy(i, i*8, 0, hp=20, restitution=1), "radius": 1} for i in range(3)]
        return state(ball(radius=1), foes)

    def test_legacy_impact_response_matches_shared_rule_numbers(self):
        # BallMechanics::ResolveNormalImpact(3,0,2,1,.8,1,1).
        first, second = normal_impact(3, 0, 2, 1, .8)
        self.assertAlmostEqual(first, 1.4)
        self.assertAlmostEqual(second, 3.2)
        self.assertAlmostEqual(first*2 + second, 6)

    def test_chain_reaches_third_ball_in_chronological_order(self):
        value = self.setup_chain()
        before = copy.deepcopy(value)
        result = project(value, value["offered_balls"][0]["status"], (-50, -50), (0, 0), "enemy:0", (2, 0))
        pairs = [set((c["first"], c["second"])) for c in result["contacts"]]
        self.assertEqual(pairs[:2], [{"enemy:0", "enemy:1"}, {"enemy:1", "enemy:2"}])
        self.assertLess(result["contacts"][0]["time"], result["contacts"][1]["time"])
        self.assertEqual(value, before)

    def test_moving_away_target_is_not_hit_at_old_static_position(self):
        value = self.setup_chain()
        value["enemies"] = value["enemies"][:2]
        value["enemies"][1]["velocity"] = {"x": 2, "z": 0}
        result = project(value, value["offered_balls"][0]["status"], (-50, -50), (0, 0), "enemy:0", (1, 0))
        self.assertTrue(result["complete"])
        self.assertEqual(result["contacts"], [])

    def test_retained_body_cannot_consume_a_piercing_pass(self):
        value = state(ball("pierce", radius=1), [{**enemy(0, -20, 0), "radius": 1}, {**enemy(1, 5, 0, hp=0, defeated=True), "radius": 1}])
        result = project(value, value["offered_balls"][0]["status"], (0, 0), (3, 0), "enemy:0", (0, 0), pierce_remaining=2)
        self.assertTrue(result["contacts"])
        self.assertEqual(result["contacts"][0]["second"], "enemy:1")
        self.assertFalse(result["contacts"][0]["pierced"])

    def test_contact_limit_reports_incomplete_prediction(self):
        value = self.setup_chain()
        result = project(value, value["offered_balls"][0]["status"], (-50, -50), (0, 0), "enemy:0", (2, 0), max_contacts=1)
        self.assertEqual(len(result["contacts"]), 1)
        self.assertFalse(result["complete"])

    def test_travel_counts_rebound_distance_not_only_final_displacement(self):
        value = state(ball(radius=1), [enemy(0, 0, 0, hp=20)])
        value["enemies"][0]["radius"] = 1
        value["table"]["walls"] = [{"start": {"x": 10, "z": -20}, "end": {"x": 10, "z": 20}}]
        result = project(value, value["offered_balls"][0]["status"], (-50, -50), (0, 0), "enemy:0", (2, 0))
        displacement = __import__("math").hypot(*result["positions"]["enemy:0"])
        self.assertGreaterEqual(result["wall_hits_by_body"]["enemy:0"], 1)
        self.assertGreater(result["distances"]["enemy:0"], displacement + 5)

    def test_precontact_player_pocket_cannot_kill_enemy_behind_it(self):
        value = state(ball(attack=9), [enemy(0, 0, 20, hp=2)])
        value["table"]["pockets"] = [{"position": {"x": 0, "z": 8}, "radius": 3}]
        choices = evaluate_build_shots(value, {}, shot_type="direct", shot_goal="damage", fixed_power=7)
        self.assertTrue(choices)
        self.assertTrue(all(c["predicted_kill_count"] == 0 for c in choices))
        self.assertTrue(all(c["self_pocket_risk"] == 1 for c in choices))
        self.assertTrue(all(not any(c["expected_damage_by_target"].values()) for c in choices))

    def test_stress_score_keeps_reviewable_additive_contributions(self):
        value = state(ball(attack=3), [enemy(0, 0, 18, hp=3), enemy(1, 8, 27, hp=4)])
        choices = evaluate_build_shots(value, {"human_error": {"aim_radius_ratio": .02, "power_ratio": .05}}, fixed_power=3)
        self.assertTrue(choices)
        for c in choices:
            self.assertAlmostEqual(sum(c["score_breakdown"].values()), c["score"], places=3)
            self.assertEqual(len(c["robustness"]["samples"]), 9)

    def test_numerically_complete_chain_is_not_a_guaranteed_attack_stop(self):
        value = state(ball(attack=1, impactAttack=10), [enemy(0, 0, 15, hp=100), enemy(1, 0, 26, hp=2, attack=3)])
        choices = evaluate_build_shots(value, {}, target_id="enemy:0", shot_type="direct", fixed_power=7)
        chained = next(c for c in choices if "enemy:1" in c["speculative_defeated_target_ids"])
        self.assertTrue(chained["coupled_chain"]["complete"])
        self.assertNotIn("enemy:1", chained["expected_defeated_target_ids"])
        self.assertNotIn("enemy:1", chained["expected_attack_prevention_by_target"])
        self.assertGreaterEqual(chained["conservative_incoming_damage"], 3)
        self.assertGreater(chained["expected_incoming_damage"], 0)
        self.assertLess(chained["expected_incoming_damage"], chained["conservative_incoming_damage"])


if __name__ == "__main__":
    unittest.main()
