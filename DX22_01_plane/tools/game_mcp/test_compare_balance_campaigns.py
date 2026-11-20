import json
import tempfile
import unittest
from pathlib import Path
from compare_balance_campaigns import BUILDS, boss_summary, compare_campaigns, group_comparison


class ComparisonTests(unittest.TestCase):
    def test_failed_boss_attempt_is_not_a_kill_time_sample(self):
        def row(won, shots):
            return dict(boss_reached=True, boss_won=won, boss_shots=shots,
                        boss_damage=10, break_starts=1, pocket_effects=1, pocket_finishers=0)
        result = boss_summary([row(True, 5), row(True, 9), row(False, 40)])
        self.assertEqual(result['mean_shots_to_kill'], 7)
        self.assertEqual(result['median_shots_to_kill'], 7)
        self.assertEqual(result['reached'], 3)
        self.assertEqual(result['wins'], 2)

    def test_unreached_stage_is_missing_not_zero_win_rate(self):
        rows = group_comparison([dict(stage_id='boss', won=True, damage=0)], [], ('stage_id',))
        self.assertIsNone(rows[0]['after']['win_rate'])
        self.assertIsNone(rows[0]['mean_damage_delta'])

    def test_paired_campaign_report_and_version_guards(self):
        with tempfile.TemporaryDirectory() as folder:
            base, current = Path(folder)/'before', Path(folder)/'after'
            for directory, version in ((base, 'B0006'), (current, 'B0007')):
                directory.mkdir()
                batches = []
                for build in BUILDS:
                    batch = directory/build
                    batch.mkdir()
                    batches.append(str(batch))
                    won = not (version=='B0007' and build=='standard')
                    raw = dict(balance_version=version,
                               run_context=dict(run_combat_mode='legacy', mcp_shot_policy='standard',
                                                controller_profile='intermediate', dynamic_balance_enabled_at_start=False),
                               stages=[dict(stage_id='boss_001',stage_type='boss',stage_index=1,
                                            stage_result=dict(result='clear' if won else 'game_over',player_damage_taken=10 if version=='B0007' else 0,total_shots=(9 if won else 40) if version=='B0007' else 1))],
                               events=[])
                    source = batch/'run.json'
                    source.write_text(json.dumps(raw), encoding='utf-8')
                    record = dict(source_log=str(source), normal_end=True, configuration_signature=version,
                                  build_profile=build, run_seed=1001, final_boss_defeated=won)
                    (batch/'all_runs.jsonl').write_text(json.dumps(record)+'\n', encoding='utf-8')
                (directory/'analysis.json').write_text(json.dumps(dict(batches=batches)), encoding='utf-8')
            report = compare_campaigns(base, current, target=1)
            self.assertTrue(Path(report).exists())
            result = json.loads((current/'B0006_vs_B0007_comparison.json').read_text(encoding='utf-8'))
            self.assertEqual(result['paired_seed_build_pairs'], 5)
            self.assertEqual(result['overall_before']['win_rate'], 1)
            self.assertEqual(result['overall_after']['win_rate'], .8)
            self.assertEqual(result['boss_overall_after']['mean_shots_to_kill'], 9)
            source = current/'heavy/run.json'
            raw = json.loads(source.read_text(encoding='utf-8'))
            raw['balance_version']='B0006'
            source.write_text(json.dumps(raw), encoding='utf-8')
            with self.assertRaises(ValueError):
                compare_campaigns(base, current, target=1)


if __name__ == '__main__':
    unittest.main()
