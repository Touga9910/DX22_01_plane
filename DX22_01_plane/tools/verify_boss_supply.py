"""Isolated live shots: prepaid supply, shared predictions and unchanged AI scores."""
import argparse
import ctypes
import hashlib
import sys
from pathlib import Path
import capture_boss_armor as b
import verify_boss_escorts as escorts
sys.path.insert(0,str(Path(__file__).resolve().parent/'game_mcp'))
from boss_supply_audit import audit_supply_run


def trial(label,build,manual=False):
    def configure(folder):
        if manual:b.h.write(folder/'assets/data/player_deck.json',dict(deck=['player_guard']*3))
    case='manual_guard' if manual else build
    with b.game(label,case,build,configure=configure,expected_break_balls=0) as (folder,process,state):
        supplied=0
        for index in range(1,9 if manual else 61):
            b.h.command(folder,process,'evaluate_boss_shots')
            evaluation=b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
            selected=(next((c for c in evaluation['choices'] if c['metrics']['player_hp_loss']==0),None)
                      if manual else evaluation['recommended']);assert selected
            b.h.write(folder/f'evaluation_{index:02d}.json',evaluation)
            before=state;hp=state['player']['current_hp'];metrics=selected['metrics']
            if manual:
                b.h.command(folder,process,'select_ball',offer_index=selected['offer_index'])
                v=selected['velocity']
                state=escorts.fire(folder,process,state,index,'fire_shot',direction_x=v['x'],direction_z=v['z'],power=selected['power'])
            else:
                state=escorts.fire(folder,process,state,index,'fire_boss_shot',candidate_id=selected['candidate_id'],state_key=evaluation['state_key'])
            assert hp-state['player']['current_hp']==min(hp,metrics['player_hp_loss']+metrics['expected_enemy_turn_damage'])
            living=sum(e.get('boss_minion',False) and not e['defeated'] and not e.get('pocketed') for e in state['enemies'])
            assert living<=4
            terminal=state['scene']=='result' or state['game_state'] in ('clear_reward','game_over')
            if not terminal:
                boss=state['boss_state'];old=before['boss_state']
                if metrics['advance_supply_attempted_if_turn_continues']:
                    assert boss['advance_summon_consumed'] and boss['break_shots_remaining']==1
                    endpoint=b.h.read(folder/f'shot_{index:02d}/expected.json')
                    survivors=sum(x.get('boss_minion',False) and x['active'] and not x['defeated'] and not x['pocketed'] for x in endpoint['balls'])
                    assert living==survivors+metrics['advance_supply_requested_if_turn_continues'] and living>=2
                    supplied+=metrics['advance_supply_requested_if_turn_continues']
                if old['advance_summon_consumed'] and old['summon_cycle']==boss['summon_cycle']:
                    assert boss['advance_summon_consumed'],'Re-Break refunded an already prepaid summon'
            print(case,index,'HP',state['player']['current_hp'],'minions',living,'advance',metrics['advance_supply_requested_if_turn_continues'],flush=True)
            if terminal:break
        else:
            if not manual:raise AssertionError('No terminal result within 60 shots')
        # Flush the final phase without touching a normal save or another game.
        window=b.h.window_for(process)
        if window:ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
        assert process.wait(timeout=15)==0
        raw=b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        claims=[e['details'] for e in raw['events'] if e['event_type']=='boss_advance_supply']
        assert len({d['cycle'] for d in claims})==len(claims)
        assert all(d['break_shots_remaining']==1 and d['requested']==max(0,2-d['living_before']) for d in claims)
        audit=audit_supply_run(raw) if not manual else dict(advance_claims=len(claims),advance_requested=sum(d['requested'] for d in claims))
        result=dict(build=case,shots=index,prediction_matched=True,terminal=terminal,actual_supply_checks=audit,supplied=supplied)
        b.h.write(folder/'summary.json',result)
        return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--label',required=True);parser.add_argument('--manual-only',action='store_true');args=parser.parse_args()
    results=([trial(args.label,'standard',manual=True)] if args.manual_only else
             [trial(args.label,build) for build in ('standard','heavy','pierce','bounce','anchor')])
    b.h.write(b.ROOT/args.label/'verification_summary.json',dict(state='completed',
        exe_sha256=hashlib.sha256(b.EXE.read_bytes()).hexdigest(),verified_shots=sum(r['shots'] for r in results),results=results))
