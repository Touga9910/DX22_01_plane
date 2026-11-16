"""Regression checks for the new shot stats in bounded MCP estimates."""
import copy
import unittest
from pathlib import Path
from test_build_shot_evaluator import ball, enemy, state
from build_profiles import load_build_profiles, compose_control_profile, profile_settings_hash
from shot_planner import load_player_profiles
from build_shot_evaluator import evaluate_build_shots, collision_damage


class GuardImpactEstimates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).parent
        builds, _ = load_build_profiles(root / "build_profiles.json")
        players = load_player_profiles(root / "player_profiles.json")
        build = dict(builds["standard"], id="standard",
                     settings_hash=profile_settings_hash(builds["standard"]))
        cls.profile = compose_control_profile(players["intermediate"], build)

    def shot(self, impact=0, shield=0, collision_shield=0):
        value = state(ball(attack=1, impactAttack=impact, shotShield=shield,
                           enemyCollisionShield=collision_shield),
                      [enemy(0, 0, 15, hp=100, attack=99),
                       enemy(1, 0, 26, hp=100, attack=77)])
        return value

    def evaluate(self, value):
        return evaluate_build_shots(value, self.profile, target_id="enemy:0",
                                    shot_type="direct", fixed_power=8)[0]

    def test_impact_independence_and_both_targets(self):
        zero = self.evaluate(self.shot(impact=0))
        strong = self.evaluate(self.shot(impact=3))
        self.assertEqual(strong["metrics"]["chain_collisions"], 1)
        for target in ("enemy:0", "enemy:1"):
            self.assertAlmostEqual(strong["expected_damage_by_target"].get(target, 0)
                                   - zero["expected_damage_by_target"].get(target, 0), 3)
        value = self.shot(impact=3)
        for foe in value["enemies"]:
            foe["attack"] = 1
        self.assertEqual(self.evaluate(value)["expected_damage_by_target"],
                         strong["expected_damage_by_target"])
        self.assertEqual(collision_damage(0, enemy(0, 0, 0), (0, -5)), 0)

    def test_guard_shield_is_from_this_shot_and_enemy_pair_only(self):
        plain = self.evaluate(self.shot(impact=1))
        guard = self.evaluate(self.shot(impact=1, shield=5, collision_shield=1))
        self.assertEqual(plain["expected_incoming_damage"]
                         - guard["expected_incoming_damage"], 6)
        single = self.shot(impact=1, shield=5, collision_shield=1)
        single["enemies"] = single["enemies"][:1]
        without_pair = self.evaluate(single)
        no_effect = copy.deepcopy(single)
        no_effect["offered_balls"][0]["status"]["enemyCollisionShield"] = 0
        self.assertEqual(without_pair["expected_incoming_damage"],
                         self.evaluate(no_effect)["expected_incoming_damage"])
        no_effect["player"]["shield"] = 100
        self.assertEqual(self.evaluate(no_effect)["expected_incoming_damage"],
                         without_pair["expected_incoming_damage"])


if __name__ == "__main__":
    unittest.main()
