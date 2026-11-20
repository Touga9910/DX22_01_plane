"""Audit a prepaid summon at the last Break input, including zero-size claims."""


def audit_supply_stage(stage, events):
    result=dict(advance_claims=0,advance_requested=0,advance_spawned=0,prepaid_summons_skipped=0)
    cycle=0;used=False;claimed=set();pending=None;regular_pending=0;windows=[]
    for event in events:
        name=event['event_type'];d=event.get('details',{})
        if name=='boss_ai_decision':windows.append([event,[]])
        if windows:windows[-1][1].append(event)
        if name=='boss_advance_supply':
            if d['cycle']!=cycle or cycle in claimed or used:
                raise ValueError('Duplicate advance supply within a strong-attack cycle')
            if d['break_shots_remaining']!=1 or d['requested']!=max(0,2-d['living_before']):
                raise ValueError('Supply timing or deficit is wrong')
            claimed.add(cycle);used=True;pending=d['requested']
            result['advance_claims']+=1;result['advance_requested']+=d['requested']
        if name=='boss_minions_summoned' and d.get('advance'):
            if pending is None or d['requested']!=pending or d['spawned']!=pending:
                raise ValueError('Advance spawn differs from the shared requested count')
            result['advance_spawned']+=d['spawned'];pending=None
        if name=='boss_minions_summoned' and not d.get('advance'):
            if not regular_pending or d['requested']!=regular_pending or d['spawned']!=regular_pending:
                raise ValueError('Unscheduled or duplicate regular summon')
            regular_pending=0
        if name=='boss_supply_cycle_completed':
            if d['cycle']!=cycle or bool(d['advance_consumed'])!=used:
                raise ValueError('Supply cycle state was reset prematurely')
            if used and d['requested']!=0:raise ValueError('Prepaid summon executed twice')
            result['prepaid_summons_skipped']+=int(used)
            regular_pending=d['requested']
            used=False;cycle+=1
    if pending not in (None,0):raise ValueError('Requested advance supply was never spawned')
    if regular_pending:raise ValueError('Requested regular summon was never spawned')
    if len(windows)!=len(stage['shots']):raise ValueError('Missing supply prediction telemetry')
    for i,(decision,window) in enumerate(windows):
        metrics=decision['details']['boss_evaluation']['metrics']
        expected=metrics['advance_supply_attempted_if_turn_continues']
        count=metrics['advance_supply_requested_if_turn_continues']
        actual=[e['details'] for e in window if e['event_type']=='boss_advance_supply']
        died=i==len(windows)-1 and stage['stage_result']['result']=='game_over'
        if len(actual)!=int(expected) and not (died and expected and not actual):
            raise ValueError('Supply prediction disagrees with actual turn transition')
        if actual and actual[0]['requested']!=count:raise ValueError('Predicted deficit differs from live supply')
    return result


def audit_supply_run(raw):
    total=dict(advance_claims=0,advance_requested=0,advance_spawned=0,prepaid_summons_skipped=0)
    for stage in raw['stages']:
        if stage['stage_type']!='boss':continue
        events=[e for e in raw['events'] if e.get('stage_index')==stage['stage_index']]
        for key,value in audit_supply_stage(stage,events).items():total[key]+=value
    return total
