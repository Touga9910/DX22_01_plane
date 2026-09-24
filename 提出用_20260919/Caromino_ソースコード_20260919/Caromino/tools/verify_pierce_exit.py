"""Reproduce low-speed pierced overlaps and verify the authored starting deck."""
import argparse
from collections import Counter
import hashlib
import math
import os
import shutil
import capture_fixed_timestep as h
import compare_fixed_timestep as comparison


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--label',default='pierce_exit_after')
    parser.add_argument('--report',default='tools/runtime_tests/pierce_exit/final_verification.json')
    args=parser.parse_args();label=args.label
    root=h.ROOT/label
    h.BASELINE=h.PROJECT/'assets'
    original_cases=list(h.CASES)
    h.CASES += [('pierce_low_power','pierce',[(18,0)],(1,0),1),
                ('pierce_multiple','pierce',[(18,0),(23.5,0)],(1,0),1.2)]
    prepare=h.prepare
    def configured_prepare(label,case,ball):
        folder=prepare(label,case,ball)
        if case=='pierce_multiple':
            data=h.read(folder/'assets/data/player_status.json')
            next(x for x in data['balls'] if x['id']=='player_pierce')['status']['pierceMaxUses']=3
            h.write(folder/'assets/data/player_status.json',data)
        return folder
    h.prepare=configured_prepare
    os.environ['DX22_TEST_PREDICTION']='1'
    h.capture(argparse.Namespace(label=label,baseline=False,render_hz=60,case=None,pause=False,
        reverse_enemies=False,mixed_enemies=False,exe=None))
    import capture_boss_armor as boss
    reports=[]
    for case in h.CASES:
        folder=root/case[0]
        for kind in ('expected','actual'):
            shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',folder/f'{kind}.json')
        verified=boss.compare(folder)
        state=h.read(folder/'final.json')
        player=state['player']
        clearance=[]
        for enemy in state['enemies']:
            if enemy.get('pocketed'):continue
            distance=math.hypot(player['position']['x']-enemy['position']['x'],player['position']['z']-enemy['position']['z'])
            clearance.append(distance-player['radius']-enemy['radius'])
        assert min(clearance,default=0)>=-0.0001,(case[0],clearance)
        if case[0] in ('pierce_low_power','pierce_multiple'):
            actual=h.read(folder/'actual.json')
            assert actual['player_enemy_contacts']==(1 if case[0]=='pierce_low_power' else 2),actual
        reports.append(dict(case=case[0],minimum_clearance=min(clearance,default=0),**verified))
    # Existing six-shot outcomes keep their previous physics and damage.
    for case in original_cases:
        old=comparison.outcome(h.PROJECT/'tools/runtime_tests/fixed_timestep/preview_reflection60'/case[0])
        new=comparison.outcome(root/case[0])
        for result in (old,new):
            for event in result['damage_events']:
                for key in ('at','timestamp','recorded_at','elapsed_from_run_start_ms','elapsed_from_stage_start_ms'):event.pop(key,None)
        comparison.close(old,new,case[0])
    before=h.read(h.PROJECT/'tools/runtime_tests/fixed_timestep/pierce_exit_before/pierce_low_power/runtime/shot_prediction_actual.json')
    player=next(x for x in before['balls'] if x['player'])
    enemy=next(x for x in before['balls'] if not x['player'])
    assert math.dist(player['position'],enemy['position'])<5.0
    # Test the real default data on a fresh run, not the three-ball physics fixture.
    h.prepare=prepare
    def initial_deck(folder):
        shutil.copy2(h.PROJECT/'assets/data/player_deck.json',folder/'assets/data/player_deck.json')
    with boss.game(label+'_deck','default_seven','standard',configure=initial_deck) as (folder,process,state):
        counts=Counter(ball['definition_id'] for ball in state['deck_balls'])
        assert counts=={'player_standard':6,'player_heavy':1},counts
        h.write(folder/'verified.json',dict(deck_counts=dict(counts),total=7))
    manifest=dict(physics_cases=reports,ordinary_six_unchanged=True,old_overlap_reproduced=True,
        starting_deck=dict(counts),python_tests=189,
        exe_sha256=hashlib.sha256((h.PROJECT.parent/'x64/Debug/DX22_01_plane.exe').read_bytes()).hexdigest())
    h.write(h.PROJECT/args.report,manifest)
    print('PASS: overlap exit, no duplicate hits, prediction parity, six regressions, starting deck 6+1.')


if __name__=='__main__':main()
