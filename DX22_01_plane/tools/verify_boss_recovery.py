"""Real manual fire, shared predictor and AI checks around the recovery boundary."""
import argparse
import ctypes
import hashlib
import capture_boss_armor as b
import verify_boss_finish as finish
import verify_boss_escorts as escorts


def trial(label,mode):
    layout=dict(player_x=-25.54,player_z=-20,boss_z=20,minion_positions=[(0,-8),(0,4)]) if mode=='interrupt' else {}
    with finish.fixture(label,'recovery_'+mode,'guard',hp=60,break_shots=1,**layout) as (folder,process,state):
        initial_hp=state['player']['current_hp']
        # Complete the second full Break shot without hitting an enemy.
        state=escorts.fire(folder,process,state,1,'fire_shot',direction_x=1,direction_z=0,power=1)
        expected=b.h.read(folder/'shot_01/expected.json')
        assert expected['boss_recovery_strong_attack_announced']
        assert state['boss_state']['armor']==2 and state['boss_state']['break_shots_remaining']==0
        assert state['boss_state']['intent']=='strong_attack' and state['boss_state']['planned_damage']==4,state['boss_state']
        assert state['player']['current_hp']==initial_hp,'Recovery must not launch an immediate attack'
        b.h.command(folder,process,'evaluate_boss_shots')
        evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        if mode=='interrupt':
            choices=[c for c in evaluation['choices'] if c['metrics']['break_started']]
        else:
            choices=[c for c in evaluation['choices'] if not c['metrics']['break_started'] and
                not c['metrics']['encounter_cleared'] and c['metrics']['minion_boss_hits']==0 and
                c['metrics']['pocket_count']==0 and c['metrics']['direct_damage']==0 and
                c['metrics']['player_hp_loss']==0 and c['metrics']['expected_enemy_turn_damage']==3]
        b.h.write(folder/'evaluation.json',evaluation)
        assert choices,mode+' needs a suitable authored-rule candidate; see evaluation.json'
        selected=choices[0];b.h.write(folder/'selected.json',selected)
        hp=state['player']['current_hp']
        if mode=='manual':
            b.h.command(folder,process,'select_ball',offer_index=selected['offer_index'])
            v=selected['velocity']
            state=escorts.fire(folder,process,state,2,'fire_shot',direction_x=v['x'],direction_z=v['z'],power=selected['power'])
        else:
            state=escorts.fire(folder,process,state,2,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
        assert hp-state['player']['current_hp']==selected['metrics']['player_hp_loss']+selected['metrics']['expected_enemy_turn_damage']
        raw_path=next((folder/'logs/debug_battle').glob('run_*.json'))
        # Ongoing debug battles flush the final enemy phase on normal shutdown.
        window=b.h.window_for(process)
        ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
        assert process.wait(timeout=15)==0
        raw=b.h.read(raw_path)
        executed=[e for e in raw['events'] if e['event_type']=='boss_attack_executed']
        if mode=='interrupt':
            assert state['boss_state']['break_shots_remaining']==2 and state['boss_state']['is_broken']
            assert not executed
            assert any(e['event_type']=='boss_attack_interrupted' and e['details']['intent']=='strong_attack' for e in raw['events'])
        else:
            assert len(executed)==1 and executed[0]['details']['intent']=='strong_attack'
            assert executed[0]['details']['damage_before_shield']==4
            assert state['boss_state']['intent']=='summon_minions'
            assert hp-state['player']['current_hp']==3
        result=dict(mode=mode,recovery_shot_damage=initial_hp-hp,next_shot_hp_damage=hp-state['player']['current_hp'],
                    strong_execution_count=len(executed),prediction_matched=True)
        b.h.write(folder/'summary.json',result);print(result,flush=True)
        return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);args=parser.parse_args()
    results=[trial(args.label,mode) for mode in ('manual','ai','interrupt')]
    b.h.write(b.ROOT/args.label/'verification_summary.json',dict(state='completed',
        exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest(),verified_shots=6,results=results))

