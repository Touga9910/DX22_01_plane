import unittest

from build_shot_evaluator import pocket_capture_distance, evaluate_build_shots
from recovery_risk import recovery_reserve, project_path_health
from test_build_shot_evaluator import ball, enemy, state
from test_route_value_decision import route_state
from build_profiles import load_build_profiles
from route_decision import evaluate_rest_site_action, evaluate_route_options
from pathlib import Path
from shot_planner import _plan_bank_shot, plan_build_shot


class TargetSurvivalRevisionTests(unittest.TestCase):
    def test_retained_defeated_body_blocks_guaranteed_kill_but_keeps_fallback(self):
        wreck = enemy(0, 0, 10, hp=0, defeated=True)
        target = enemy(1, 0, 25, hp=1, attack=2)
        value = state(ball(attack=5), [wreck, target])
        choices = evaluate_build_shots(value, {}, target_id="enemy:1", shot_type="direct", fixed_power=7)
        self.assertTrue(choices)
        self.assertTrue(all(c["retained_body_blocker"] == "enemy:0" for c in choices))
        self.assertTrue(all(c["predicted_kill_count"] == 0 for c in choices))
        self.assertTrue(all("enemy:1" not in c["expected_attack_prevention_by_target"] for c in choices))

    def test_bank_execution_reflects_at_ball_center_contact_line(self):
        wall = {"start": {"x": 15, "z": -20}, "end": {"x": 15, "z": 50}}
        bank = _plan_bank_shot((0, 0), (0, 20), wall, 0, radius=2.4)
        self.assertAlmostEqual(bank["contact_point"][0], 12.6)
        self.assertAlmostEqual(bank["contact_point"][1], 10)
        value = state(ball(), [enemy(0, 0, 20)])
        value["table"]["walls"] = [wall]
        result = plan_build_shot(value, "enemy:0", {"allow_bank_shots": True},
                                 power_mode="manual", power=7, shot_type="bank", wall_index=0)
        args = result["arguments"]
        z_at_wall = args["direction_z"] / args["direction_x"] * 12.6
        self.assertAlmostEqual(z_at_wall, 10, places=4)
        self.assertAlmostEqual(2 * z_at_wall, 20, places=4)

    def test_aligned_but_distant_pocket_miss_is_not_a_defeat(self):
        pocket = {"position": {"x": 0, "z": 40}, "radius": 3}
        self.assertIsNone(pocket_capture_distance((0, 0), (.3, .9539392014), pocket, 2.4))
        self.assertAlmostEqual(pocket_capture_distance((0, 0), (0, 1), pocket, 2.4), 34.6)

    def test_legacy_static_secondary_kill_does_not_cancel_that_attack(self):
        value = state(ball(attack=1, impactAttack=10), [enemy(0, 0, 15, hp=100), enemy(1, 0, 26, hp=2)])
        choices = evaluate_build_shots(value, {"coupled_chain_prediction": False, "recommended_power": {"min": 7, "max": 7}}, target_id="enemy:0", shot_type="direct", fixed_power=7)
        chosen = next(c for c in choices if "enemy:1" in c["speculative_defeated_target_ids"])
        self.assertNotIn("enemy:1", chosen["expected_defeated_target_ids"])
        self.assertNotIn("enemy:1", chosen["expected_attack_prevention_by_target"])

    def future_state(self):
        value = route_state(hp=39)
        value["player"]["progress"] = 8
        value["run_map"]["nodes"] = [
            {"node_id": 0, "area": 8, "active": True, "destination": "rest", "next_node_ids": [1]},
            {"node_id": 1, "area": 9, "destination": "battle", "next_node_ids": [2]},
            {"node_id": 2, "area": 10, "destination": "battle", "next_node_ids": [3]},
            {"node_id": 3, "area": 11, "destination": "battle", "next_node_ids": [4]},
            {"node_id": 4, "area": 12, "destination": "rest", "next_node_ids": []}]
        return value

    def test_rest_reserve_accounts_for_future_late_fights(self):
        value = self.future_state()
        reserve = recovery_reserve(value)
        self.assertEqual([f["group"] for f in reserve["future_fights"]], ["mid", "late", "late"])
        self.assertEqual(reserve["required_hp"], 45)
        profiles, _ = load_build_profiles(Path(__file__).with_name("build_profiles.json"))
        value.update(scene="rest_site", available_actions=["heal", "upgrade_ball"])
        action = evaluate_rest_site_action(value, profiles, "standard")
        self.assertEqual(action["recommended_action"], "heal")
        self.assertEqual(action["hp_after_action"], 50)

    def test_future_heal_cannot_erase_lethal_damage_before_arrival(self):
        value = self.future_state()
        value["player"]["current_hp"] = 8
        nodes = value["run_map"]["nodes"]
        late = {**nodes[2], "area": 13}
        unsafe = project_path_health(value, [late, nodes[4]])
        safer = project_path_health(value, [nodes[4], late], "heal")
        self.assertLess(unsafe["minimum_tail_hp"], 0)
        self.assertGreater(unsafe["risk_cost"], safer["risk_cost"])
        self.assertLess(safer["exit_hp"], 21)


if __name__ == "__main__":
    unittest.main()
