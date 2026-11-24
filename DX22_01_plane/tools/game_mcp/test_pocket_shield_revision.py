import unittest
import copy
from unittest.mock import patch

from build_shot_evaluator import evaluate_build_shots, hp_exposure, score_candidate, candidate_paths, ball_stats, _coupled_rescore, build_joint_shot_context
from coupled_chain import project, trigger_time
from test_build_shot_evaluator import ball, enemy, state


class PocketShieldRevision(unittest.TestCase):
    def test_equal_offer_reuse_keeps_scores_order_and_instance_identity(self):
        value = state(ball('guard',attack=2), [enemy(0,0,18,hp=4),enemy(1,9,24,hp=4)])
        duplicate = copy.deepcopy(value['offered_balls'][0])
        duplicate.update(index=1,instance_id=99,selected=False)
        heavy = ball('heavy',attack=2)
        heavy.update(index=2,instance_id=3,selected=False)
        value['offered_balls'] += [duplicate,heavy]
        with patch('build_shot_evaluator.evaluate_build_shots',wraps=evaluate_build_shots) as calls:
            cached = build_joint_shot_context(value,{})
            self.assertEqual(calls.call_count,2)
        with patch('build_shot_evaluator._offer_evaluation_key',side_effect=lambda b:b['instance_id']):
            with patch('build_shot_evaluator.evaluate_build_shots',wraps=evaluate_build_shots) as calls:
                uncached = build_joint_shot_context(value,{})
                self.assertEqual(calls.call_count,3)
        self.assertEqual(cached,uncached)
        guards = [c for c in cached['choices'] if c['definition_id']=='player_guard']
        self.assertEqual({c['instance_id'] for c in guards},{1,99})
        self.assertIsNot(guards[0]['score_breakdown'],guards[1]['score_breakdown'])

    def test_corpse_deflection_is_simulated_before_target_and_spends_shield(self):
        value = state(ball('guard',attack=1,shotShield=3), [enemy(0,0,25,hp=100,attack=2),
            enemy(1,2,8,hp=0,defeated=True)])
        value['table']['pockets'] = [{'position':{'x':-6,'z':18},'radius':1}]
        offer = value['offered_balls'][0]
        path = {'mode':'direct','aim':(0.,25.),'kind':'center'}
        choice = score_candidate(value,offer,value['enemies'][0],ball_stats(value,offer),path,7,{},coupled=True)
        self.assertEqual(choice['retained_body_blocker'],'enemy:1')
        self.assertIsNotNone(choice['coupled_chain'])
        self.assertIn('player',choice['coupled_chain']['pocketed'])
        self.assertEqual(choice['self_pocket_risk'],1)
        self.assertEqual(choice['expected_damage_by_target'],{})
        self.assertEqual(choice['expected_shield_after_pocket'],1)
        self.assertEqual(choice['expected_incoming_damage'],1)

    def test_overlapping_pocket_fires_even_on_exit(self):
        self.assertEqual(trigger_time((1, 0), (2, 0), 3, 1), 0)
        value = state(ball(radius=1), [enemy(0, 30, 30)])
        value['table']['pockets'] = [{'position': {'x': 0, 'z': 0}, 'radius': 3}]
        result = project(value, value['offered_balls'][0]['status'], (1, 0), (2, 0), 'enemy:0', (0, 0))
        self.assertIn('player', result['pocketed'])
        self.assertLess(result['player_pocket_time'], .001)

    def test_pocketed_living_enemy_and_corpse_cannot_rebound_into_another_body(self):
        for dead in (False, True):
            with self.subTest(dead=dead):
                value = state(ball(radius=1), [{**enemy(0, 0, 0, hp=0 if dead else 10), 'defeated': dead, 'radius': 1},
                                               {**enemy(1, -8, 0), 'radius': 1}])
                value['table']['pockets'] = [{'position': {'x': 10, 'z': 0}, 'radius': 2}]
                result = project(value, value['offered_balls'][0]['status'], (-50, -50), (0, 0), 'enemy:0', (2, 0))
                self.assertIn('enemy:0', result['pocketed'])
                self.assertEqual(result['contacts'], [])

    def test_fractional_pocket_exposure_does_not_reuse_shield(self):
        incoming, pocket, shield = hp_exposure(2, 3, 2, .5)
        self.assertEqual(incoming, .5)  # 0 without falling, 1 after falling.
        self.assertEqual(pocket, 0)
        self.assertEqual(shield, 2)

    def test_shield_gained_after_pocket_cannot_absorb_past_damage(self):
        late = hp_exposure(0, 0, 2, 1, [(4, 3)], pocket_time=2)
        early = hp_exposure(0, 0, 2, 1, [(1, 3)], pocket_time=2)
        self.assertEqual(late, (0, 2, 3))
        self.assertEqual(early, (0, 0, 1))

    def test_reported_hp_components_share_all_nine_scenarios(self):
        value = state(ball(attack=2, shotShield=3), [enemy(0, 0, 18, hp=3), enemy(1, 8, 27, hp=4)])
        value['table']['pockets'] = [{'position': {'x': 8, 'z': 35}, 'radius': 3}]
        choices = evaluate_build_shots(value, {'human_error': {'aim_radius_ratio': .02, 'power_ratio': .05}}, fixed_power=3)
        self.assertTrue(choices)
        for c in choices:
            self.assertEqual(len(c['robustness']['samples']), 9)
            self.assertAlmostEqual(c['expected_net_hp_loss'], c['expected_incoming_damage'] + c['expected_self_pocket_hp_loss'] - c['expected_repair'], delta=.003)
            self.assertAlmostEqual(c['score'], sum(c['score_breakdown'].values()), places=3)
            self.assertGreaterEqual(c['conservative_incoming_damage'] + .001, c['expected_incoming_damage'])

    def test_unreachable_stress_path_does_not_inherit_repair_or_completed_motion(self):
        value = state(ball(attack=2), [enemy(0, 0, 18, hp=5)])
        offer = value['offered_balls'][0]
        stats = ball_stats(value, offer)
        profile = {'human_error': {'aim_radius_ratio': .02, 'power_ratio': .05}}
        path = next(candidate_paths(value, value['enemies'][0], stats, 'direct', -1, 'damage', -1))
        candidate = score_candidate(value, offer, value['enemies'][0], stats, path, 3, profile)
        nominal = copy.deepcopy(candidate)
        nominal.update(self_pocket_risk=1., expected_self_pocket_hp_loss=2., expected_repair=1.,
                       expected_incoming_damage=1., expected_net_hp_loss=2.,
                       conservative_incoming_damage=1., conservative_net_hp_loss=2.,
                       coupled_chain={'complete':True})
        calls = 0
        def simulated_path(*args, **kwargs):
            nonlocal calls
            calls += 1
            return None if calls == 2 else copy.deepcopy(nominal)
        with patch('build_shot_evaluator.score_candidate', side_effect=simulated_path):
            result = _coupled_rescore(value, offer, stats, candidate, profile)
        failed = result['robustness']['samples'][1]
        self.assertFalse(failed['complete'])
        self.assertEqual(failed['loss'], failed['incoming'] + failed['pocket_loss'])
        self.assertEqual(failed['kills'], [])
        self.assertAlmostEqual(result['expected_net_hp_loss'], result['expected_incoming_damage'] + result['expected_self_pocket_hp_loss'] - result['expected_repair'], delta=.003)


if __name__ == '__main__':
    unittest.main()
