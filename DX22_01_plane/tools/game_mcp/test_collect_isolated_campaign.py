import copy
import unittest
from collect_isolated_campaign import validate_entry, campaign_seeds


class CampaignSeedsTests(unittest.TestCase):
    def test_explicit_small_paired_run_target(self):
        self.assertEqual(campaign_seeds([5,6,9],target=3),[5,6,9])
        with self.assertRaises(ValueError):campaign_seeds([5,6],target=3)
        with self.assertRaises(ValueError):campaign_seeds(None,target=0)
    def test_explicit_seed_order_is_preserved(self):
        seeds = list(range(2026112110, 2026112100, -1))
        self.assertEqual(campaign_seeds(seeds), seeds)
        self.assertEqual(campaign_seeds(None), list(range(1001, 1011)))

    def test_invalid_or_paired_override_is_rejected(self):
        for seeds in ([1] * 10, list(range(9)), list(range(11)), list(range(9)) + [-1], list(range(9)) + [2**32]):
            with self.subTest(seeds=seeds), self.assertRaises(ValueError):
                campaign_seeds(seeds)
        with self.assertRaisesRegex(ValueError, 'paired'):
            campaign_seeds(list(range(10)), object())


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
