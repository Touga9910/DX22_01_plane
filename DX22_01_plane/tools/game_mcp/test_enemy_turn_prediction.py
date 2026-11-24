import copy
import unittest

from enemy_turn_prediction import forecast_enemy_turn, attack_damage
from build_shot_evaluator import evaluate_build_shots
from test_shot_recovery import battle, ball, enemy


class EnemyTurnPredictionTests(unittest.TestCase):
    def state(self):
        state = battle([ball()], [enemy(0, 15), enemy(1, 30)])
        state["battle_combat_mode"] = "phase_prototype"
        for value in state["enemies"]:
            value.update(hp=100, max_hp=100, can_attack_this_turn=False,
                         planned_action="wait", turns_until_attack=1, can_charge=False)
        return state

    def test_phase_wait_does_not_hide_next_attack_and_input_is_unchanged(self):
        state = self.state()
        original = copy.deepcopy(state)
        result = forecast_enemy_turn(state)
        self.assertEqual(sum(attack_damage(e, 0) for e in result["enemies"]), 2)
        self.assertEqual(state, original)
        choices = evaluate_build_shots(state, {"recommended_power": {"min": 3, "max": 7}})
        self.assertTrue(choices)
        self.assertTrue(all(c["expected_incoming_damage"] == 2 for c in choices))

    def test_timer_charge_start_and_charge_attack(self):
        state = self.state()
        state["enemy_action_rules"] = {"charge_damage_multiplier": 3}
        state["enemies"][0].update(turns_until_attack=2)
        state["enemies"][1].update(can_charge=True, charge_state="idle", attack=2)
        result = forecast_enemy_turn(state)
        self.assertFalse(any(e["can_attack_this_turn"] for e in result["enemies"]))
        state["enemies"][1]["charge_state"] = "charging"
        result = forecast_enemy_turn(state)
        self.assertEqual(attack_damage(result["enemies"][1], 1), 5)
        self.assertFalse(result["enemies"][0]["can_attack_this_turn"])

    def test_dead_pocketed_and_stunned_enemies_do_not_attack(self):
        for flag in ("defeated", "pocketed", "stunned"):
            state = self.state()
            state["enemies"][0][flag] = True
            self.assertFalse(forecast_enemy_turn(state)["enemies"][0]["can_attack_this_turn"])

    def test_legacy_preserves_committed_action(self):
        state = self.state()
        state["battle_combat_mode"] = "legacy"
        self.assertIs(forecast_enemy_turn(state), state)


if __name__ == "__main__":
    unittest.main()
