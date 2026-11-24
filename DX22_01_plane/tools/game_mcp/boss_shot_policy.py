"""Use the game's shared predictor for boss shots; never invent an arbitrary direction."""
import json
from bridge_store import GameBridgeError

POLICY_MODEL = "boss_mcp_v23_safe_score_ties"


def rank_boss_choices(choices):
    """Preserve meaningful score differences; prefer safety within numerical ties."""
    def risk(choice):
        m = choice.get('metrics', {})
        immediate = max(0., float(m.get('player_hp_loss', 0)))
        incoming = max(0., float(m.get('expected_enemy_turn_damage', 0)))
        return (immediate + incoming, immediate, incoming)
    ordered = sorted(choices, key=lambda c: c.get('score', float('-inf')), reverse=True)
    ranked = []
    while ordered:
        best = float(ordered[0].get('score', float('-inf')))
        end = 1
        while end < len(ordered) and best - float(ordered[end].get('score', float('-inf'))) <= 1e-6:
            end += 1
        ranked.extend(sorted(ordered[:end], key=risk))
        ordered = ordered[end:]
    return ranked

def rescore_boss_choice(choice, state):
    """Re-rank published native trajectories; never alter their simulated outcome."""
    mapped = dict(choice)
    metrics = choice.get("metrics", {})
    parts = dict(choice.get("score_breakdown", {}))
    if not parts or not metrics:
        return mapped
    hp = float(state.get("player", {}).get("current_hp",50))
    urgency = 3. if hp <= 10 else 1.
    parts["safety"] = -(4. * float(metrics.get("player_hp_loss",0)) + 2. * float(metrics.get("expected_enemy_turn_damage",0))) * urgency
    improvement = float(metrics.get("opportunity_improvement",0))
    parts["setup"] = min(2., improvement*5.)
    parts["anchor_position"] = 0.
    progress = sum(float(metrics.get(k,0)) for k in ("direct_damage","fixed_damage","pocket_damage","armor_removed"))
    if not progress and not metrics.get("break_started") and not metrics.get("encounter_cleared") and improvement <= .05:
        parts["no_progress"] = -2.
    # Approximate the HP cost of leaving work for later turns. This is a
    # bounded one-board estimate, not another simulated enemy-turn forecast.
    # Published alternatives set the reachable damage scale; ball names do not.
    work = state.get('_boss_work_reference', {})
    hp_removed = sum(float(metrics.get(k,0)) for k in ('direct_damage','fixed_damage','pocket_damage'))
    if work and not metrics.get('encounter_cleared'):
        unavoidable = max(0.,work['hp']-work.get('best_damage',work['hp']))
        remaining = max(0.,work['hp']-hp_removed-unavoidable)
        turns = remaining/max(1.,work['damage_per_shot'])
        parts['future_turn_exposure'] = -min(6.,.5*work['attack_per_turn']*turns)
    else:
        parts['future_turn_exposure'] = 0.
    mapped.update(native_score=choice.get("score"), native_score_breakdown=choice.get("score_breakdown"),
                  score=sum(parts.values()), score_breakdown=parts, policy_model=POLICY_MODEL)
    return mapped



def evaluate_boss_choices(store, state):
    if not state.get('boss_state') or 'evaluate_boss_shots' not in state.get('available_actions', []):
        raise GameBridgeError('停止中の最終ボス戦で評価してください。')
    result = store.submit_command('evaluate_boss_shots', {})
    if not result.get('ok') or not isinstance(result.get('evaluation'), dict):
        raise GameBridgeError(result.get('message', 'ボス候補の評価に失敗しました。'))
    evaluation = result['evaluation']
    boss_target_id = state['boss_state'].get('target_id', 'boss')

    # Use only currently reachable published predictions, with a robust median
    # of each offer's best progress rather than a single lucky maximum.
    best_damage={}
    for c in evaluation.get('choices',[]):
        if c.get('score_breakdown',{}).get('fatal',0)<0:
            continue  # An immediately fatal trajectory is not a feasible tempo reference.
        m=c.get('metrics',{})
        d=sum(float(m.get(k,0)) for k in ('direct_damage','fixed_damage','pocket_damage'))
        best_damage[c.get('offer_index')]=max(best_damage.get(c.get('offer_index'),0),d)
    values=sorted(d for d in best_damage.values() if d>0)
    core=next((e for e in state.get('enemies',[]) if e.get('target_id')==boss_target_id),{})
    reference_state=state
    if values and core.get('hp',0)>0:
        escorts=sum(e.get('forecast_attack_damage',e.get('attack',0)) for e in state.get('enemies',[])
                    if e.get('boss_minion') and not e.get('defeated') and not e.get('pocketed'))
        reference_state={**state,'_boss_work_reference':dict(hp=core['hp'],best_damage=min(core['hp'],max(values)),damage_per_shot=values[len(values)//2],
                            attack_per_turn=max(0.,float(core.get('attack',0)))+min(2.,max(0.,escorts)))}

    def public_choice(choice):
        if not isinstance(choice, dict):
            return choice
        mapped = rescore_boss_choice(choice, reference_state)
        if mapped.get('target_id') == 'boss':
            mapped['simulation_target_id'] = 'boss'
            mapped['target_id'] = boss_target_id
        elif mapped.get('target_id') == 'position':
            mapped['simulation_target_id'] = 'position'
            mapped['target_id'] = None
        return mapped

    choices = [public_choice(choice) for choice in evaluation.get('choices', [])]
    choices = rank_boss_choices(choices)
    by_offer = {}
    for choice in choices:
        by_offer.setdefault(choice.get('offer_index'),choice)
    result = dict(evaluation, recommended=choices[0] if choices else public_choice(evaluation.get('recommended')),
                  choices=choices, offer_choices=list(by_offer.values()) if choices else
                  [public_choice(c) for c in evaluation.get('offer_choices', [])], policy_model=POLICY_MODEL,
                  remaining_work_reference=reference_state.get('_boss_work_reference'))
    store._boss_policy_evaluation = result
    return result



def fire_boss_choice(store, state, candidate_id, state_key):
    if not state.get('boss_state') or 'fire_boss_shot' not in state.get('available_actions', []):
        raise GameBridgeError('停止中の最終ボス戦で実行してください。')
    if not isinstance(candidate_id, str) or not candidate_id or not isinstance(state_key, str) or not state_key:
        raise GameBridgeError('評価結果のcandidate_idとstate_keyを指定してください。')
    # C++ checks the current board/deck before selecting the offer and firing.
    result = store.submit_command('fire_boss_shot', {'candidate_id': candidate_id, 'state_key': state_key})
    evaluation = getattr(store, '_boss_policy_evaluation', {})
    if result.get('ok') and evaluation.get('state_key') == state_key and hasattr(store,'bridge_directory'):
        audit = {'state_key':state_key, 'candidate_id':candidate_id, 'policy_model':POLICY_MODEL,
                 'remaining_work_reference':evaluation.get('remaining_work_reference'),
                 'player_hp':state.get('player',{}).get('current_hp'),
                 'selected':next((c for c in evaluation.get('choices',[]) if c.get('candidate_id')==candidate_id),None),
                 'choices':evaluation.get('choices',[]), 'command_result':result}
        with (store.bridge_directory/'boss_policy_audit.jsonl').open('a',encoding='utf-8') as stream:
            stream.write(json.dumps(audit,ensure_ascii=False)+'\n')
    return result
