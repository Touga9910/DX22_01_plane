"""Count executed strong attacks including shielded hits; never count plans as hits."""


def audit_strong_stage(stage, events, version):
    windows = []
    strong = False  # The authored encounter opens with Normal.
    for event in events:
        name = event['event_type']
        if name == 'enemy_action_plan':
            core = next((a for a in event['details']['actions']
                         if a['enemy_id'] == 'enemy_boss_core'), None)
            strong = bool(core and core['action'] == 'normal_attack' and
                          core['predicted_damage_before_defense'] == 4)
        if name == 'boss_ai_decision':
            windows.append((strong, []))
        if windows:
            windows[-1][1].append(event)
    shots = stage['shots']
    if len(windows) != len(shots):
        raise ValueError('Strong attack audit requires every boss shot decision')
    result = dict(strong_announced=0, strong_interruptions=0, strong_executions=0,
                  strong_kill_cancellations=0, recovery_announcements=0)
    for index, (announced, window) in enumerate(windows):
        names = [e['event_type'] for e in window]
        interruptions = [e for e in window if e['event_type'] == 'boss_attack_interrupted'
                         and e['details']['intent'] == 'strong_attack']
        result['strong_interruptions'] += len(interruptions)
        result['recovery_announcements'] += names.count('boss_break_ended')
        if version in ('B0012','B0013'):
            metrics = window[0]['details']['boss_evaluation']['metrics']
            if bool(metrics['recovery_strong_attack_announced']) != ('boss_break_ended' in names):
                raise ValueError('Predicted recovery announcement disagrees with live recovery')
        killed = any(e['event_type'] == 'enemy_defeated' and
                     e['details'].get('enemy_id') == 'enemy_boss_core' for e in window)
        executed = announced and not interruptions and not killed and shots[index]['player_hp_after'] > 0
        explicit = [e for e in window if e['event_type'] == 'boss_attack_executed'
                    and e['details']['intent'] == 'strong_attack']
        if version in ('B0012','B0013'):
            if len(explicit) != int(executed) or any(e['details']['damage_before_shield'] != 4 for e in explicit):
                raise ValueError('Strong attack execution telemetry disagrees with shot outcome')
        elif executed:
            # B0011 has no execution event. Require an actual HP hit as proof
            # for each reconstructed execution; do not silently treat plans as hits.
            start = shots[index]['elapsed_from_run_start_ms'] + shots[index]['duration_ms']
            end = shots[index+1]['elapsed_from_run_start_ms'] if index+1 < len(shots) else float('inf')
            if not any(d['source'] == 'boss_strong_attack' and start <= d['elapsed_from_run_start_ms'] < end
                       for d in stage.get('damage_events', [])):
                raise ValueError('Baseline strong execution lacks actual hit evidence')
        result['strong_announced'] += int(announced)
        result['strong_executions'] += int(executed)
        result['strong_kill_cancellations'] += int(announced and not interruptions and killed)
    return result


def audit_strong_run(raw):
    totals = dict(strong_announced=0, strong_interruptions=0, strong_executions=0,
                  strong_kill_cancellations=0, recovery_announcements=0)
    for stage in raw['stages']:
        if stage['stage_type'] != 'boss':
            continue
        events = [e for e in raw['events'] if e.get('stage_index') == stage['stage_index']]
        for key, value in audit_strong_stage(stage, events, raw['balance_version']).items():
            totals[key] += value
    return totals
