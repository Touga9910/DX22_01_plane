import unittest
from unittest.mock import patch
from test_build_shot_evaluator import ball,enemy,state
from build_shot_evaluator import build_joint_shot_context

class LethalPreferenceSafety(unittest.TestCase):
    def choices(self,kill_loss):
        heavy=ball('heavy');standard=ball('standard');standard.update(index=1,instance_id=2,selected=False)
        s=state(heavy,[enemy(0,0,15)]);s['offered_balls'].append(standard)
        def evaluate(s,p,*,ball):
            kill=ball['definition_id']=='player_standard'
            loss=kill_loss if kill else 0.
            return [dict(definition_id=ball['definition_id'],score=18. if kill else 20.,
                power=3,target_id='enemy:0',predicted_kill_count=int(kill),
                expected_net_hp_loss=loss,conservative_net_hp_loss=loss,
                robustness={'samples':[{'conservative_loss':loss}]})]
        with patch('build_shot_evaluator.evaluate_build_shots',side_effect=evaluate):
            return build_joint_shot_context(s,{'mcp_shot_policy':'standard'})

    def test_kill_preference_cannot_override_safer_generator(self):
        result=self.choices(2.)
        self.assertEqual(result['recommended']['definition_id'],'player_heavy')

    def test_lower_scoring_kill_does_not_erase_equal_safety_setup_value(self):
        result=self.choices(0.)
        self.assertEqual(result['recommended']['definition_id'],'player_heavy')
        self.assertFalse(result['selection_diagnostics']['legacy_lethal_preference_used'])
