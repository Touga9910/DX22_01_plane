import json
import math
from pathlib import Path
import unittest
from launch_physics import project_launch
from build_shot_evaluator import score_candidate,ball_stats,bound_offense_rewards,_coupled_rescore,_special_score
from test_build_shot_evaluator import ball,enemy,state

class LaunchPredictionRevision(unittest.TestCase):
    def test_straight_stationary_pierce_followups_remain_confirmed(self):
        b=ball('pierce',attack=3,pierceMaxUses=2)
        s=state(b,[enemy(0,0,15,hp=1),enemy(1,0,25,hp=1),enemy(2,0,35,hp=1)])
        c=_coupled_rescore(s,b,ball_stats(s,b),{'target_id':'enemy:0','power':5.,
            '_evaluation_path':{'mode':'direct','aim':(0,15),'kind':'center'}},
            {'human_error':{'aim_radius_ratio':.02,'power_ratio':.05}})
        self.assertEqual(set(c['expected_defeated_target_ids']),{'enemy:0','enemy:1','enemy:2'})
        self.assertEqual(c['conservative_incoming_damage'],0)

    def test_first_contact_chain_impact_area_removals_are_not_motion_speculation(self):
        b=ball('chain_impact',attack=2,chainImpactRadius=8,heavyCollisionConsumeAmount=5,heavyFinisherDamagePerCollision=1)
        s=state(b,[enemy(0,0,15,hp=5),enemy(1,7,15,hp=3),enemy(2,-7,15,hp=3)])
        s['ball_synergies']={'heavy_stacks':3}
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,15),'kind':'center'},5,{},coupled=True)
        self.assertEqual(set(c['expected_defeated_target_ids']),{'enemy:1','enemy:2'})

    def test_moving_pierce_followup_does_not_become_a_confirmed_removal(self):
        b=ball('pierce',attack=3,pierceMaxUses=2)
        s=state(b,[enemy(0,0,15,hp=1),enemy(1,0,25,hp=1,velocity={'x':.1,'z':0})])
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,15),'kind':'center'},5,{},coupled=True)
        self.assertNotIn('enemy:1',c['expected_defeated_target_ids'])

    def test_moving_area_board_retains_uncertain_removals(self):
        b=ball('chain_impact',attack=2,chainImpactRadius=8,heavyCollisionConsumeAmount=5,heavyFinisherDamagePerCollision=1)
        s=state(b,[enemy(0,0,15,hp=5),enemy(1,7,15,hp=3,velocity={'x':.1,'z':0})])
        s['ball_synergies']={'heavy_stacks':3}
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,15),'kind':'center'},5,{},coupled=True)
        self.assertNotIn('enemy:1',c['expected_defeated_target_ids'])

    def test_wall_reflection_has_no_artificial_nudge(self):
        s=state(ball('heavy',radius=.5,friction=0),[])
        s['table']['walls']=[{'start':{'x':2,'z':-10},'end':{'x':2,'z':10}}]
        r=project_launch(s,s['offered_balls'][0]['status'],(0,0),(2,0),max_iterations=1)
        self.assertAlmostEqual(r['positions']['player'][0],1.5,delta=1e-6)
        self.assertLess(r['velocities']['player'][0],0)

    def test_repeated_touching_does_not_grant_repeated_damage(self):
        s=state(ball('heavy',radius=.5,mass=1,restitution=.1,friction=.4),
            [{**enemy(0,1,0,hp=100),'radius':.5,'restitution':.1}])
        events=[]
        r=project_launch(s,s['offered_balls'][0]['status'],(0,0),(1,0),on_contact=lambda e:events.append(e))
        self.assertTrue(r['complete'])
        self.assertEqual(len(events),1)

    def test_special_ball_cannot_invent_a_hit_beyond_a_pocket(self):
        b=ball('minion_finisher',pierce=True,pierceMaxUses=2)
        s=state(b,[enemy(0,0,15,hp=1)])
        s['table']['pockets']=[{'position':{'x':0,'z':6},'radius':1}]
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,15),'kind':'center'},7,{},coupled=True)
        self.assertEqual(c['expected_damage_by_target'],{})
        self.assertEqual(c['expected_defeated_target_ids'],[])
        self.assertTrue(c['prediction_input']['from_launch'])

    def test_generated_minions_do_not_guarantee_attacker_removal(self):
        b=ball('minion_finisher',attack=3,pierce=True,pierceMaxUses=2)
        s=state(b,[enemy(0,0,15,hp=1)])
        s['ball_synergies']={'pierce_stacks':3,'pierce_traces':[{'start':{'x':0,'z':0},'direction':{'x':0,'z':1}}]}
        c=score_candidate(s,b,s['enemies'][0],ball_stats(s,b),{'mode':'direct','aim':(0,15),'kind':'center'},7,{},coupled=True)
        self.assertTrue(c['spawned_actor_approximation'])
        self.assertEqual(c['expected_defeated_target_ids'],[])
        self.assertGreater(c['expected_incoming_damage'],0)
        self.assertEqual(c['conservative_incoming_damage'],1)

    def test_distinct_kills_keep_value_without_unbounded_contact_reward(self):
        a={'damage':10.,'kills':12.,'chain_collisions':1000.}
        b={'damage':10.,'kills':4.,'chain_collisions':1000.}
        bound_offense_rewards(a,3);bound_offense_rewards(b,1)
        self.assertGreater(sum(a.values()),sum(b.values()))
        self.assertLessEqual(sum(a.values()),20.)

    def test_heavy_resources_count_living_distance_per_enemy(self):
        b=ball('heavy',heavyDistanceStacks=True,eventStackGenerationBonus=1)
        s=state(b,[enemy(0,0,15)])
        s['deck_balls']=[ball('chain_impact')]
        _,facts,_=_special_score(s,b,b['status'],s['enemies'],s['enemies'][0],{'mode':'direct'},
            {'enemy:0':8},{'enemy:0':0},{'enemy:0'},0,100,0,0,False,0,
            {'travel':100,'wall_bounces':0,'living_enemy_distances':{'enemy:0':39,'enemy:1':81}})
        self.assertEqual(facts['heavy_stacks_generated_estimate'],3) # floor(39/40)+floor(81/40)+1

    def test_saved_zero_risk_falls_are_detected_by_interior_sampling(self):
        cases=json.loads(Path(__file__).with_name('launch_prediction_regression_inputs.json').read_text(encoding='utf-8'))
        for case in cases:
            i=case['input']
            with self.subTest(key=case['key']):
                c=_coupled_rescore(i['state'],i['ball'],i['stats'],{'target_id':i['target_id'],
                    'power':math.hypot(*i['cue_velocity']),'_evaluation_path':i['path']},i['profile'])
                self.assertGreater(c['self_pocket_risk'],0)
                self.assertEqual(len(c['robustness']['samples']),25)

if __name__=='__main__':unittest.main()
