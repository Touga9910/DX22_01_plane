import unittest
from build_shot_evaluator import (_marginal_heavy_consumer_value, score_candidate,ball_stats,_coupled_rescore,hp_exposure)
from boss_shot_policy import rescore_boss_choice,evaluate_boss_choices
from test_build_shot_evaluator import ball,enemy,state

class GuardSetupRevision(unittest.TestCase):
    def test_guard_fall_spends_shield_before_enemy_turn(self):
        b=ball('guard',attack=1,shotShield=3)
        s=state(b,[enemy(0,0,25,hp=100,attack=3)])
        s['table']['pockets']=[{'position':{'x':0,'z':8},'radius':1}]
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,25),'kind':'center'},7,{},coupled=True)
        self.assertTrue(c['prediction_input']['from_launch'])
        self.assertEqual(c['self_pocket_risk'],1)
        self.assertEqual(c['expected_self_pocket_hp_loss'],0)
        self.assertEqual(c['expected_shield_after_pocket'],1)
        self.assertEqual(c['expected_incoming_damage'],2)
        self.assertEqual(c['expected_damage_by_target'],{})
        self.assertEqual(hp_exposure(3,3,2,0),(0,0,3))

    def test_guard_uses_interior_stress_and_records_owned_consumers(self):
        b=ball('guard',shotShield=3)
        s=state(b,[enemy(0,0,18,hp=20)])
        s['deck_balls']=[ball('chain_impact')]
        c=_coupled_rescore(s,b,ball_stats(s,b),{'target_id':'enemy:0','power':4.,'_evaluation_path':{'mode':'direct','aim':(0,18),'kind':'center'}},{'human_error':{'aim_radius_ratio':.02,'power_ratio':.05}})
        self.assertEqual(len(c['robustness']['samples']),25)
        self.assertEqual(c['prediction_input']['state']['deck_balls'][0]['status'],s['deck_balls'][0]['status'])

    def test_marginal_consumer_values_only_new_useful_damage(self):
        s=state(ball('heavy'),[])
        hp={'a':30,'b':20,'c':20}
        self.assertEqual(_marginal_heavy_consumer_value(s,0,3,hp,{},.5),0)
        s['deck_balls']=[ball('chain_impact',attack=1,heavyCollisionConsumeAmount=5,heavyFinisherDamagePerCollision=1)]
        self.assertEqual(_marginal_heavy_consumer_value(s,0,3,hp,{},.5),0)
        self.assertEqual(_marginal_heavy_consumer_value(s,5,3,hp,{},.5),0)
        self.assertEqual(_marginal_heavy_consumer_value(s,0,3,{'a':30},{},.5),0)
        self.assertEqual(_marginal_heavy_consumer_value(s,0,3,{'a':30,'b':1,'c':1},{},.5),0)
        s['deck_balls'][0]['status']['heavyFinisherDamagePerCollision']=2
        self.assertEqual(_marginal_heavy_consumer_value(s,0,3,hp,{},.5),0)

    def test_anchor_stop_alone_has_no_reward(self):
        b=ball('anchor')
        s=state(b,[enemy(0,0,18,hp=20)])
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,18),'kind':'center'},4,{},coupled=True)
        self.assertEqual(c['score_breakdown']['stable_stop'],0)

class BossCostRevision(unittest.TestCase):
    def choice(self,damage=3,incoming=0,opportunity=0,improvement=0,kill=False):
        return {'candidate_id':'a','state_key':'k','velocity':{'x':1,'z':2},'score':0,
            'score_breakdown':{'direct_damage':damage,'safety':-incoming,'setup':opportunity+improvement*5,'anchor_position':opportunity*.5,'kill':100 if kill else 0,'fatal':0},
            'metrics':{'direct_damage':damage,'expected_enemy_turn_damage':incoming,'opportunity_improvement':improvement,'encounter_cleared':kill}}
    def test_enemy_hp_cost_balances_progress(self):
        s={'player':{'current_hp':50}}
        risky=rescore_boss_choice(self.choice(damage=6,incoming=2),s)
        safe=rescore_boss_choice(self.choice(damage=3),s)
        self.assertGreater(safe['score'],risky['score'])
        self.assertEqual(risky['score_breakdown']['safety'],-4)
        self.assertEqual(risky['velocity'],{'x':1,'z':2})
    def test_absolute_position_does_not_repeat_setup_reward(self):
        a=rescore_boss_choice(self.choice(damage=0,opportunity=10),{})
        self.assertEqual(a['score_breakdown']['setup'],0)
        self.assertEqual(a['score_breakdown']['anchor_position'],0)
        self.assertLess(a['score'],0)
        b=rescore_boss_choice(self.choice(damage=0,improvement=2),{})
        self.assertEqual(b['score_breakdown']['setup'],2)
        self.assertNotIn('no_progress',b['score_breakdown'])
    def test_true_finish_and_fatal_are_preserved(self):
        a=rescore_boss_choice(self.choice(damage=2,incoming=1,kill=True),{})
        self.assertEqual(a['score_breakdown']['kill'],100)
        a['score_breakdown']['fatal']=-1000
        self.assertEqual(rescore_boss_choice(a,{})['score_breakdown']['fatal'],-1000)
    def test_public_ranking_preserves_native_identity_and_state_key(self):
        evaluation={'state_key':'k','choices':[dict(self.choice(6,2),offer_index=0,target_id='boss'),dict(self.choice(3),candidate_id='b',offer_index=1,target_id='boss')]}
        class Store:
            def submit_command(self,*args):return {'ok':True,'evaluation':evaluation}
        r=evaluate_boss_choices(Store(),{'boss_state':{'target_id':'boss:0'},'available_actions':['evaluate_boss_shots']})
        self.assertEqual(r['recommended']['candidate_id'],'b')
        self.assertEqual(r['state_key'],'k')
        self.assertEqual(r['recommended']['target_id'],'boss:0')
        self.assertEqual(evaluation['choices'][0]['score'],0)

class SavedNativeRegression(unittest.TestCase):
    def test_saved_idle_cue_recollision_fall_spends_shield(self):
        import json,math
        from pathlib import Path
        case=json.loads(Path(__file__).with_name('guard_launch_regression_input.json').read_text(encoding='utf-8'))
        i=case['input'];v=case['actual_launch']['velocity'];s=i['state']
        target=next(e for e in s['enemies'] if e['target_id']==i['target_id'])
        c=score_candidate(s,i['ball'],target,i['stats'],i['path'],math.hypot(v[0],v[2]),i['profile'],coupled=True,launch_velocity=(v[0],v[2]))
        self.assertIn('player',c['coupled_chain']['pocketed'])
        self.assertEqual(c['expected_shield_after_pocket'],1)
        self.assertEqual(c['expected_incoming_damage'],case['expected_enemy_hp'])
        self.assertEqual(c['expected_self_pocket_hp_loss'],0)
        grid=_coupled_rescore(s,i['ball'],i['stats'],{'target_id':i['target_id'],'power':math.hypot(*i['cue_velocity']),'_evaluation_path':i['path']},i['profile'])
        self.assertGreater(grid['self_pocket_risk'],0)
        self.assertEqual(grid['robustness']['worst_net_hp_loss'],2)
        self.assertGreater(grid['expected_incoming_damage'],0)

    def test_saved_boss_tradeoffs_keep_progress_without_extra_pocket_hp(self):
        import json
        from pathlib import Path
        cases=json.loads(Path(__file__).with_name('boss_progress_regression_inputs.json').read_text(encoding='utf-8'))
        for case in cases:
            with self.subTest(build=case['build']):
                ranked=sorted((rescore_boss_choice(c,{'player':{'current_hp':40}}) for c in case['choices']),key=lambda c:c['score'],reverse=True)
                self.assertEqual(ranked[0]['candidate_id'],case['preferred_candidate_id'])

if __name__=='__main__':unittest.main()
