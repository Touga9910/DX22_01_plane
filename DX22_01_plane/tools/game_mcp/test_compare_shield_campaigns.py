import json
from pathlib import Path
import tempfile
import unittest
from compare_balance_campaigns import BUILDS, compare_campaigns
from compare_shield_campaigns import boss_shot_telemetry


class ShieldComparisonTests(unittest.TestCase):
    def test_only_post_core_cleanup_can_lack_boss_decisions(self):
        stage=dict(stage_result=dict(total_shots=3),shots=[
            dict(ball_id=ball,elapsed_from_run_start_ms=time) for ball,time in
            [('player_guard',10),('player_standard',20),('player_guard',30)]])
        events=[dict(event_type='boss_ai_decision',details=dict(boss_evaluation=dict(definition_id=ball)))
                for ball in ('player_guard','player_standard')]
        death=dict(event_type='enemy_defeated',elapsed_from_run_start_ms=25,
                   details=dict(enemy_id='enemy_boss_core'))
        events.append(death)
        result=boss_shot_telemetry(stage,events)
        self.assertEqual(result,dict(boss_total_shots=3,boss_guard_shots=2,
                                    boss_core_kill_shots=2,boss_cleanup_shots=1))
        death['elapsed_from_run_start_ms']=35
        with self.assertRaisesRegex(ValueError,'Incomplete boss decision'):
            boss_shot_telemetry(stage,events)

    def test_target_override_selects_paired_baseline_subset(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0008')
            self.create_campaign(current,'B0009')
            originals={}
            for build in BUILDS:
                path=base/build/'all_runs.jsonl'
                original=path.read_bytes();record=json.loads(original)
                extra=dict(record,run_seed=1002)
                path.write_bytes(original+(json.dumps(extra)+'\n').encode('utf-8'))
                originals[path]=path.read_bytes()
            (current/'collection_target.json').write_text(json.dumps(dict(valid_runs_per_build=1)),encoding='utf-8')
            report=compare_campaigns(base,current,target=20)
            result=json.loads((current/'B0008_vs_B0009_comparison.json').read_text(encoding='utf-8'))
            self.assertEqual(result['baseline_available_runs'],10)
            self.assertEqual(result['paired_seed_build_pairs'],5)
            self.assertTrue(result['paired_baseline_subset'])
            self.assertEqual(result['comparisons'][-1]['before']['samples'],5)
            self.assertIn('5ラン',Path(report).name)
            self.assertTrue(all(path.read_bytes()==contents for path,contents in originals.items()))

    def create_campaign(self, directory, version):
        directory.mkdir()
        batches=[]
        for build in BUILDS:
            batch=directory/build
            batch.mkdir()
            batches.append(str(batch))
            lost=version=='B0008' and build=='heavy'
            count=3 if lost else 2
            events=[]
            for shot in range(count):
                guard=version=='B0007' or shot==0
                choice=dict(definition_id='player_guard' if guard else 'player_standard',
                            score_breakdown=dict(stop_shield=2.4 if guard and version=='B0007' else 0),
                            metrics=dict(break_started=shot==0 and version=='B0009',
                                encounter_cleared=shot==count-1,expected_enemy_turn_damage=0,minions_retired_after_shot=1,recovery_strong_attack_announced=False,advance_supply_attempted_if_turn_continues=False,advance_supply_requested_if_turn_continues=0))
                events.append(dict(stage_index=1,event_type='boss_ai_decision',details=dict(
                    boss_evaluation=choice,model={'B0008':'boss_shared_ccd_v3_safety_only','B0009':'boss_shared_ccd_v4_escorts','B0010':'boss_shared_ccd_v4_escorts','B0011':'boss_shared_ccd_v4_escorts','B0012':'boss_shared_ccd_v4_escorts','B0013':'boss_shared_ccd_v4_escorts'}.get(version,'boss_shared_ccd_v2_pocket'))))
            raw=dict(balance_version=version,run_context=dict(run_combat_mode='legacy',mcp_shot_policy='standard',
                controller_profile='intermediate',dynamic_balance_enabled_at_start=False),events=events,
                stages=[dict(stage_id='boss',stage_index=1,stage_type='boss',stage_result=dict(
                    result='game_over' if lost else 'clear',player_damage_taken=3 if lost else int(version=='B0008'),total_shots=count))])
            source=batch/'run.json'
            if version in ('B0011','B0012','B0013'):
                for i,event in enumerate(events):event['elapsed_from_run_start_ms']=9+20*i
                events.append(dict(stage_index=1,event_type='enemy_defeated',elapsed_from_run_start_ms=35,details=dict(enemy_id='enemy_boss_core')))
                raw['stages'][0]['shots']=[dict(ball_id=ball,elapsed_from_run_start_ms=time,
                    duration_ms=10,enemies_alive_after=remaining) for ball,time,remaining in
                    [('player_guard',10,1),('player_standard',30,0)]]
            source.write_text(json.dumps(raw),encoding='utf-8')
            record=dict(source_log=str(source),normal_end=True,configuration_signature=version,
                        build_profile=build,run_seed=1001,final_boss_defeated=not lost)
            (batch/'all_runs.jsonl').write_text(json.dumps(record)+'\n',encoding='utf-8')
        (directory/'analysis.json').write_text(json.dumps(dict(batches=batches)),encoding='utf-8')

    def test_escort_version_pair_and_output_names(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0008')
            self.create_campaign(current,'B0009')
            report=compare_campaigns(base,current,target=1)
            self.assertIn('B0008とB0009',Path(report).name)
            result=json.loads((current/'B0008_vs_B0009_comparison.json').read_text(encoding='utf-8'))
            self.assertEqual(result['paired_seed_build_pairs'],5)
            self.assertEqual(result['comparisons'][-1]['loss_to_win'],1)
            self.assertEqual(result['comparisons'][-1]['after']['guard_selection_rate'],.5)

    def test_finish_fix_version_pair(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0010')
            self.create_campaign(current,'B0011')
            report=compare_campaigns(base,current,target=1)
            self.assertIn('B0010とB0011',Path(report).name)
            data=json.loads((current/'B0010_vs_B0011_comparison.json').read_text(encoding='utf-8'))
            total=data['comparisons'][-1]['after']
            self.assertEqual(total['boss_cleanup_shots'],0)
            self.assertEqual(total['residual_battles'],0)
            self.assertEqual(total['actual_core_shots'],10)
            self.assertEqual(total['guard_selection_rate'],.5)

    def test_recovery_version_pair(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0011')
            self.create_campaign(current,'B0012')
            report=compare_campaigns(base,current,target=1)
            self.assertIn('B0011とB0012',Path(report).name)
            data=json.loads((current/'B0011_vs_B0012_comparison.json').read_text(encoding='utf-8'))
            total=data['comparisons'][-1]['after']
            self.assertEqual(total['strong_executions'],0)
            self.assertEqual(total['strong_interruptions'],0)
            self.assertEqual(total['residual_battles'],0)

    def test_advance_supply_version_pair(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0012')
            self.create_campaign(current,'B0013')
            report=compare_campaigns(base,current,target=1)
            data=json.loads((current/'B0012_vs_B0013_comparison.json').read_text(encoding='utf-8'))
            self.assertEqual(data['comparisons'][-1]['after']['advance_spawned'],0)
            self.assertEqual(data['paired_seed_build_pairs'],5)

    def test_formation_pair_opening_break_and_telemetry_audit(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0009')
            self.create_campaign(current,'B0010')
            report=compare_campaigns(base,current,target=1)
            self.assertIn('B0009とB0010',Path(report).name)
            result=json.loads((current/'B0009_vs_B0010_comparison.json').read_text(encoding='utf-8'))
            total=result['comparisons'][-1]
            self.assertEqual(total['before']['boss_opening_break_rate'],1)
            self.assertEqual(total['after']['boss_opening_break_rate'],0)
            source=current/'standard/run.json'
            raw=json.loads(source.read_text(encoding='utf-8'))
            del raw['events'][0]['details']['boss_evaluation']['metrics']['break_started']
            source.write_text(json.dumps(raw),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'Missing opening Break telemetry'):
                compare_campaigns(base,current,target=1)

    def test_pooled_guard_rate_and_loss_denominators_and_old_ai_rejection(self):
        with tempfile.TemporaryDirectory() as root:
            base,current=Path(root)/'before',Path(root)/'after'
            self.create_campaign(base,'B0007')
            self.create_campaign(current,'B0008')
            provenance=dict(revision='presentation-fix',retained_valid_runs=3,
                            old_exe_sha256='old-build',new_exe_sha256='fixed-build')
            (current/'crashfix_provenance_fixture.json').write_text(json.dumps(provenance),encoding='utf-8')
            report=compare_campaigns(base,current,target=1)
            self.assertTrue(Path(report).exists())
            data=json.loads((current/'B0007_vs_B0008_comparison.json').read_text(encoding='utf-8'))
            self.assertEqual(data['runtime_patch_provenance'],[provenance])
            overall=data['comparisons'][-1]
            self.assertEqual(overall['after']['win_rate'],.8)
            self.assertEqual(overall['after']['boss_mean_kill_shots'],2)
            self.assertEqual(overall['after']['boss_mean_damage'],1.4)
            self.assertAlmostEqual(overall['after']['guard_selection_rate'],5/11)
            self.assertEqual(overall['before']['guard_selection_rate'],1)
            self.assertEqual(overall['paired_boss_wins'],4)
            source=current/'standard/run.json'
            raw=json.loads(source.read_text(encoding='utf-8'))
            raw['events'][0]['details']['boss_evaluation']['score_breakdown']['stop_shield']=2.4
            source.write_text(json.dumps(raw),encoding='utf-8')
            with self.assertRaisesRegex(ValueError,'old shield scoring'):
                compare_campaigns(base,current,target=1)


if __name__=='__main__':unittest.main()
