import json,math,unittest
from pathlib import Path
from build_shot_evaluator import score_candidate,_marginal_heavy_consumer_value
from boss_shot_policy import rescore_boss_choice,evaluate_boss_choices
from test_build_shot_evaluator import ball,state

class AutomatedLaunchRevision(unittest.TestCase):
 def test_saved_heavy_launch_matches_native_reflections_and_fall(self):
  import launch_physics
  from unittest.mock import patch
  x=json.loads(Path(__file__).with_name('heavy_automated_launch_regression.json').read_text(encoding='utf-8'))
  i=x['input'];s=i['state'];v=x['actual_launch']['velocity'];t=next(e for e in s['enemies'] if e['target_id']==i['target_id'])
  observed=[];original=launch_physics.project_launch
  def capture(*a,**kw):
   r=original(*a,**kw);observed.append(r);return r
  with patch('launch_physics.project_launch',capture):
   c=score_candidate(s,i['ball'],t,i['stats'],i['path'],math.hypot(v[0],v[2]),i['profile'],coupled=True,launch_velocity=(v[0],v[2]))
  self.assertIn('player',c['coupled_chain']['pocketed'])
  self.assertEqual(c['expected_self_pocket_hp_loss'],2)
  r=observed[0]
  self.assertEqual(sum(not e['player_contact'] for e in r['contacts']),13)
  self.assertEqual(len(r['player_wall_events']),2)
  for a,b in zip(r['player_wall_events'],x['actual_physics']['wall_reflections']):
   self.assertEqual(a['position'],b['position_xz'])
   self.assertEqual(a['velocity'],b['velocity_after_physics_xz'])
 def test_owned_consumer_does_not_prove_future_aoe_or_kill(self):
  s=state(ball('heavy'),[])
  s['deck_balls']=[ball('chain_impact',attack=2,heavyCollisionConsumeAmount=5,heavyFinisherDamagePerCollision=1)]
  self.assertEqual(_marginal_heavy_consumer_value(s,0,1,{'a':20,'b':3},{},.5),0)
  self.assertEqual(_marginal_heavy_consumer_value(s,0,1,{'a':20,'b':2},{},.5),0)
  self.assertEqual(_marginal_heavy_consumer_value(s,5,1,{'a':20,'b':8},{},.5),0)
 def test_remaining_work_cost_prefers_shorter_exposure_without_ball_bonus(self):
  s={'player':{'current_hp':50},'_boss_work_reference':{'hp':10,'damage_per_shot':4,'attack_per_turn':2}}
  def c(d):return dict(score=0,score_breakdown={'damage':d},metrics={'direct_damage':d})
  a,b=rescore_boss_choice(c(2),s),rescore_boss_choice(c(6),s)
  self.assertEqual(a['score_breakdown']['future_turn_exposure'],-2)
  self.assertEqual(b['score_breakdown']['future_turn_exposure'],-1)
  x=c(6);x['definition_id']='player_guard'
  self.assertEqual(rescore_boss_choice(x,s)['score'],b['score'])
  x['metrics']['encounter_cleared']=True
  self.assertEqual(rescore_boss_choice(x,s)['score_breakdown']['future_turn_exposure'],0)
 def test_public_work_scale_uses_actual_board_and_offer_progress(self):
  choices=[dict(candidate_id=str(n),offer_index=n,score=1,score_breakdown={'damage':d},metrics={'direct_damage':d}) for n,d in enumerate((2,4,100))]
  class Store:
   def submit_command(self,*args):return {'ok':True,'evaluation':{'choices':choices}}
  s={'boss_state':{'target_id':'enemy:0'},'available_actions':['evaluate_boss_shots'],'enemies':[{'target_id':'enemy:0','hp':20,'attack':2}]}
  r=evaluate_boss_choices(Store(),s)
  self.assertEqual(r['remaining_work_reference'],{'hp':20,'best_damage':20,'damage_per_shot':4,'attack_per_turn':2})
  self.assertEqual(r['recommended']['candidate_id'],'2')
 def test_unavoidable_work_does_not_saturate_every_candidates_cost(self):
  s={'_boss_work_reference':{'hp':60,'best_damage':8,'damage_per_shot':4,'attack_per_turn':4}}
  def c(d):return dict(score=0,score_breakdown={'damage':d},metrics={'direct_damage':d})
  a,b=rescore_boss_choice(c(4),s),rescore_boss_choice(c(8),s)
  self.assertEqual(a['score_breakdown']['future_turn_exposure'],-2)
  self.assertEqual(b['score_breakdown']['future_turn_exposure'],0)
 def test_fatal_progress_is_not_a_reachable_work_reference(self):
  choices=[dict(candidate_id='safe',offer_index=0,score=1,score_breakdown={'damage':2},metrics={'direct_damage':2}),
           dict(candidate_id='fatal',offer_index=1,score=-900,score_breakdown={'damage':100,'fatal':-1000},metrics={'direct_damage':100})]
  class Store:
   def submit_command(self,*args):return {'ok':True,'evaluation':{'choices':choices}}
  s={'boss_state':{'target_id':'enemy:0'},'available_actions':['evaluate_boss_shots'],'enemies':[{'target_id':'enemy:0','hp':60,'attack':2}]}
  r=evaluate_boss_choices(Store(),s)
  self.assertEqual(r['remaining_work_reference']['best_damage'],2)
  self.assertEqual(r['remaining_work_reference']['damage_per_shot'],2)

if __name__=='__main__':unittest.main()
