"""Audit core death against actual shot-end counts, independent of AI guesses."""


def audit_stage(stage, events, strict=False):
    shots=stage.get('shots',[])
    deaths=[e for e in events if e['event_type']=='enemy_defeated' and
        e.get('details',{}).get('enemy_id')=='enemy_boss_core']
    if not deaths:
        if strict and stage['stage_result']['result']=='clear':
            raise ValueError('Cleared boss stage has no core death telemetry')
        return dict(core_defeated=False,extra_shots=0,residual_battles=0,
                    post_kill_shot_damage=0,core_guard_shots=sum(s['ball_id']=='player_guard' for s in shots),
                    core_shots=len(shots),retirement_expected=0)
    if len(deaths)!=1 or not shots:raise ValueError('Ambiguous core death or missing shot records')
    death=deaths[0]['elapsed_from_run_start_ms']
    eligible=[i for i,s in enumerate(shots) if s['elapsed_from_run_start_ms']<=death]
    if not eligible:raise ValueError('Core death precedes the first shot')
    index=eligible[-1];kill=shots[index];extra=len(shots)-index-1
    residual=kill['enemies_alive_after']
    resolved=kill['elapsed_from_run_start_ms']+kill['duration_ms']
    damage=sum(e['damage'] for e in stage.get('damage_events',[]) if e['elapsed_from_run_start_ms']>resolved)
    decisions=[e for e in events if e['event_type']=='boss_ai_decision' and e['elapsed_from_run_start_ms']<=death]
    metrics=decisions[-1]['details']['boss_evaluation']['metrics'] if decisions else {}
    if strict:
        if extra or residual:raise ValueError('Post-core extra shot or surviving escort detected')
        if damage:raise ValueError('Player damaged after the core-killing shot ended')
        if stage['stage_result']['result']!='clear':raise ValueError('Core defeat did not end in stage victory')
        if not metrics.get('encounter_cleared') or metrics.get('expected_enemy_turn_damage')!=0:
            raise ValueError('AI completion prediction disagrees with core defeat')
    return dict(core_defeated=True,extra_shots=extra,residual_battles=int(residual>0),
        residual_enemies=residual,post_kill_shot_damage=damage,core_shots=index+1,
        core_guard_shots=sum(s['ball_id']=='player_guard' for s in shots[:index+1]),
        retirement_expected=metrics.get('minions_retired_after_shot',0))


def audit_run(raw,strict=False):
    return [audit_stage(s,[e for e in raw['events'] if e.get('stage_index')==s['stage_index']],strict)
            for s in raw['stages'] if s['stage_type']=='boss']
