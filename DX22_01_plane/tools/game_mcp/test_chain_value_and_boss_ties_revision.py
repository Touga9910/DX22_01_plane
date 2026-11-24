import json
import unittest
from pathlib import Path

from boss_shot_policy import rank_boss_choices
from build_shot_evaluator import _heavy_inventory_option_value, _marginal_heavy_consumer_value
from test_build_shot_evaluator import ball, state


class ChainValueAndBossTiesRevision(unittest.TestCase):
    def test_inventory_needs_owned_spender_and_unfilled_capacity(self):
        s = state(ball('heavy'), [])
        self.assertEqual(_heavy_inventory_option_value(s, 0, 5, 1), 0)
        s['deck_balls'] = [ball('chain_impact', heavyCollisionConsumeAmount=5)]
        self.assertEqual(_heavy_inventory_option_value(s, 0, 50, 1), .5)
        self.assertEqual(_heavy_inventory_option_value(s, 5, 5, 1), 0)
        self.assertAlmostEqual(_heavy_inventory_option_value(s, 4, 5, .5), .05)
        self.assertEqual(_marginal_heavy_consumer_value(s, 0, 5, {'a':20, 'b':3}, {}, 1), 0)

    def test_score_tie_prefers_total_hp_then_immediate_hp(self):
        a = dict(score=1., metrics=dict(player_hp_loss=2, expected_enemy_turn_damage=0))
        b = dict(score=1., metrics=dict(player_hp_loss=0, expected_enemy_turn_damage=0))
        self.assertIs(rank_boss_choices([a,b])[0], b)
        c = dict(score=1., metrics=dict(player_hp_loss=0, expected_enemy_turn_damage=2))
        self.assertIs(rank_boss_choices([a,c])[0], c)

    def test_numeric_tie_does_not_override_real_score_difference(self):
        a = dict(score=1., metrics=dict(player_hp_loss=2))
        b = dict(score=1.-1e-8, metrics=dict(player_hp_loss=0))
        self.assertIs(rank_boss_choices([a,b])[0], b)
        b['score'] = .999
        self.assertIs(rank_boss_choices([a,b])[0], a)

    def test_saved_boss_tie_selects_safe_alternative(self):
        fixture = Path(__file__).with_name('boss_safety_tie_regression.json')
        rows = json.loads(fixture.read_text(encoding='utf-8'))
        self.assertTrue(rows)
        for row in rows:
            candidates = [row['selected']] + row['safer_same_score']
            best = rank_boss_choices(candidates)[0]
            def loss(c):
                return sum(c['metrics'].get(k,0) for k in ('player_hp_loss','expected_enemy_turn_damage'))
            self.assertLess(loss(best), loss(row['selected']))


if __name__ == '__main__':
    unittest.main()
