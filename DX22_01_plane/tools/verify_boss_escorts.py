"""Isolated live/prediction/intent regression for the escort boss encounter."""
import argparse
from collections import Counter
import hashlib
import shutil
import capture_boss_armor as b


def verify(shot):
    expected=b.h.read(shot/'expected.json');actual=b.h.read(shot/'actual.json')
    assert expected['complete'] and expected['world_unchanged'] and not expected['path_truncated']
    for key in ('player_enemy_contacts','enemy_enemy_contacts'):assert expected[key]==actual[key],key
    for key in ('boss_encounter_cleared','boss_minions_retired_after_shot','boss_recovery_strong_attack_announced',
                'boss_advance_supply_attempted_if_turn_continues','boss_advance_supply_requested_if_turn_continues'):
        if key in expected:assert expected[key]==actual[key],(key,expected[key],actual[key])
    left={x['id']:x for x in expected['balls']};right={x['id']:x for x in actual['balls']}
    assert not(set(right)-set(left))
    for key in set(left)-set(right):assert left[key]['defeated'] and not left[key]['active'],left[key]
    error=0
    for key in set(left)&set(right):
        x,y=left[key],right[key]
        returned=x['player'] and x['pocketed'] and not x['defeated'] and not y['pocketed']
        for field in ('position','velocity'):
            if returned:continue
            for a,c in zip(x[field],y[field]):
                error=max(error,abs(a-c));assert abs(a-c)<1e-5,(field,x,y)
        for field in ('hp','active','defeated','pocketed','boss','armor','break_shots_remaining','break_started_this_shot','pocket_resolved_this_shot'):
            if returned and field in ('active','pocketed'):continue
            assert x[field]==y[field],(field,x,y)
    return dict(matched=True,max_error=error)


def fire(folder,process,state,index,action,**arguments):
    shot=folder/f'shot_{index:02d}';shot.mkdir()
    b.h.write(shot/'before.json',state)
    previous=state['sequence']
    b.h.command(folder,process,action,**arguments)
    def finished():
        s=b.h.read(folder/'runtime/game_mcp/game_state.json')
        return s if s.get('sequence',0)>previous and (s.get('game_state') in ('aiming_direction','clear_reward','game_over') or s.get('scene')=='result') else None
    final=b.h.wait_for(finished,process,90)
    b.h.write(shot/'final.json',final)
    for kind in ('expected','actual'):
        shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',shot/f'{kind}.json')
    b.h.write(shot/'verification.json',verify(shot))
    return final


def trial(label,build):
    with b.game(label,build,build,expected_break_balls=0) as (folder,process,state):
        assert sum(e.get('boss_minion',False) for e in state['enemies'])==3
        for index in range(1,41):
            b.h.command(folder,process,'evaluate_boss_shots')
            evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
            assert evaluation['model']=='boss_shared_ccd_v4_escorts'
            assert all(c['score_breakdown']['stop_shield']==0 for c in evaluation['choices'])
            selected=evaluation['recommended']
            b.h.write(folder/f'evaluation_{index:02d}.json',evaluation)
            state=fire(folder,process,state,index,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
            actual_loss=b.h.read(folder/f'shot_{index:02d}'/'before.json')['player']['current_hp']-state['player']['current_hp']
            metrics=selected['metrics']
            before_hp=b.h.read(folder/f'shot_{index:02d}'/'before.json')['player']['current_hp']
            expected_loss=min(before_hp,metrics['player_hp_loss']+metrics['expected_enemy_turn_damage'])
            assert abs(actual_loss-expected_loss)<1e-5,(actual_loss,metrics)
            boss=state.get('boss_state') or {}
            print(build,index,'player',state['player']['current_hp'],'boss',boss.get('hp',0),'armor',boss.get('armor',0),'break',boss.get('break_shots_remaining',0),flush=True)
            if state['scene']=='result' or state['game_state'] in ('clear_reward','game_over'):break
        else:raise AssertionError('No terminal boss result within 40 shots')
        raw=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        events=Counter(e['event_type'] for e in raw['events'])
        result=dict(build=build,shots=index,player_hp=state['player']['current_hp'],events=dict(events))
        b.h.write(folder/'summary.json',result)
        return result


def intent_cycle(label):
    def configure(folder):b.h.write(folder/'assets/data/player_deck.json',{'deck':['player_guard']*3})
    with b.game(label,'intent_cycle','standard',configure=configure,expected_break_balls=0) as (folder,process,state):
        intents=('normal_attack','prepare_strong_attack','strong_attack','summon_minions')
        for index in range(1,13):
            assert state['boss_state']['intent']==intents[(index-1)%4],state['boss_state']
            assigned=[e for e in state['enemies'] if e.get('boss_minion') and e['planned_action']=='normal_attack']
            assert len(assigned)==2
            hp=state['player']['current_hp']
            state=fire(folder,process,state,index,'fire_shot',direction_x=1 if index%2 else -1,direction_z=0,power=1)
            assert hp-state['player']['current_hp']==(1,0,3,0)[(index-1)%4],(index,hp,state['player']['current_hp'])
            assert sum(e.get('boss_minion',False) and not e['defeated'] for e in state['enemies'])<=4
            print('intent_cycle',index,'HP',state['player']['current_hp'],flush=True)
        raw=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        events=Counter(e['event_type'] for e in raw['events'])
        assert events['boss_minions_summoned']==1 # after the first summon the cap is full
        result=dict(build='intent_cycle',shots=12,events=dict(events))
        b.h.write(folder/'summary.json',result)
        return result


def strong_interruption(label):
    with b.game(label,'strong_interruption','standard',expected_break_balls=0) as (folder,process,state):
        for index in (1,2):
            state=fire(folder,process,state,index,'fire_shot',direction_x=1 if index==1 else -1,direction_z=0,power=1)
        assert state['boss_state']['intent']=='strong_attack'
        b.h.command(folder,process,'evaluate_boss_shots')
        evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        candidates=[c for c in evaluation['choices'] if c['metrics']['break_started']]
        assert candidates,'Expected an available Break interruption'
        selected=candidates[0]
        before_hp=state['player']['current_hp']
        state=fire(folder,process,state,3,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
        assert state['boss_state']['is_broken'] and state['boss_state']['break_shots_remaining']==2
        assert before_hp-state['player']['current_hp']==selected['metrics']['player_hp_loss']+selected['metrics']['expected_enemy_turn_damage']
        raw=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        assert any(e['event_type']=='boss_attack_interrupted' and e['details']['intent']=='strong_attack' for e in raw['events'])
        assert not any(e['source']=='boss_strong_attack' for stage in raw['stages'] for e in stage['damage_events'])
        return dict(build='strong_interruption',shots=3)


def resume_finished(label,build):
    folder=b.ROOT/label/build
    shots=sorted(folder.glob('shot_*'))
    assert shots and b.h.read(folder/'exe.json')['sha256']==hashlib.sha256(b.EXE.read_bytes()).hexdigest()
    for index,shot in enumerate(shots,1):
        assert shot.name==f'shot_{index:02d}'
        verify(shot)
        before=b.h.read(shot/'before.json');final=b.h.read(shot/'final.json')
        m=b.h.read(folder/f'evaluation_{index:02d}.json')['recommended']['metrics']
        hp=before['player']['current_hp']
        assert hp-final['player']['current_hp']==min(hp,m['player_hp_loss']+m['expected_enemy_turn_damage'])
    final=b.h.read(shots[-1]/'final.json')
    assert final['scene']=='result' or final['game_state'] in ('clear_reward','game_over'),'Cannot resume an unfinished fixture'
    raw=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
    assert raw['stages'][0]['stage_result']['result'] in ('clear','game_over')
    result=dict(build=build,shots=len(shots),player_hp=final['player']['current_hp'],events=dict(Counter(e['event_type'] for e in raw['events'])))
    b.h.write(folder/'summary.json',result)
    print('verified existing terminal fixture',build,result['shots'],flush=True)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);parser.add_argument('--build');parser.add_argument('--intent-cycle',action='store_true');parser.add_argument('--interrupt-strong',action='store_true');parser.add_argument('--resume-finished',action='store_true');args=parser.parse_args()
    results=[]
    if args.interrupt_strong:results.append(strong_interruption(args.label))
    elif args.intent_cycle:results.append(intent_cycle(args.label))
    else:
        for build in ([args.build] if args.build else ('standard','heavy','pierce','bounce','anchor')):
            results.append(resume_finished(args.label,build) if args.resume_finished and (b.ROOT/args.label/build).exists() else trial(args.label,build))
    b.h.write(b.ROOT/args.label/'verification_summary.json',dict(state='completed',exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest(),results=results,verified_shots=sum(r['shots'] for r in results)))
