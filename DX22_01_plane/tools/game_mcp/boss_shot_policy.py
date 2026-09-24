"""Use the game's shared predictor for boss shots; never invent an arbitrary direction."""
from bridge_store import GameBridgeError


def evaluate_boss_choices(store, state):
    if not state.get('boss_state') or 'evaluate_boss_shots' not in state.get('available_actions', []):
        raise GameBridgeError('停止中の最終ボス戦で評価してください。')
    result = store.submit_command('evaluate_boss_shots', {})
    if not result.get('ok') or not isinstance(result.get('evaluation'), dict):
        raise GameBridgeError(result.get('message', 'ボス候補の評価に失敗しました。'))
    evaluation = result['evaluation']
    boss_target_id = state['boss_state'].get('target_id', 'boss')

    def public_choice(choice):
        if not isinstance(choice, dict):
            return choice
        mapped = dict(choice)
        if mapped.get('target_id') == 'boss':
            mapped['simulation_target_id'] = 'boss'
            mapped['target_id'] = boss_target_id
        elif mapped.get('target_id') == 'position':
            mapped['simulation_target_id'] = 'position'
            mapped['target_id'] = None
        return mapped

    return dict(
        evaluation,
        recommended=public_choice(evaluation.get('recommended')),
        choices=[public_choice(choice) for choice in evaluation.get('choices', [])],
        offer_choices=[public_choice(choice) for choice in evaluation.get('offer_choices', [])],
    )


def fire_boss_choice(store, state, candidate_id, state_key):
    if not state.get('boss_state') or 'fire_boss_shot' not in state.get('available_actions', []):
        raise GameBridgeError('停止中の最終ボス戦で実行してください。')
    if not isinstance(candidate_id, str) or not candidate_id or not isinstance(state_key, str) or not state_key:
        raise GameBridgeError('評価結果のcandidate_idとstate_keyを指定してください。')
    # C++ checks the current board/deck before selecting the offer and firing.
    return store.submit_command('fire_boss_shot', {'candidate_id': candidate_id, 'state_key': state_key})
