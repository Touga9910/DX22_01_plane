import unittest
from unittest.mock import patch
from pathlib import Path
import tempfile

from coupled_chain import project
from build_shot_evaluator import bound_offense_rewards, score_candidate, ball_stats
from test_build_shot_evaluator import ball, enemy, state
import collect_until_deadline


class HeavySafetyRevision(unittest.TestCase):
    @staticmethod
    def contact_events(events):
        def simulate(state,stats,start,velocity,target,target_velocity,**kwargs):
            for event in events:kwargs['on_contact'](event)
            return {'contacts':events,'pocketed':[],'complete':True,'iterations':1,
                    'positions':{'player':start},'distances':{},'minimum_player_pocket_clearance':100}
        return simulate

    @staticmethod
    def event(a,b,wall=0,living=False):
        return dict(first=a,second=b,first_position=[0,10],second_position=[0,15],
                    player_wall_hits=wall,living_enemy_pair=living,time=1)

    def test_heavy_knockback_multiplier_does_not_apply_to_a_corpse(self):
        value=state(ball('heavy',mass=2,radius=.5,friction=.4,restitution=.8),
                    [{**enemy(0,1.2,0,hp=0,defeated=True),'radius':.5}])
        result=project(value,value['offered_balls'][0]['status'],(0.,0.),(2.,0.),'enemy:0',(0.,0.),
                       from_launch=True,max_contacts=1)
        self.assertAlmostEqual(result['velocities']['enemy:0'][0],4/3*1.6*.8,delta=1e-6)
        self.assertTrue(result['contacts'][0]['second_dead'])

    def test_corpse_has_zero_effective_attack_and_does_not_advance_collision_relic(self):
        value=state(ball('heavy',attack=2,impactAttack=0),[enemy(0,0,15,hp=100),
                    enemy(1,10,15,hp=0,defeated=True,attack=4)])
        value['relics']=[{'index':1,'owned':True}]
        events=[self.event('enemy:1','enemy:0'),self.event('player','enemy:0')]
        with patch('build_shot_evaluator.project_coupled_chain',self.contact_events(events)):
            c=score_candidate(value,value['offered_balls'][0],value['enemies'][0],
                ball_stats(value,value['offered_balls'][0]),{'mode':'direct','aim':(0.,15.),'kind':'center'},7,{},coupled=True)
        self.assertEqual(c['expected_damage_by_target']['enemy:0'],2) # corpse 0 + cue 2, no false +1
        self.assertEqual(c['metrics']['chain_collisions'],0)

    def test_bank_bonus_can_become_ready_after_an_earlier_direct_hit(self):
        value=state(ball('heavy',attack=2),[enemy(0,0,15,hp=100),enemy(1,10,20,hp=100)])
        value['relics']=[{'index':2,'owned':True}]
        events=[self.event('player','enemy:0'),self.event('player','enemy:1',wall=1)]
        with patch('build_shot_evaluator.project_coupled_chain',self.contact_events(events)):
            c=score_candidate(value,value['offered_balls'][0],value['enemies'][0],
                ball_stats(value,value['offered_balls'][0]),{'mode':'direct','aim':(0.,15.),'kind':'center'},7,{},coupled=True)
        self.assertEqual(c['expected_damage_by_target'],{'enemy:0':2,'enemy:1':4})

    def test_saved_inputs_replay_with_defeat_callback(self):
        value=state(ball('heavy',attack=3),[enemy(0,0,15,hp=3),enemy(1,0,25,hp=10)])
        offer=value['offered_balls'][0];path={'mode':'direct','aim':(0.,15.),'kind':'center'}
        c=score_candidate(value,offer,value['enemies'][0],ball_stats(value,offer),path,7,{},coupled=True)
        i=c['prediction_input'];target=next(e for e in i['state']['enemies'] if e['target_id']==i['target_id'])
        replay=score_candidate(i['state'],i['ball'],target,i['stats'],i['path'],7,i['profile'],
                               coupled=True,launch_velocity=i['cue_velocity'])
        self.assertEqual(c['coupled_chain'],replay['coupled_chain'])
        self.assertEqual(c['expected_damage_by_target'],replay['expected_damage_by_target'])
        self.assertTrue(any(e['first_dead'] or e['second_dead'] for e in replay['coupled_chain']['contacts']))
        for event in replay['coupled_chain']['contacts']:
            if event['first_dead']:self.assertEqual(event['first_attack'],0)
            if event['second_dead']:self.assertEqual(event['second_attack'],0)

    def test_braking_across_stop_threshold_keeps_last_completed_tick(self):
        # Automated shots brake on tick zero: .2 + 11*.1 = 1.3 travel.
        # Skipping first friction falsely drifts into the pocket at x=1.55.
        value=state(ball('heavy',radius=.1,friction=.1),[])
        value['table']['pockets']=[{'position':{'x':1.75,'z':0},'radius':.1}]
        result=project(value,value['offered_balls'][0]['status'],(0.,0.),(.3,0.),'none',(0.,0.),from_launch=True)
        self.assertNotIn('player',result['pocketed'])
        self.assertAlmostEqual(result['positions']['player'][0],1.3,places=5)

    def test_first_contact_uses_one_full_automated_launch_friction_update(self):
        value=state(ball('heavy',mass=2,radius=.5,friction=.4,restitution=.8),
                    [{**enemy(0,1.2,0,hp=100),'radius':.5}])
        result=project(value,value['offered_balls'][0]['status'],(0.,0.),(2.,0.),'enemy:0',(0.,0.),
                       from_launch=True,max_contacts=1)
        self.assertAlmostEqual(result['elapsed'],.125)
        self.assertAlmostEqual(result['velocities']['player'][0],1.6-2/3*1.6*.8,delta=1e-6)
        self.assertAlmostEqual(result['velocities']['enemy:0'][0],4/3*1.6*.8*1.3,places=6)
        self.assertEqual(len(result['contacts']),1)

    def test_subthreshold_player_still_drifts_during_stop_confirmation(self):
        value=state(ball('heavy',radius=.1),[{**enemy(0,100,100),'radius':.1}])
        value['table']['pockets']=[{'position':{'x':.8,'z':0},'radius':.1}]
        result=project(value,value['offered_balls'][0]['status'],(0.,0.),(.1,0.),'enemy:0',(0.,0.),from_launch=True)
        self.assertIn('player',result['pocketed'])
        self.assertAlmostEqual(result['player_pocket_time'],5.999,places=2)

    def test_heavy_trajectory_starts_at_cue_and_credits_no_phantom_hit(self):
        value=state(ball('heavy',attack=2),[enemy(0,0,25,hp=100)])
        value['table']['pockets']=[{'position':{'x':0,'z':8},'radius':1}]
        offer=value['offered_balls'][0]
        c=score_candidate(value,offer,value['enemies'][0],ball_stats(value,offer),
                          {'mode':'direct','aim':(0.,25.),'kind':'center'},7,{},coupled=True)
        self.assertTrue(c['coupled_chain']['from_launch'])
        self.assertEqual(c['coupled_chain']['friction_mode'],'fixed_tick')
        self.assertEqual(c['prediction_input']['cue_position'],[0.,0.])
        self.assertEqual(c['expected_damage_by_target'],{})
        self.assertEqual(c['expected_defeated_target_ids'],[])
        self.assertEqual(c['self_pocket_risk'],1)

    def test_repeated_contact_rewards_are_bounded_by_real_damage(self):
        b={'damage':6.,'kills':4.,'prevented_attack':2.,'chain_collisions':100.,'safety':-12.}
        budget=bound_offense_rewards(b)
        self.assertEqual(budget['secondary_budget'],3)
        self.assertEqual(sum(b.values()),-3.)
        guard={'damage':2.,'kills':4.,'prevented_attack':2.,'safety':-4.}
        bound_offense_rewards(guard)
        self.assertGreater(sum(guard.values()),sum(b.values()))

    def test_safe_multi_enemy_damage_still_outscores_safe_single_enemy(self):
        a={'damage':12.,'kills':12.,'chain_collisions':20.,'safety':0.}
        b={'damage':3.,'kills':4.,'safety':0.}
        bound_offense_rewards(a);bound_offense_rewards(b)
        self.assertGreater(sum(a.values()),sum(b.values()))

    def test_atomic_status_save_retries_sharing_violation(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'status.json';path.write_text('old')
            original=Path.replace
            attempts=[]
            def replace(source,dest):
                attempts.append(1)
                if len(attempts)<3:
                    self.assertEqual(path.read_text(),'old')
                    raise PermissionError('Windows sharing violation')
                return original(source,dest)
            with patch.object(Path,'replace',replace),patch.object(collect_until_deadline.time,'sleep') as sleep:
                collect_until_deadline.save(path,{'state':'collecting'})
            self.assertEqual(sleep.call_count,2)
            self.assertIn('collecting',path.read_text())
