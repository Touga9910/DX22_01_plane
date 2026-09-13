import argparse
import copy
import json
import math
import subprocess
import capture_boss_armor as b
from capture_boss_armor import h

def vec(obj):
    p=obj['position']; return p['x'],p['z']
def sub(a,c): return a[0]-c[0],a[1]-c[1]
def unit(a):
    length=math.hypot(*a); return a[0]/length,a[1]/length
def point(n,d,scale): return n[0]+d[0]*scale,n[1]+d[1]*scale

def final_save(folder):
    # Derived from a generated, isolated checkpoint. User save files are never read.
    source=h.read(b.ROOT/'basic_v1/anchor/saves/run_save.json')
    payload=source['payload']
    for instance_id in (4,5):
        ball=copy.deepcopy(payload['deck']['draw_pile'][0]); ball['instance_id']=instance_id
        payload['deck']['draw_pile'].append(ball)
    payload['deck']['next_instance_id']=6
    payload['run'].update(progress=15,area_progress=15,run_phase='final_boss',selected_stage_id='boss_001',current_hp=50)
    payload['run_statistics'].update(area_progress=15,reached_floor=15)
    payload['resume_scene']='battle'
    payload['run_map']=dict(version=1,seed=payload['random']['route_selection_seed'],start_area=15,area_count=0,path=[0],active_node_id=1,legacy_entry_type=-1)
    # An explicit high-attack fixture tests state transitions/victory, not balance.
    for name in ('draw_pile','discard_pile','offered_balls'):
        for ball in payload['deck'][name]: ball['status']['attack']=12
    raw=json.dumps(payload,sort_keys=True,ensure_ascii=False,separators=(',',':')).encode()
    checksum=14695981039346656037
    for value in raw: checksum=((checksum^value)*1099511628211)&((1<<64)-1)
    source['checksum']=f'{checksum:016x}'
    h.write(folder/'saves/run_save.json',source)
    subprocess.run([str(h.PROJECT/'tools/runtime_tests/test_boss_armor_runtime.exe'),
                    '--fixture-checksum', str(folder/'saves/run_save.json')],check=True)

def travel_power(distance):
    # Anchor movement on empty space, same per-tick friction and 11-tick stop rule.
    def travelled(power):
        pos=0; count=0; v=power
        for _ in range(1000):
            if v*v<0.03: count+=1
            else: count=0; v=max(0,v-0.06)
            if count>10: break
            pos+=v
        return pos
    lo,hi=0.2,3.0
    for _ in range(32):
        mid=(lo+hi)/2
        if travelled(mid)<distance: lo=mid
        else: hi=mid
    return (lo+hi)/2

def run(args):
    if args.case in ('all','neutral_enemy'):
        with b.game(args.label,'neutral_enemy','anchor',boss=(-40,-20),extra=[dict(enemy_id='enemy_tank',x=-8,z=20)]) as (folder,process,s):
            final=b.shot(folder,process,1,(-4,10),4)
            assert [e['hp'] for e in final['enemies']]==[e['hp'] for e in s['enemies']]
            actual=h.read(folder/'shot_01/actual.json')
            assert actual['player_enemy_contacts']==actual['enemy_enemy_contacts']==0
    if args.case in ('all','neutral_pocket'):
        with b.game(args.label,'neutral_pocket','anchor',boss=(-40,-20)) as (folder,process,s):
            n=vec(s['break_balls'][0]); goal=(0,36); d=unit(sub(goal,n))
            contact=point(n,d,-(s['player']['radius']+s['break_balls'][0]['radius']))
            b.shot(folder,process,1,contact,8)
            actual=h.read(folder/'shot_01/actual.json')
            assert any(x['break_ball'] and x['pocketed'] for x in actual['balls']),actual
    if args.case in ('all','full_boss'):
        with b.game(args.label,'full_boss','anchor',configure=final_save,resume=True) as (folder,process,s):
            index=0; observed_break=False; observed_end=False; previous_broken=False
            for index in range(1,31):
                boss=s['boss_state']; p=vec(s['player']); target=vec(s['enemies'][0])
                if boss['is_broken']:
                    observed_break=True
                    # The first cycle deliberately lets both guaranteed shots expire.
                    if not observed_end: direction=(0,-1); power=0.3
                    else: direction=sub(target,p); power=4
                else:
                    if previous_broken: observed_end=True
                    choices=[]
                    for ball in s['break_balls']:
                        if not ball['active']: continue
                        n=vec(ball); desired=unit(sub(target,n))
                        contact=point(n,desired,-(ball['radius']+s['player']['radius']))
                        direction=sub(contact,p)
                        u=unit(direction)
                        # Incoming ray must reach the near hemisphere of the contact circle.
                        good=u[0]*desired[0]+u[1]*desired[1]>0.25
                        setup=point(n,desired,-12)
                        choices.append((not good,math.hypot(*sub(setup,p)),direction,setup))
                    _,_,direction,setup=min(choices)
                    if min(choices)[0]:
                        direction=sub(setup,p); power=travel_power(math.hypot(*direction))
                    else: power=4
                previous_broken=boss['is_broken']
                s=b.shot(folder,process,index,direction,power)
                if s['scene']=='result': break
            assert observed_break and observed_end
            assert s['scene']=='result',s['boss_state']
            assert s['run_progress']['final_boss_defeated'],s
            h.write(folder/'scenario_verified.json',dict(shots=index,break_started=True,break_expired=True,final_boss_defeated=True))

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--label',required=True); parser.add_argument('--case',default='all'); run(parser.parse_args())
