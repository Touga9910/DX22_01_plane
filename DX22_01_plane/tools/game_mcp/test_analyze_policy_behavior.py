import unittest

from analyze_policy_behavior import audit_raw


class PolicyBehaviorAuditTests(unittest.TestCase):
    def raw(self):
        return {"stages": [{"stage_id": "phase", "stage_type": "normal",
            "stage_context": {"combat_mode": "phase_prototype"},
            "elapsed_from_run_start_ms": 0, "stage_start": {"player_hp": 10},
            "stage_result": {"result": "clear", "duration_ms": 500, "player_damage_taken": 2},
            "damage_events": [{"elapsed_from_run_start_ms": 200, "source": "enemy_attack", "damage": 2}],
            "shots": [{"elapsed_from_run_start_ms": 100, "enemies_defeated": 0,
                "shot_context": {"mcp_telemetry": {"build_evaluation": {"expected_incoming_damage": 0}}},
                "decision_log": {"predicted_kills": 1, "candidate_comparison": {
                    "actual_selected_instance_id": 1, "actual_matches_recommendation": False,
                    "candidates": [{"instance_id": 1, "score": -100}, {"instance_id": 2, "score": 10}]}}}]}]}

    def test_includes_enemy_action_after_shot_resolution(self):
        full = audit_raw(self.raw())
        result = full["audit"]
        self.assertEqual(result["phase_enemy_hp_damage"], 2)
        self.assertEqual(result["phase_zero_prediction_with_actual_attack"], 1)
        self.assertEqual(result["zero_prediction_with_kill_overestimate"], 1)
        self.assertEqual(result["higher_score_ball_ignored"], 1)
        self.assertEqual(result["phase_prediction_absolute_error"], 2)
        self.assertEqual(result["phase_underestimated_hp_damage"], 2)
        self.assertEqual(full["prediction_error_examples"][0]["actual_enemy_hp_damage"], 2)

    def test_partial_last_turn_is_not_counted_as_complete(self):
        raw = self.raw()
        raw["stages"][0]["stage_result"] = {}
        shot = raw['stages'][0]['shots'][0]
        shot['physics_chain'] = {'enabled':True,'effective_enemy_enemy_collision_count':0}
        shot['shot_context']['mcp_telemetry']['build_evaluation']['coupled_chain'] = {'complete':True,'contacts':[]}
        result = audit_raw(raw)["audit"]
        self.assertEqual(result["phase_shots"], 1)
        self.assertNotIn("settled_phase_turns", result)
        self.assertNotIn('coupled_native_observed_shots',result)

    def test_native_physics_count_is_separate_from_player_contacts(self):
        raw = self.raw()
        shot = raw['stages'][0]['shots'][0]
        shot['physics_chain'] = {'enabled':True,'effective_enemy_enemy_collision_count':1}
        shot['shot_context']['mcp_telemetry']['build_evaluation']['coupled_chain'] = {
            'complete':True,'contacts':[{'player_contact':True},{'player_contact':False},{'player_contact':False}]}
        a = audit_raw(raw)['audit']
        self.assertEqual(a['coupled_native_observed_shots'],1)
        self.assertEqual(a['coupled_enemy_pair_absolute_error'],1)
        self.assertEqual(a['coupled_enemy_pair_predicted_contacts'],2)
        self.assertEqual(a['coupled_enemy_pair_overestimated_contacts'],1)

    def test_expected_and_conservative_forecasts_are_audited_separately(self):
        raw = self.raw()
        shot = raw['stages'][0]['shots'][0]
        shot['shot_context']['mcp_telemetry']['build_evaluation']['conservative_incoming_damage'] = 2
        result = audit_raw(raw)['audit']
        self.assertEqual(result['phase_prediction_absolute_error'], 2)
        self.assertEqual(result['phase_conservative_absolute_error'], 0)
        self.assertEqual(result['phase_conservative_underestimated_hp_damage'], 0)

    def test_damage_integrity_failure_is_not_silently_accepted(self):
        raw = self.raw()
        raw["stages"][0]["stage_result"]["player_damage_taken"] = 3
        with self.assertRaisesRegex(ValueError, "disagree"):
            audit_raw(raw)

    def test_legacy_self_pocket_is_counted_even_without_enemy_attack(self):
        raw = self.raw()
        stage = raw["stages"][0]
        stage["stage_context"]["combat_mode"] = "legacy"
        stage["damage_events"][0]["source"] = "pocket"
        result = audit_raw(raw)
        self.assertEqual(result["audit"]["geometric_total_zero_prediction_with_damage"], 1)
        self.assertEqual(result["audit"]["geometric_pocket_zero_prediction_with_damage"], 1)
        self.assertEqual(result["hp_prediction_error_examples"][0]["actual_gross"], 2)
        self.assertNotIn("phase_enemy_hp_damage", result["audit"])


if __name__ == "__main__":
    unittest.main()
