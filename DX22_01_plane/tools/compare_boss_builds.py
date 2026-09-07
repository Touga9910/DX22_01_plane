"""Same-condition boss fixtures. No live/user save or server is touched."""
import argparse
import asyncio
import json
from pathlib import Path
import shutil
import sys
import time
import capture_boss_armor as b
from capture_boss_armor import h
sys.path.insert(0,str(h.PROJECT/'tools/game_mcp'))
from server import create_server
from bridge_store import GameBridgeStore
from shot_planner import load_player_profiles

def main(args):
    results=[]
    builds=[args.build] if args.build else ['standard','heavy','pierce','bounce','anchor']
    for build in builds:
        case=f'{build}_level{args.level}_{args.controller}'
        def configure(folder):
            data=h.read(folder/'assets/data/player_status.json')
            for ball in data['balls']:
                if ball['id']==f'player_{build}':
                    for upgrade in ball['upgrades'][:args.level]: ball['status'].update(upgrade)
            h.write(folder/'assets/data/player_status.json',data)
            h.write(folder/'assets/data/player_deck.json',{'deck':[f'player_{build}']*5})
        with b.game(args.label,case,build,configure=configure) as (folder,process,state):
            store=GameBridgeStore(folder/'runtime/game_mcp',command_timeout_seconds=45)
            server=create_server(store,'127.0.0.1',8877,load_player_profiles(h.PROJECT/'tools/game_mcp/player_profiles.json'),'intermediate')
            def mcp_call(name,arguments):
                result=asyncio.run(server.call_tool(name,arguments))
                if isinstance(result,tuple): return result[1]['result']
                for content in result:
                    if hasattr(content,'text'): return json.loads(content.text)['result']
                raise AssertionError(result)
            total_fixed=total_direct=breaks=0; planning_ms=[]; types=[]
            status='shot_limit'
            for index in range(1,args.max_shots+1):
                if args.controller=='mcp': evaluation=mcp_call('evaluate_boss_shots',{})
                else:
                    result=store.submit_command('evaluate_boss_shots',{}); assert result['ok'],result
                    evaluation=result['evaluation']
                choice=evaluation['recommended']; assert choice is not None,evaluation
                planning_ms.append(evaluation['milliseconds']); types.append(choice['contact_kind'])
                if index==1:
                    bad=store.submit_command('fire_boss_shot',{'candidate_id':choice['candidate_id'],'state_key':'invalid'})
                    assert not bad['ok']
                    again=store.submit_command('evaluate_boss_shots',{})['evaluation']
                    assert again['state_key']==evaluation['state_key'] and again['recommended']==choice
                shot=folder/f'shot_{index:02d}';shot.mkdir();h.write(shot/'evaluation.json',evaluation);h.write(shot/'before.json',state)
                arguments=dict(candidate_id=choice['candidate_id'],state_key=evaluation['state_key'])
                result=mcp_call('fire_boss_shot',arguments) if args.controller=='mcp' else store.submit_command('fire_boss_shot',arguments)
                assert result['ok'],result
                frames=[]; sequence=-1
                def finished():
                    nonlocal sequence
                    s=h.read(folder/'runtime/game_mcp/game_state.json')
                    if not s or s['sequence']==sequence:return None
                    sequence=s['sequence'];frames.append(s)
                    return s if s['game_state'] in ('aiming_direction','clear_reward','game_over') or s['scene']=='result' else None
                state=h.wait_for(finished,process,60)
                h.write(shot/'frames.json',frames);h.write(shot/'final.json',state)
                for kind in ('expected','actual'):shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',shot/f'{kind}.json')
                verification=b.compare(shot)
                actual=h.read(shot/'actual.json'); endboss=next(x for x in actual['balls'] if x['boss']);endplayer=next(x for x in actual['balls'] if x['player'])
                before=h.read(shot/'before.json')
                assert before['boss_state']['hp']-endboss['hp']==choice['metrics']['direct_damage']+choice['metrics']['fixed_damage'],choice
                assert before['player']['current_hp']-endplayer['hp']==choice['metrics']['player_hp_loss'],choice
                h.write(shot/'verification.json',verification)
                total_fixed+=choice['metrics']['fixed_damage'];total_direct+=choice['metrics']['direct_damage'];breaks+=choice['metrics']['break_started']
                print(case,index,choice['contact_kind'],'boss',endboss['hp'],'armor',endboss['armor'],'hp',state['player']['current_hp'],'plan_ms',round(evaluation['milliseconds']),flush=True)
                if endboss['hp']==0:status='win';break
                if state['scene']=='result' or state['player']['current_hp']==0:status='loss';break
                if index>=8 and len(set(types[-8:]))==1 and sum(h.read(folder/f'shot_{j:02d}/evaluation.json')['recommended']['metrics']['direct_damage']+h.read(folder/f'shot_{j:02d}/evaluation.json')['recommended']['metrics']['fixed_damage'] for j in range(index-7,index+1))==0:
                    status='stalled';break
            summary=dict(build=build,upgrade_parameters_level=args.level,controller=args.controller,status=status,shots=index,
                final_hp=state['player']['current_hp'],boss_hp=endboss['hp'],direct_damage=total_direct,fixed_damage=total_fixed,breaks=breaks,
                fixed_damage_ratio=total_fixed/max(1,total_fixed+total_direct),planning_ms_mean=sum(planning_ms)/len(planning_ms),planning_ms_max=max(planning_ms),
                kinds={kind:types.count(kind) for kind in set(types)},fixture='HP50, boss60/Armor2, five identical balls, no relics, seed20260904',
                note='Upgrade parameters copied into fixture definitions. Deterministic single-condition result, not a population win rate.')
            h.write(folder/'summary.json',summary);results.append(summary)
        h.write(b.ROOT/args.label/'comparison.json',results)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);parser.add_argument('--build');parser.add_argument('--level',type=int,default=0);parser.add_argument('--controller',choices=['cpp_shared','mcp'],default='mcp');parser.add_argument('--max-shots',type=int,default=50)
    main(parser.parse_args())
