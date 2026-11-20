import copy
import unittest
from collect_isolated_campaign import validate_entry


class PairedEntryTests(unittest.TestCase):
    def baseline(self):
        return dict(stages=[dict(stage_id='normal',stage_index=1,stage_type='normal',
            stage_result=dict(result='clear',player_damage_taken=3,total_shots=2)),
            dict(stage_id='boss',stage_index=2,stage_type='boss',stage_start=dict(player_hp=47),
            stage_context=dict(deck=[dict(id='player_standard',upgrade_level=2)],owned_relics=['Power Core']))])

    def test_boss_damage_changes_are_allowed_but_entry_shuffle_is_rejected(self):
        before=self.baseline();after=copy.deepcopy(before)
        after['stages'][1]['stage_result']=dict(result='clear',player_damage_taken=7,total_shots=8)
        validate_entry(before,after)
        after['stages'][1]['stage_context']['deck'][0]['upgrade_level']=1
        with self.assertRaisesRegex(ValueError,'Boss entry'):
            validate_entry(before,after)

    def test_route_damage_mismatch_is_rejected(self):
        before=self.baseline();after=copy.deepcopy(before)
        after['stages'][0]['stage_result']['player_damage_taken']=4
        with self.assertRaisesRegex(ValueError,'Pre-boss'):
            validate_entry(before,after)


if __name__=='__main__':unittest.main()
