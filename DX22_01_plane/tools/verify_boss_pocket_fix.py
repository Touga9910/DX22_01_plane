"""Isolated live regression for the former full-health one-shot pocket kill."""
import argparse
from collections import Counter
import capture_boss_armor as b


def main(label):
    def configure(folder):
        b.h.write(folder/'assets/data/player_deck.json', {'deck': ['player_guard']*3})
    with b.game(label, 'guard_opening_pocket', 'standard', boss=(0,26), configure=configure) as (folder, process, state):
        result = b.h.command(folder, process, 'evaluate_boss_shots')
        evaluation = b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        b.h.write(folder/'opening_evaluation.json', evaluation)
        assert evaluation['choices'] and not any(c['metrics']['boss_killed'] for c in evaluation['choices'])
        # Keep a reachable pocket so this checks the rule, not an enclosure
        # that would merely prevent the player from using the pocket at all.
        final = b.shot(folder, process, 1, (4, 26), 6)
        assert final['boss_state']['hp'] > 0 and final['boss_state']['armor'] <= 1
        assert final['scene'] == 'battle' and final['game_state'] == 'aiming_direction'
        log = b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        counts = Counter(e['event_type'] for e in log['events'])
        pocket = next(e['details'] for e in log['events'] if e['event_type']=='boss_pocket_resolved')
        assert pocket['armor_before'] == 2 and pocket['armor_after'] == 1 and pocket['damage'] == 0
        assert counts['boss_pocket_resolved'] == counts['boss_pocket_returned'] == 1
        assert counts['enemy_pocket_defeat'] == 0
        b.h.write(folder/'regression_summary.json', {
            'opening_pocket_survived': True, 'pocket_armor_after': 1, 'hp_after': final['boss_state']['hp'],
            'prediction_matches_live': True, 'one_pocket_effect': True,
        })
        print('Opening pocket: boss survives, pocket strips Armor 2 -> 1, prediction matches.', flush=True)
        # A neutral can complete the Break after the pocket in the same physical
        # chain. The next shot must now get the percentage reward, not a kill.
        assert final['boss_state']['is_broken']
        b.h.command(folder, process, 'evaluate_boss_shots')
        evaluation = b.h.read(folder/'runtime/game_mcp/last_result.json')['evaluation']
        choice = next(c for c in evaluation['choices'] if c['metrics']['pocket_damage']==15)
        v = choice['velocity']
        final = b.shot(folder, process, 2, (v['x'],v['z']), choice['power'])
        log = b.h.read(next((folder/'logs/balance').glob('run_*.json')))
        pocket_events = [e['details'] for e in log['events'] if e['event_type']=='boss_pocket_resolved']
        assert len(pocket_events)==2 and pocket_events[-1]['damage']==15
        assert final['boss_state']['hp']>0 and final['boss_state']['break_shots_remaining']==1
        print('Break pocket: 15 HP damage, no instant kill or Break refresh, prediction matches.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True)
    main(parser.parse_args().label)
