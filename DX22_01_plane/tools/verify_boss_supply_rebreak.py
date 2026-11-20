"""Force a prepaid strong interruption without refunding the same summon cycle."""
import argparse
import ctypes
import hashlib
import capture_boss_armor as b
import verify_boss_finish as finish
import verify_boss_escorts as escorts


def run(label):
    with finish.fixture(label,'prepaid_rebreak','guard',hp=60,break_shots=2,
                        player_z=-20,boss_z=20,minion_positions=[(0,-8),(0,4)]) as (folder,process,state):
        state=escorts.fire(folder,process,state,1,'fire_shot',direction_x=1,direction_z=0,power=1)
        assert state['boss_state']['advance_summon_consumed'] and state['boss_state']['break_shots_remaining']==1
        state=escorts.fire(folder,process,state,2,'fire_shot',direction_x=-1,direction_z=0,power=1)
        assert state['boss_state']['intent']=='strong_attack' and state['boss_state']['summon_cycle']==0
        for index in (3,4):
            b.h.command(folder,process,'evaluate_boss_shots')
            evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
            choices=[c for c in evaluation['choices'] if c['metrics']['break_started']] if index==3 else [evaluation['recommended']]
            assert choices
            selected=choices[0];b.h.write(folder/f'evaluation_{index:02d}.json',evaluation)
            assert not selected['metrics']['advance_supply_attempted_if_turn_continues']
            hp=state['player']['current_hp']
            state=escorts.fire(folder,process,state,index,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
            assert hp-state['player']['current_hp']==selected['metrics']['player_hp_loss']+selected['metrics']['expected_enemy_turn_damage']
            assert state['boss_state']['summon_cycle']==0 and state['boss_state']['advance_summon_consumed']
            assert state['boss_state']['break_shots_remaining']==(2 if index==3 else 1)
        window=b.h.window_for(process);ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
        assert process.wait(timeout=15)==0
        raw=b.h.read(next((folder/'logs/debug_battle').glob('run_*.json')))
        claims=[e for e in raw['events'] if e['event_type']=='boss_advance_supply']
        assert len(claims)==1 and claims[0]['details']['cycle']==0 and claims[0]['details']['requested']==0
        assert any(e['event_type']=='boss_attack_interrupted' and e['details']['intent']=='strong_attack' for e in raw['events'])
        result=dict(state='completed',verified_shots=4,strong_interrupted=True,rebreak_refund=False,
                    same_cycle_claims=1,exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest())
        b.h.write(b.ROOT/label/'verification_summary.json',result);print(result)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);args=parser.parse_args();run(args.label)
