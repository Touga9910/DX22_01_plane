"""Exercise copied data fixtures without modifying the user's game data or saves."""
import argparse
import os
from types import SimpleNamespace
import capture_fixed_timestep as harness


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True)
    args = parser.parse_args()
    originals = harness.CASES[:5]
    harness.CASES = [(f'weak_{name}', ball, enemies, direction, 2)
                     for name, ball, enemies, direction, _ in originals]
    harness.CASES += [(f'upgraded_{name}', ball, enemies, direction, 4)
                      for name, ball, enemies, direction, _ in originals]
    harness.CASES += [
        ('fatal_pocket', 'standard', [(28,20),(45,-15),(-35,-18)], (0,1), 8),
        ('frontal_guard', 'standard', [(0,18),(0,28),(30,-15)], (0,1), 4),
        ('pocket_damage', 'heavy', [(0,29),(30,0),(-30,0)], (0,1), 6),
    ]
    original_prepare = harness.prepare

    def prepare(label, case, ball):
        folder = original_prepare(label, case, ball)
        player_path = folder / 'assets/data/player_status.json'
        player = harness.read(player_path)
        if case.startswith('upgraded_'):
            # Same physical parameters as upgrade level 2. This fixture tests the
            # solver, not the shop/upgrade UI, so the deck's upgrade label stays 0.
            for definition in player['balls']:
                if definition['id'] == f'player_{ball}':
                    for upgrade in definition['upgrades']:
                        definition['status'].update(upgrade)
        if case == 'fatal_pocket':
            player['currentHp'] = 1
        harness.write(player_path, player)
        if case in ('frontal_guard', 'pocket_damage'):
            enemy_path = folder / 'assets/data/enemy_data.json'
            enemy = harness.read(enemy_path)
            entries = enemy['enemies'] if isinstance(enemy, dict) else enemy
            for definition in entries:
                if definition['id'] != 'enemy_strong':
                    continue
                definition['status']['maxHp'] = 18 if case == 'pocket_damage' else 12
                definition['gimmick'] = {
                    'frontalDamageMultiplier': 0.5 if case == 'frontal_guard' else 1.0,
                    'pocketDamageRatio': 0.35 if case == 'pocket_damage' else 0.0,
                }
            harness.write(enemy_path, enemy)
        return folder

    harness.prepare = prepare
    os.environ['DX22_TEST_PREDICTION'] = '1'
    harness.capture(SimpleNamespace(label=args.label, exe=None, baseline=False,
        render_hz=30, case=None, pause=False, reverse_enemies=False, mixed_enemies=False))


if __name__ == '__main__':
    main()
