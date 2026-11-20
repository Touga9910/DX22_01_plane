import copy
import unittest
from boss_finish_audit import audit_stage


class FinishAuditTests(unittest.TestCase):
    def fixture(self):
        stage=dict(stage_result=dict(result='clear',total_shots=2),damage_events=[],shots=[
            dict(ball_id='player_guard',elapsed_from_run_start_ms=10,duration_ms=5,enemies_alive_after=3),
            dict(ball_id='player_standard',elapsed_from_run_start_ms=30,duration_ms=10,enemies_alive_after=0)])
        events=[dict(event_type='boss_ai_decision',elapsed_from_run_start_ms=29,details=dict(boss_evaluation=dict(metrics=dict(encounter_cleared=True,expected_enemy_turn_damage=0,minions_retired_after_shot=3)))),
                dict(event_type='enemy_defeated',elapsed_from_run_start_ms=35,details=dict(enemy_id='enemy_boss_core'))]
        return stage,events

    def test_actual_last_shot_proves_completion(self):
        s,e=self.fixture();a=audit_stage(s,e,strict=True)
        self.assertEqual((a['extra_shots'],a['residual_battles'],a['core_shots'],a['core_guard_shots']),(0,0,2,1))

    def test_extra_shot_residual_damage_and_ai_disagreement_rejected(self):
        for kind in ('extra','residual','damage','ai'):
            s,e=self.fixture()
            if kind=='extra':s['shots'].append(dict(ball_id='player_guard',elapsed_from_run_start_ms=50,duration_ms=5,enemies_alive_after=0))
            if kind=='residual':s['shots'][-1]['enemies_alive_after']=1
            if kind=='damage':s['damage_events']=[dict(damage=1,elapsed_from_run_start_ms=45)]
            if kind=='ai':e[0]['details']['boss_evaluation']['metrics']['encounter_cleared']=False
            with self.subTest(kind=kind),self.assertRaises(ValueError):audit_stage(s,e,strict=True)

    def test_baseline_cleanup_counted_without_changing_original(self):
        s,e=self.fixture();s['shots'][-1]['enemies_alive_after']=1
        s['shots'].append(dict(ball_id='player_guard',elapsed_from_run_start_ms=50,duration_ms=5,enemies_alive_after=0))
        s['damage_events']=[dict(damage=1,elapsed_from_run_start_ms=45)]
        original=copy.deepcopy(s);a=audit_stage(s,e)
        self.assertEqual((a['extra_shots'],a['residual_battles'],a['post_kill_shot_damage']),(1,1,1))
        self.assertEqual(s,original)


if __name__=='__main__':unittest.main()
