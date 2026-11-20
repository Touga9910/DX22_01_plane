import copy
import unittest
from boss_strong_audit import audit_strong_stage


class StrongAuditTests(unittest.TestCase):
    def fixture(self):
        stage = dict(shots=[dict(elapsed_from_run_start_ms=10,duration_ms=10,player_hp_after=30)],damage_events=[])
        plan = dict(event_type='enemy_action_plan',details=dict(actions=[dict(
            enemy_id='enemy_boss_core',action='normal_attack',predicted_damage_before_defense=4)]))
        decision = dict(event_type='boss_ai_decision',details=dict(boss_evaluation=dict(
            metrics=dict(recovery_strong_attack_announced=False))))
        executed = dict(event_type='boss_attack_executed',details=dict(intent='strong_attack',damage_before_shield=4))
        return stage,[plan,decision,executed]

    def test_fully_shielded_execution_is_counted(self):
        stage,events=self.fixture()
        result=audit_strong_stage(stage,events,'B0012')
        self.assertEqual(result['strong_executions'],1)
        with self.assertRaisesRegex(ValueError,'lacks actual hit'):
            audit_strong_stage(stage,events[:2],'B0011')
        stage['damage_events']=[dict(source='boss_strong_attack',elapsed_from_run_start_ms=21,damage=1)]
        self.assertEqual(audit_strong_stage(stage,events[:2],'B0011')['strong_executions'],1)

    def test_break_and_core_kill_are_not_executions(self):
        stage,events=self.fixture()
        for cancellation in [dict(event_type='boss_attack_interrupted',details=dict(intent='strong_attack')),
                             dict(event_type='enemy_defeated',details=dict(enemy_id='enemy_boss_core'))]:
            result=audit_strong_stage(stage,events[:2]+[cancellation],'B0012')
            self.assertEqual(result['strong_executions'],0)
        with self.assertRaisesRegex(ValueError,'execution telemetry'):
            audit_strong_stage(stage,events+[cancellation],'B0012')

    def test_recovery_prediction_must_match_actual_transition(self):
        stage,events=self.fixture()
        events=copy.deepcopy(events[:2]);events.append(dict(event_type='boss_break_ended'))
        with self.assertRaisesRegex(ValueError,'recovery announcement'):
            audit_strong_stage(stage,events,'B0012')


if __name__=='__main__':unittest.main()
