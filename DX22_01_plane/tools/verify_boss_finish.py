"""Pocket-finisher regressions in private debug presets; authored stats stay exact."""
import argparse
from contextlib import contextmanager
import copy
import ctypes
import hashlib
import os
import shutil
import subprocess
import capture_boss_armor as b
import verify_boss_escorts as escorts


@contextmanager
def fixture(label,case,build,hp=12,broken=True,break_shots=2,player_x=0,player_z=0,boss_z=26,minion_positions=None):
    folder=b.h.prepare(label,case,build)
    data=b.h.read(folder/'assets/data/player_status.json')
    ball=next(x for x in data['balls'] if x['id']=='player_'+build)
    status=dict(data['status'],impactAttack=1,shotShield=0,enemyCollisionShield=0)
    status.update(ball['status'])
    card=dict(definition_id=ball['id'],category=ball['category'],upgrade_level=0,
        upgrade_model_version=7,status=status,upgrade_table=ball['upgrades'])
    preset=b.h.read(b.h.PROJECT/'tools/runtime_tests/debug_mode/setup_boss.json')
    preset.update(hp=50,max_hp=50,money=0,seed=20260904,player_x=player_x,player_z=player_z,
        armor=0 if broken else 2,break_shots=break_shots if broken else 0,break_balls=[],
        highest_unlocked_ascension=0,selected_ascension=0,
        relics=[False]*len(preset['relics']),achievements=[True]*len(preset['achievements']))
    preset['deck']=[dict(copy.deepcopy(card),instance_id=i) for i in (1,2,3)]
    enemies=b.h.read(folder/'assets/data/enemy_data.json')['enemies']
    core=next(x for x in enemies if x['id']=='enemy_boss_core')
    minion=next(x for x in enemies if x['id']=='enemy_boss_minion')
    preset['enemies']=[dict(id=core['id'],hp=hp,max_hp=core['status']['maxHp'],status=core['status'],x=0,z=boss_z)]+[
        dict(id=minion['id'],hp=3,max_hp=3,status=minion['status'],x=x,z=z)
        for x,z in (minion_positions if minion_positions is not None else [(60,-26)])]
    if build=='pierce':
        # Spend the authored three pierce uses on a front line, then physically
        # push the core. Do not disable Pierce or change its balance values.
        preset['player_z']=-24
        preset['enemies']=preset['enemies'][:1]+[
            dict(id=minion['id'],hp=3,max_hp=3,status=minion['status'],x=0,z=z) for z in (-12,0,12)]
    b.h.write(folder/'saves/debug_battle_setup.json',preset)
    sentinel=b'{"regression_sentinel":"normal save must remain unchanged"}'
    (folder/'saves/run_save.json').write_bytes(sentinel)
    with (folder/'process.log').open('w',encoding='utf-8') as log:
        process=subprocess.Popen([str(b.EXE),'--debug-battle'],cwd=folder,
            env=dict(os.environ,DX22_TEST_PREDICTION='1'),stdout=log,stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            state=b.h.wait_for(lambda:s if (s:=b.h.read(folder/'runtime/game_mcp/game_state.json')).get('debug_mode',{}).get('active') and 'fire_shot' in s.get('available_actions',[]) else None,process)
            window=b.h.wait_for(lambda:b.h.window_for(process),process)
            ctypes.windll.user32.ShowWindow(window[0],0)
            b.h.write(folder/'initial.json',state)
            assert state['boss_state']['hp']==hp and state['boss_state']['armor']==preset['armor']
            assert sum(e.get('boss_minion',False) for e in state['enemies'])==4
            yield folder,process,state
            assert (folder/'saves/run_save.json').read_bytes()==sentinel
            assert not list((folder/'logs/balance').glob('run_*.json'))
        finally:
            if process.poll() is None:
                window=b.h.window_for(process)
                if window:ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
                assert process.wait(timeout=15)==0
            b.h.write(folder/'exe.json',dict(sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest()))


def fire(folder,process,state,action,**arguments):
    shot=folder/'shot_01';shot.mkdir();b.h.write(shot/'before.json',state)
    sequence=state['sequence'];b.h.command(folder,process,action,**arguments)
    def finished():
        s=b.h.read(folder/'runtime/game_mcp/game_state.json')
        return s if s.get('sequence',0)>sequence and (s.get('debug_mode',{}).get('finished') or s.get('game_state')=='aiming_direction') else None
    final=b.h.wait_for(finished,process,90);b.h.write(shot/'final.json',final)
    for kind in ('expected','actual'):shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',shot/f'{kind}.json')
    b.h.write(shot/'verification.json',escorts.verify(shot))
    return final


def trial(label,build,mode,expect_leftovers=False):
    with fixture(label,build+'_'+mode,build) as (folder,process,state):
        b.h.command(folder,process,'evaluate_boss_shots')
        evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        candidates=[c for c in evaluation['choices'] if c['metrics']['boss_killed'] and c['metrics']['pocket_damage']>0]
        assert candidates,'Need a lethal pocket candidate with authored ball stats'
        selected=max(candidates,key=lambda c:(-c['metrics']['minion_boss_hits'],c['score']))
        b.h.write(folder/'evaluation.json',evaluation);b.h.write(folder/'selected.json',selected)
        before_hp=state['player']['current_hp']
        if mode=='manual':
            b.h.command(folder,process,'select_ball',offer_index=selected['offer_index'])
            v=selected['velocity'];state=fire(folder,process,state,'fire_shot',direction_x=v['x'],direction_z=v['z'],power=selected['power'])
        else:state=fire(folder,process,state,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
        raw=b.h.read(next((folder/'logs/debug_battle').glob('run_*.json')))
        core=next(e for e in reversed(raw['events']) if e['event_type']=='boss_pocket_resolved')['details']
        assert core['hp_after']==0
        expected=b.h.read(folder/'shot_01/expected.json')
        remaining=sum(x['enemy_id']=='enemy_boss_minion' and x['active'] and not x['defeated'] for x in expected['balls'])
        assert remaining>0,'Regression must leave an escort at the physics endpoint'
        alive=[e for e in state['enemies'] if e.get('boss_minion') and not e['defeated'] and not e.get('pocketed')]
        if expect_leftovers:
            assert alive and not state['debug_mode']['finished'] and state['game_state']=='aiming_direction'
        else:
            assert expected['boss_encounter_cleared']
            assert expected['boss_minions_retired_after_shot']>=remaining
            assert selected['metrics']['encounter_cleared']
            assert selected['metrics']['minions_retired_after_shot']>=remaining
            assert selected['metrics']['expected_enemy_turn_damage']==0
            assert not alive and state['debug_mode']['finished']
            assert raw['stages'][0]['stage_result']['result']=='clear'
            assert raw['stages'][0]['stage_result']['total_shots']==1
            assert before_hp-state['player']['current_hp']==selected['metrics']['player_hp_loss']
            assert 'fire_shot' not in state['available_actions'] and 'fire_boss_shot' not in state['available_actions']
            try:
                b.h.command(folder,process,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
            except AssertionError:
                assert not b.h.read(folder/'runtime/game_mcp/last_result.json')['ok']
            else:raise AssertionError('A stale plan fired after victory')
            assert b.h.read(next((folder/'logs/debug_battle').glob('run_*.json')))['stages'][0]['stage_result']['total_shots']==1
        result=dict(build=build,mode=mode,shots=1,lethal_pocket=True,escorts_at_physics_stop=remaining,
            leftover_bug_reproduced=expect_leftovers,cleared_without_extra_shot=not expect_leftovers)
        b.h.write(folder/'summary.json',result);print(result,flush=True);return result


def living_core_control(label):
    with fixture(label,'living_core_guard','guard',hp=60,broken=False) as (folder,process,state):
        b.h.command(folder,process,'evaluate_boss_shots')
        evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        choices=[c for c in evaluation['choices'] if c['metrics']['pocket_count']==1 and
            c['metrics']['minion_boss_hits']==0 and not c['metrics']['break_started']]
        assert choices
        selected=choices[0];b.h.write(folder/'selected.json',selected)
        hp=state['player']['current_hp']
        final=fire(folder,process,state,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
        assert not selected['metrics']['encounter_cleared'] and selected['metrics']['stop_shield']==3
        assert not final['debug_mode']['finished'] and final['boss_state']['hp']>0
        assert final['boss_state']['armor']==1 and final['boss_state']['break_shots_remaining']==0
        assert sum(e.get('boss_minion') and not e['defeated'] for e in final['enemies'])==4
        assert hp-final['player']['current_hp']==selected['metrics']['player_hp_loss']+selected['metrics']['expected_enemy_turn_damage']
        result=dict(living_core_preserved=True,escorts_preserved=4,guard_shield=3,
            hp_damage=hp-final['player']['current_hp'],armor_after=1)
        b.h.write(folder/'summary.json',result);print(result,flush=True);return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True)
    parser.add_argument('--build');parser.add_argument('--mode',choices=('manual','ai'))
    parser.add_argument('--expect-leftovers',action='store_true');parser.add_argument('--living-core',action='store_true');args=parser.parse_args()
    results=[]
    if args.living_core:
        living_core_control(args.label)
        raise SystemExit(0)
    for build in ([args.build] if args.build else ('standard','heavy','pierce','bounce','anchor')):
        for mode in ([args.mode] if args.mode else ('manual','ai')):
            results.append(trial(args.label,build,mode,args.expect_leftovers))
    b.h.write(b.ROOT/args.label/'verification_summary.json',dict(state='completed',
        exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest(),results=results,
        verified_shots=sum(r['shots'] for r in results)))
