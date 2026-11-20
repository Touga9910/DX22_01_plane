"""Isolated causal checks of Guard defense and boss candidate selection."""
import argparse
import hashlib
import json
import shutil
from collections import Counter, defaultdict
from pathlib import Path

import capture_boss_armor as b

SEEDS = (2026111700, 2026111704, 2026111709, 2026111714, 2026111719)
CONDITIONS = ('baseline', 'shield0_replay', 'shield1_replay', 'no_flat_bonus', 'restrict_guard')


def choose(evaluation, condition):
    choices = evaluation['choices']
    if condition == 'no_flat_bonus':
        bonuses = defaultdict(set)
        for choice in choices:
            bonuses[choice['offer_index']].add(choice['score_breakdown']['stop_shield'])
        assert all(len(values)==1 for values in bonuses.values()), 'Cannot infer exact reranking for aim-dependent shields'
        # Constant shift within an offer keeps its best candidate in this pool.
        return max(choices, key=lambda c:(c['score']-c['score_breakdown']['stop_shield'],
                                        -c['offer_index'], -int(c['candidate_id'].split(':')[-1])))
    if condition == 'restrict_guard':
        alternatives = [c for c in choices if c['definition_id']!='player_guard']
        return alternatives[0] if alternatives else evaluation['recommended']
    return evaluation['recommended']


def pose(state):
    enemies = state.get('enemies', [])
    boss = next((e for e in enemies if e['enemy_id']=='enemy_boss_core'), None)
    return dict(player=state['player']['position'], boss=boss['position'] if boss else None,
                hp=state.get('boss_state',{}).get('hp') if state.get('boss_state') else 0,
                armor=state.get('boss_state',{}).get('armor') if state.get('boss_state') else 0)


def assert_same_pose(left, right):
    assert left['hp']==right['hp'] and left['armor']==right['armor'], (left,right)
    for who in ('player','boss'):
        if left[who] is None or right[who] is None:
            assert left[who]==right[who]
            continue
        for axis in ('x','y','z'):
            assert abs(left[who][axis]-right[who][axis]) < 1e-5, (who,left,right)


def trial(label, seed, condition, baseline=None):
    case=f'{seed}_{condition}'
    shield=0 if condition=='shield0_replay' else 1 if condition=='shield1_replay' else 3
    def configure(folder):
        data=b.h.read(folder/'assets/data/player_status.json')
        data['currentHp']=50
        for ball in data['balls']:
            if ball['id']=='player_guard':
                ball['status']['shotShield']=shield
        b.h.write(folder/'assets/data/player_status.json',data)
        b.h.write(folder/'assets/data/player_deck.json',
                  {'deck':['player_standard']*6+['player_guard']*6+['player_heavy']})
    actions=[]
    with b.game(label,case,'standard',configure=configure,run_seed=seed) as (folder,process,state):
        assert state['player']['current_hp']==50
        for index in range(1,41):
            before_pose=pose(state)
            b.h.command(folder,process,'evaluate_boss_shots')
            evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
            if baseline is not None:
                original=baseline['actions'][index-1]
                assert_same_pose(before_pose,original['before_pose'])
                candidates=[c for c in evaluation['choices'] if c['candidate_id']==original['candidate_id']
                            and c['instance_id']==original['instance_id']]
                assert candidates, 'Fixed action is not available in the counterfactual candidate pool'
                choice=candidates[0]
                assert choice['definition_id']==original['definition_id'] and choice['velocity']==original['velocity']
            else:
                choice=choose(evaluation,condition)
            shot=folder/f'shot_{index:02d}'
            shot.mkdir()
            b.h.write(shot/'before.json',state)
            b.h.write(shot/'evaluation.json',evaluation)
            b.h.write(shot/'selected.json',choice)
            action={k:choice[k] for k in ('candidate_id','instance_id','definition_id','velocity')}
            action['before_pose']=before_pose
            actions.append(action)
            b.h.command(folder,process,'fire_boss_shot',candidate_id=choice['candidate_id'],state_key=evaluation['state_key'])
            previous=state['sequence']
            def finished():
                s=b.h.read(folder/'runtime/game_mcp/game_state.json')
                if s.get('sequence',0)<=previous:return None
                return s if s.get('game_state') in ('aiming_direction','clear_reward','game_over') or s.get('scene')=='result' else None
            state=b.h.wait_for(finished,process,60)
            b.h.write(shot/'final.json',state)
            for kind in ('expected','actual'):
                shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',shot/f'{kind}.json')
            check=b.compare(shot)
            b.h.write(shot/'verification.json',check)
            print(case,index,choice['definition_id'],'hp',state['player']['current_hp'],
                  'boss',pose(state)['hp'],flush=True)
            if state['game_state']=='clear_reward' or state['scene']=='result' or state['player']['current_hp']<=0:
                break
        else:
            raise AssertionError('Boss trial did not finish within 40 shots')
        log=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        stage=log['stages'][0]
        damage=Counter()
        for event in stage['damage_events']:
            damage[event['source']]+=event['damage']
        counts=Counter(a['definition_id'] for a in actions)
        result=dict(seed=seed,condition=condition,shield=shield,shots=len(actions),
                    won=stage['stage_result']['result']=='clear',damage_taken=stage['stage_result']['player_damage_taken'],
                    final_hp=state['player']['current_hp'],guard_shots=counts['player_guard'],
                    guard_share=counts['player_guard']/len(actions),ball_usage=dict(counts),
                    damage_by_source=dict(damage),actions=actions)
        if baseline is not None:
            assert len(actions)==len(baseline['actions'])
            assert result['won']==baseline['won']
        b.h.write(folder/'summary.json',result)
        print('COMPLETED',case,{k:result[k] for k in ('won','shots','damage_taken','guard_shots')},flush=True)
        return result


def main(args):
    root=b.ROOT/args.label
    results=[]
    for seed in (args.seed or SEEDS):
        baseline=trial(args.label,seed,'baseline')
        results.append(baseline)
        for condition in CONDITIONS[1:]:
            results.append(trial(args.label,seed,condition,
                                 baseline if condition.endswith('_replay') else None))
            b.h.write(root/'experiment_results.json',results)
    b.h.write(root/'experiment_status.json',dict(state='completed',trials=len(results),
              seeds=list(args.seed or SEEDS),exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest()))


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--label',required=True)
    parser.add_argument('--seed',type=int,action='append')
    main(parser.parse_args())
