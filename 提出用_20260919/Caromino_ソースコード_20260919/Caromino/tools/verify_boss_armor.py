"""Recheck archived stage-5 captures and retain source/executable provenance."""
from collections import Counter
import hashlib
from pathlib import Path
import capture_boss_armor as b

def main():
    p=b.h.PROJECT; root=p/'tools/runtime_tests/boss_armor'
    reports=[]; exe_hash=hashlib.sha256((p.parent/'x64/Debug/DX22_01_plane.exe').read_bytes()).hexdigest()
    groups={'basic_v1':['standard','heavy','pierce','bounce','anchor'],
            'basic30':['standard','heavy','pierce','bounce','anchor'],
            'scenarios_v1':['neutral_enemy','neutral_pocket'], 'scenarios_v4':['full_boss']}
    for label,cases in groups.items():
        for case in cases:
            folder=b.ROOT/label/case
            assert b.h.read(folder/'exe.json')['sha256']==exe_hash
            for shot in sorted(folder.glob('shot_*')):
                reports.append(dict(path=str(shot.relative_to(p)),**b.compare(shot)))
    for build in groups['basic_v1']:
        a=b.h.read(b.ROOT/'basic_v1'/build/'shot_01/actual.json')
        c=b.h.read(b.ROOT/'basic30'/build/'shot_01/actual.json')
        for result in (a,c):
            for ball in result['balls']: ball.pop('id')
        assert a==c,build
    assert len(reports)==32
    full=b.ROOT/'scenarios_v4/full_boss'
    log=b.h.read(next((full/'logs/balance').glob('run_*.json')))
    counts=Counter(e['event_type'] for e in log['events'])
    assert counts['boss_break_ball_hit']==6 and counts['boss_break_started']==3 and counts['boss_break_ended']==2
    for event in log['events']:
        if event['event_type']=='boss_break_ball_hit':
            d=event['details']; assert d['boss_damage']==4 and d['hp_before']-d['hp_after']==4
    assert b.h.read(full/'shot_20/final.json')['run_progress']['final_boss_defeated']
    ordinary=p/'tools/runtime_tests/fixed_timestep/boss_regression60'
    assert b.h.read(ordinary/'summary.json')['exe_sha256']==exe_hash
    assert len(b.h.read(ordinary/'prediction_comparison.json'))==6
    assert all(x['matched'] for x in b.h.read(ordinary/'comparison.json')['cases'])
    changed=[]
    for old in (root/'before').glob('*'):
        current=p/old.name
        if old.is_file() and current.is_file() and old.read_bytes()!=current.read_bytes(): changed.append(current)
    changed += [p/n for n in ('BossCombatRules.h','BreakBall.h','BreakBall.cpp')]
    changed += [p/'assets/data'/n for n in ('enemy_data.json','stage_01.json','encounter_balance.json')]
    manifest=dict(exe_sha256=exe_hash,boss_prediction_shots=32,ordinary_prediction_shots=6,
        max_position_velocity_error=max(r['max_error'] for r in reports),boss_30_vs_60_matched=True,
        boss_full_sequence_shots=20,boss_full_sequence_attack_fixture=12,boss_full_sequence_not_balance_measurement=True,
        event_counts=dict(counts),captures=reports,
        changed_sources={str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in changed})
    b.h.write(root/'final_verification.json',manifest)
    print('32 boss + 6 ordinary predictions match; 30/60 Hz match; 3 Break starts / 2 ends / victory verified')
    print('Changed game files:',len(changed),'Exe:',exe_hash)

if __name__=='__main__': main()
