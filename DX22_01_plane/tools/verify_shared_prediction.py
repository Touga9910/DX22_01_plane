"""Compare a forecast with the real last physics tick (before turn effects)."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent / 'runtime_tests/fixed_timestep'

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def check(label):
    report = []
    for case in read(ROOT / label / 'summary.json')['results']:
        folder = ROOT / label / case['case'] / 'runtime'
        expected = read(folder / 'shot_prediction_expected.json')
        actual = read(folder / 'shot_prediction_actual.json')
        assert expected['complete'] and expected['world_unchanged'], (case['case'], expected)
        for name in ('player_enemy_contacts', 'enemy_enemy_contacts'):
            assert expected[name] == actual[name], (case['case'], name, expected[name], actual[name])
        if 'cushions' in expected:
            assert expected['cushions'] == actual['cushions'], (case['case'], 'cushions')
        eb = {b['id']: b for b in expected['balls']}
        ab = {b['id']: b for b in actual['balls']}
        assert eb.keys() == ab.keys()
        error = 0.0
        player_returned = False
        for key, ball in eb.items():
            returned = ball.get('player') and ball['pocketed'] and not ball['defeated'] and not ab[key]['pocketed']
            player_returned = player_returned or returned
            for name in ('position','velocity'):
                if returned: continue  # Random return position is outside the shot forecast.
                for a,b in zip(ball[name],ab[key][name]):
                    error = max(error, abs(a-b))
                    assert abs(a-b) <= 1e-5, (case['case'], ball['enemy_id'], name, ball[name], ab[key][name])
            for name in ('hp', 'active', 'defeated', 'pocketed'):
                if returned and name in ('active','pocketed'): continue
                assert ball[name] == ab[key][name], (case['case'], ball['enemy_id'], name, ball[name], ab[key][name])
        item = {'case':case['case'], 'matched':True, 'max_error':error,
                'random_player_respawn_position_not_simulated': player_returned,
                **{key:expected[key] for key in ('milliseconds','ticks','substeps','path_points','path_truncated','world_unchanged')}}
        report.append(item)
        print(label, item, flush=True)
    (ROOT/label/'prediction_comparison.json').write_text(json.dumps(report,indent=2), encoding='utf-8')

if __name__ == '__main__':
    for label in sys.argv[1:]: check(label)
