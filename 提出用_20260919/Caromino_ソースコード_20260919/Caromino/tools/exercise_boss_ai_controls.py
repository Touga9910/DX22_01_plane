import argparse
import asyncio
import ctypes
import json
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
    def configure(folder):
        kinds=['standard','heavy','anchor','pierce','bounce'] if args.mode=='mixed' else ['standard']*5
        h.write(folder/'assets/data/player_deck.json',{'deck':[f'player_{kind}' for kind in kinds]})
        config=h.read(folder/'assets/data/balance_autoplay.json');config['decision_delay_frames']=60
        h.write(folder/'assets/data/balance_autoplay.json',config)
    with b.game(args.label,args.mode,'standard',configure=configure) as (folder,process,state):
        store=GameBridgeStore(folder/'runtime/game_mcp',command_timeout_seconds=45)
        server=create_server(store,'127.0.0.1',8878,load_player_profiles(h.PROJECT/'tools/game_mcp/player_profiles.json'),'intermediate')
        def call(name,arguments):
            result=asyncio.run(server.call_tool(name,arguments))
            return result[1]['result'] if isinstance(result,tuple) else json.loads(result[0].text)['result']
        if args.mode=='cpp_auto':
            user=ctypes.windll.user32;window=h.window_for(process)
            user.GetForegroundWindow.restype=ctypes.wintypes.HWND;previous=user.GetForegroundWindow()
            user.ShowWindow(window[0],9);user.SetForegroundWindow(window[0]);time.sleep(0.3)
            try:
                assert user.GetForegroundWindow()==window[0]
                user.keybd_event(0x77,0,0,0);time.sleep(0.15);user.keybd_event(0x77,0,2,0)
            finally:user.ShowWindow(window[0],0);user.SetForegroundWindow(previous)
        elif args.mode=='legacy':
            h.command(folder,process,'select_ball',offer_index=1)
            evaluation=call('evaluate_boss_shots',{})
            fired=call('fire_shot',dict(target_id=state['boss_state']['target_id']))
            assert fired['ok'] and fired['shot_plan']['legacy_tool_compatibility'],fired
            desired=fired['shot_plan']['boss_evaluation']
            assert desired['offer_index']==1,desired
            h.write(folder/'evaluation.json',evaluation)
            h.write(folder/'fire_result.json',fired)
        else:
            first=call('get_game_state',{})['boss_shot_choices']
            assert first and first['offer_choices']
            target=next(i for i in range(len(state['offered_balls'])) if not state['offered_balls'][i]['selected'])
            h.command(folder,process,'select_ball',offer_index=target)
            changed=store.submit_command('fire_boss_shot',dict(candidate_id=first['recommended']['candidate_id'],state_key=first['state_key']))
            assert not changed['ok']
            evaluation=call('evaluate_boss_shots',{})
            desired=next(c for c in evaluation['offer_choices'] if c['offer_index']!=target)
            fired=call('fire_boss_shot',dict(candidate_id=desired['candidate_id'],state_key=evaluation['state_key']))
            assert fired['ok'],fired
            h.write(folder/'evaluation.json',evaluation)
        completed=0; previous='aiming_direction'
        deadline=time.monotonic()+240
        while time.monotonic()<deadline:
            s=h.read(folder/'runtime/game_mcp/game_state.json')
            phase=s.get('game_state')
            # The bridge may be between truncation and rewrite. An empty read
            # is not a transition out of BallsMoving.
            if not phase:
                time.sleep(0.02);continue
            if previous=='balls_moving' and phase!='balls_moving':
                completed+=1;shot=folder/f'shot_{completed:02d}';shot.mkdir()
                for kind in ('expected','actual'):shutil.copy2(folder/f'runtime/shot_prediction_{kind}.json',shot/f'{kind}.json')
                h.write(shot/'final.json',s);h.write(shot/'verification.json',b.compare(shot))
                print(args.mode,completed,s['player']['current_hp'],s.get('boss_state'),flush=True)
                if args.mode!='cpp_auto':
                    expected=h.read(shot/'expected.json');boss=next(x for x in expected['balls'] if x['boss'])
                    assert 60-boss['hp']==desired['metrics']['direct_damage']+desired['metrics']['fixed_damage']
                    assert expected['world_unchanged'];break
            if phase=='clear_reward' or s.get('scene')=='result':break
            previous=phase;time.sleep(0.02)
        assert completed>0
        if args.mode=='cpp_auto':
            last_actual=h.read(folder/f'shot_{completed:02d}/actual.json')
            assert phase=='clear_reward' and any(x['boss'] and x['defeated'] and x['hp']==0 for x in last_actual['balls'])
            h.write(folder/'verified.json',dict(shots=completed,win=True,actual_cpp_autoplay=True))
        elif args.mode=='mixed':h.write(folder/'verified.json',dict(stale_rejected=True,offer_override_matched=True,read_only_world_unchanged=True))
        else:h.write(folder/'verified.json',dict(legacy_tool_compatibility=True,identical_offer_index_preserved=True,physics_matched=True))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);parser.add_argument('--mode',choices=['mixed','cpp_auto','legacy'],required=True);main(parser.parse_args())
