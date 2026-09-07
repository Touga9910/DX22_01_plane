"""Verify captured stage-6 results without rewriting their executable provenance."""
import hashlib
from pathlib import Path
import capture_boss_armor as b
import compare_fixed_timestep as normal


def main():
    p=b.h.PROJECT; reports=[]; summaries=[]
    for label in ('ai_base_v1','ai_upgraded_v1'):
        for folder in sorted((b.ROOT/label).iterdir()):
            if not folder.is_dir(): continue
            summary=b.h.read(folder/'summary.json')
            assert summary['status']=='win' and summary['boss_hp']==0
            shots=sorted(folder.glob('shot_*')); assert len(shots)==summary['shots']
            for shot in shots: reports.append(dict(path=str(shot.relative_to(p)),**b.compare(shot)))
            summaries.append(dict(capture=str(folder.relative_to(p)),exe_sha256=b.h.read(folder/'exe.json')['sha256'],**summary))
    assert len(reports)==127 and len(summaries)==10
    cpp=b.ROOT/'ai_controls_v3/cpp_auto'; reference=b.ROOT/'ai_base_v1/standard_level0_mcp'
    cppshots=sorted(cpp.glob('shot_*')); assert len(cppshots)==11
    log=b.h.read(next((cpp/'logs/balance').glob('run_*.json')))
    assert sum(e['event_type']=='boss_ai_decision' for e in log['events'])==11
    for shot in cppshots:
        reports.append(dict(path=str(shot.relative_to(p)),**b.compare(shot)))
        a=b.h.read(shot/'actual.json'); r=b.h.read(reference/shot.name/'actual.json')
        for result in (a,r):
            for ball in result['balls']: ball.pop('id')  # Process-local object addresses.
        normal.close(a,r,shot.name)
    last=b.h.read(cppshots[-1]/'actual.json')
    assert any(x['boss'] and x['hp']==0 and x['defeated'] for x in last['balls'])
    final=b.h.read(cppshots[-1]/'final.json')
    assert final['game_state']=='clear_reward' and final['player']['current_hp']==40
    b.h.write(cpp/'verified.json',dict(shots=11,win=True,actual_cpp_autoplay=True,matched_mcp_physics=True,
        note='Verified archived captures after fixing the harness assumption: cleared boss_state may be null.'))
    for label,mode in (('ai_controls_final','mixed'),('ai_controls_v3','legacy')):
        folder=b.ROOT/label/mode
        assert b.h.read(folder/'verified.json')
        reports.append(dict(path=str(folder.relative_to(p)),**b.compare(folder/'shot_01')))
    ordinary=p/'tools/runtime_tests/fixed_timestep/boss_ai_regression60'
    normal_reports=[]
    for case in b.h.CASES:
        folder=ordinary/case[0]
        # Normal capture uses the same prediction files without shot subfolders.
        import shutil
        for kind in ('expected','actual'):
            shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',folder/f'{kind}.json')
        normal_reports.append(dict(case=case[0],**b.compare(folder)))
    assert all(x['matched'] for x in b.h.read(ordinary/'comparison.json')['cases'])
    b.h.write(ordinary/'prediction_comparison.json',normal_reports)
    root=p/'tools/runtime_tests/boss_ai'
    assert all(old.read_bytes()==(p/'assets/data'/old.name).read_bytes() for old in (root/'before/data').glob('*.json'))
    tracked=[p/n for n in ('GameBossAI.cpp','Game.h','GameAutoPlay.cpp','GameMcpBridge.cpp','BallShotPrediction.cpp','BallShotPrediction.h','DX22_01_plane.vcxproj')]
    tracked.extend((p/'tools/game_mcp').glob('*.py'))
    tracked.extend(p/'tools'/n for n in ('compare_boss_builds.py','exercise_boss_ai_controls.py','verify_boss_ai.py'))
    digest=lambda file:hashlib.sha256(file.read_bytes()).hexdigest()
    manifest=dict(final_exe_sha256=digest(p.parent/'x64/Debug/DX22_01_plane.exe'),
        comparison_shots=127,cpp_autoplay_shots=11,control_shots=2,normal_shots=6,
        authored_balance_data_unchanged=True,
        max_position_velocity_error=max(x['max_error'] for x in reports+normal_reports),
        summaries=summaries,verification=reports,normal_verification=normal_reports,
        control_executable_hashes={str(f.relative_to(p)):b.h.read(f/'exe.json')['sha256'] for f in (cpp,b.ROOT/'ai_controls_final/mixed',b.ROOT/'ai_controls_v3/legacy')},
        limitations=['Single fixed boss layout, no relics, deterministic; not a full-map run or population win rate.',
          'Comparisons predate equivalent-offer alias refinement; final C++ standard run and mixed/legacy controls verify that refinement.'],
        source_sha256={str(f.relative_to(p)):digest(f) for f in tracked})
    b.h.write(root/'final_verification.json',manifest)
    print('Verified: 127 comparison + 11 C++ autoplay + 2 control + 6 ordinary shots; total 146.')


if __name__=='__main__':main()
