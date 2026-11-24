"""Bounded launch-world prediction following native CCD contact episodes.

Ports the order/overlap/slop contracts of ContinuousBallStepper and
BallPhysicsRules; does not reproduce spawned actors or all gameplay abilities.
No artificial post-contact nudges. Completion is not a safety guarantee.
"""
import math
from ctypes import c_float

def f(x): return c_float(x).value
def add(a,b): return (f(a[0]+b[0]),f(a[1]+b[1]))
def sub(a,b): return (f(a[0]-b[0]),f(a[1]-b[1]))
def mul(a,k): return (f(a[0]*k),f(a[1]*k))
def dot(a,b): return a[0]*b[0]+a[1]*b[1]
def fdot(a,b): return f(f(a[0]*b[0])+f(a[1]*b[1]))
def norm(a): return f(math.sqrt(max(0,fdot(a,a))))
def unit(a):
    size=max(1e-12,norm(a))
    return (f(a[0]/size),f(a[1]/size))
def xy(p): return (f(p.get('x',0)),f(p.get('z',0)))

def sphere_time(p,v,r,limit,trigger=False):
    d,s,a=dot(p,p),dot(v,v),dot(p,v)
    if d<=(r+.0001)**2:
        return 0. if trigger or d<max(0,r-.0001)**2 or a<-.00001*r else math.inf
    if s<=1e-20 or a>=0: return math.inf
    disc=a*a-s*(d-r*r)
    if disc<0:return math.inf
    t=(d-r*r)/(-a+math.sqrt(disc))
    return t if 0<=t<=limit else math.inf

def closest(p,a,b):
    edge=sub(b,a);den=dot(edge,edge)
    t=f(dot(sub(p,a),edge)/den) if den>1e-20 else 0.
    return add(a,mul(edge,max(0.,min(1.,t)))),t

def wall_time(p,v,r,wall,limit):
    a,b=wall
    end=(p[0]+v[0]*limit,p[1]+v[1]*limit)
    if any(max(p[k],end[k])+r+.0001<min(a[k],b[k]) or min(p[k],end[k])-r-.0001>max(a[k],b[k]) for k in (0,1)):return math.inf
    near,_=closest(p,a,b);off=sub(p,near);d=math.sqrt(dot(off,off))
    if d<r-.0001 or d<=r+.0001 and dot(off,v)<-.00001*r:return 0.
    earliest=min(sphere_time(sub(p,a),v,r,limit),sphere_time(sub(p,b),v,r,limit))
    edge=sub(b,a);size=math.sqrt(dot(edge,edge))
    if size<=1e-12:return earliest
    tangent=mul(edge,f(1/f(size)));n=(-tangent[1],tangent[0]);start=sub(p,a)
    distance,speed=dot(start,n),dot(v,n)
    if abs(speed)>1e-12:
        for side in (-1.,1.):
            if speed*side>=-.00001:continue
            t=(side*r-distance)/speed
            if 0<=t<=limit and 0<=dot(start,tangent)+dot(v,tangent)*t<=size:earliest=min(earliest,t)
    return earliest

def project_launch(state,stats,position,velocity,*,on_contact=None,max_contacts=64,max_iterations=2048,pierce_remaining=0):
    def body(key,p,v,st,dead=False,hp=0,kind=''):
        return dict(id=key,p=p,v=v,radius=f(st.get('radius',2.4)),mass=f(st.get('mass',1)),
            friction=f(st.get('friction',.02)),restitution=f(st.get('restitution',.8)),
            attack=0 if dead else st.get('attack',1),dead=dead,active=True,hp=hp,kind=kind)
    cue=body('player',tuple(map(f,position)),tuple(map(f,velocity)),stats)
    bodies=[cue]+[body(e['target_id'],xy(e['position']),xy(e.get('velocity',{})),e,e.get('defeated',False),e.get('hp',0),e.get('enemy_id',''))
        for e in state.get('enemies',[]) if not e.get('pocketed') and e.get('position')]
    walls=[(xy(w['start']),xy(w['end'])) for w in state.get('table',{}).get('walls',[])]
    pockets=[(xy(p['position']),f(p.get('radius',3))) for p in state.get('table',{}).get('pockets',[])]
    interior=mul(tuple(map(f,map(sum,zip(*(p for w in walls for p in w))))),f(1/(2*len(walls)))) if walls else (0.,0.)
    touching,pierced=set(),set();contacts=[];pocketed=set();wall_events=[]
    distances={b['id']:0. for b in bodies};living_distances=dict(distances);wall_counts={b['id']:0 for b in bodies};post_wall={b['id']:0 for b in bodies}
    elapsed=0.;iterations=0;stop_count=0;cue_simulating=True;quiet_ticks=0;clearance=math.inf;pocket_time=None;complete=False
    def retire(b):
        nonlocal pocket_time
        pocketed.add(b['id']);b['active']=False;b['dead']=True;b['attack']=0;b['v']=(0.,0.)
        if b is cue:pocket_time=elapsed
    def environment(b,start):
        if not b['active']:return
        for center,r in pockets:
            if any(center[k]<min(start[k],b['p'][k])-b['radius']-r-.0001 or center[k]>max(start[k],b['p'][k])+b['radius']+r+.0001 for k in (0,1)):continue
            delta=sub(b['p'],start);den=dot(delta,delta)
            t=max(0.,min(1.,dot(sub(center,start),delta)/max(1e-20,den)))
            if dot(sub(center,add(start,mul(delta,t))),sub(center,add(start,mul(delta,t))))<=(b['radius']+r+.0001)**2:
                retire(b);return
        for a,z in walls:
            if any(b['p'][k]+b['radius']+.0001<min(a[k],z[k]) or b['p'][k]-b['radius']-.0001>max(a[k],z[k]) for k in (0,1)):continue
            near,t=closest(b['p'],a,z);off=sub(b['p'],near);d=norm(off)
            if d>b['radius']+.0001:continue
            edge=unit(sub(z,a));inward=(-edge[1],edge[0])
            if fdot(sub(interior,a),inward)<0:inward=mul(inward,-1.)
            n=inward if 0<t<1 else unit(off) if d>1e-6 else inward
            if d<b['radius'] or fdot(n,off)<0:b['p']=add(near,mul(n,b['radius']))
            speed=fdot(b['v'],n)
            if speed<-.00001:
                b['v']=mul(sub(b['v'],mul(n,f(2*speed))),b['restitution']);wall_counts[b['id']]+=1
                if b is cue:wall_events.append(dict(time=elapsed,position=list(b['p']),velocity=list(b['v'])))
    def resolve():
        nonlocal pierce_remaining
        for b in bodies:environment(b,b['p'])
        for i,a in enumerate(bodies):
            for b in bodies[i+1:]:
                if not a['active']:break
                if not b['active']:continue
                key=frozenset((a['id'],b['id']));offset=sub(b['p'],a['p']);d=norm(offset);sep=f(a['radius']+b['radius'])
                if d>f(sep+.001):touching.discard(key);pierced.discard(key)
                if d>f(sep+.0001) or key in pierced:continue
                # SimpleMath operator/= scales by a float reciprocal; Normalize divides.
                n=mul(offset,f(1/d)) if d>.0001 else unit(sub(a['v'],b['v'])) if norm(sub(a['v'],b['v']))>.01 else (1.,0.)
                player=a is cue or b is cue;other=b if a is cue else a
                pierce=player and not other['dead'] and stats.get('pierce') and pierce_remaining>0
                starts=a['p'],b['p'];ua,ub=fdot(a['v'],n),fdot(b['v'],n)
                if not pierce:
                    overlap=max(0.,f(sep-d))
                    correction=mul(mul(n,overlap),.5)
                    a['p']=sub(a['p'],correction);b['p']=add(b['p'],correction)
                if ua-ub<=.00001:continue
                new=key not in touching;touching.add(key)
                if pierce:
                    pierce_remaining-=1;pierced.add(key)
                    if stats.get('refractAfterPierce'):cue['v']=mul(unit(sub(other['p'],cue['p'])),norm(cue['v']))
                    cue['v']=mul(cue['v'],f(stats.get('pierceSpeedRetention',.75)))
                else:
                    restitution=cue['restitution'] if player else min(a['restitution'],b['restitution']);rel=f(ua-ub);total=f(a['mass']+b['mass'])
                    ta=f(stats.get('knockbackTransfer',1)) if a is cue and not b['dead'] else 1.
                    tb=f(stats.get('knockbackTransfer',1)) if b is cue and not a['dead'] else 1.
                    av=f(ua-f(f(f(f(2*b['mass'])/total)*rel)*restitution)*tb)
                    bv=f(ub+f(f(f(f(2*a['mass'])/total)*rel)*restitution)*ta)
                    a['v']=add(a['v'],mul(n,f(av-ua)));b['v']=add(b['v'],mul(n,f(bv-ub)))
                    if not new:
                        closing=fdot(sub(a['v'],b['v']),n)
                        if closing>0:
                            a['v']=sub(a['v'],mul(n,f(closing*b['mass']/total)));b['v']=add(b['v'],mul(n,f(closing*a['mass']/total)))
                    if player and stats.get('anchor'):cue['v']=(0.,0.)
                if new:
                    event=dict(first=a['id'],second=b['id'],first_position=list(a['p']),second_position=list(b['p']),pierced=bool(pierce),
                        player_contact=player,player_wall_hits=wall_counts['player'],time=elapsed,first_dead=a['dead'],second_dead=b['dead'],
                        first_attack=a['attack'],second_attack=b['attack'],living_enemy_pair=not player and not a['dead'] and not b['dead'])
                    contacts.append(event)
                    for k in (a['id'],b['id']):post_wall[k]+=bool(wall_counts[k])
                    for k in (on_contact(event) or []) if on_contact else []:
                        for body_ in bodies:
                            if body_['id']==k:body_['dead']=True;body_['attack']=0
                    for body_ in bodies:
                        if body_['id'] in event.get('hp_after',{}):body_['hp']=event['hp_after'][body_['id']]
                environment(a,starts[0]);environment(b,starts[1])
    for tick in range(512):
        if tick:
            quiet_ticks=quiet_ticks+1 if all(not b['active'] or fdot(b['v'],b['v'])<.03 for b in bodies) else 0
            if quiet_ticks>10:complete=True;break
        # Native world rebuilds its canonical spatial order each fixed tick.
        bodies.sort(key=lambda b:(b is not cue,b['p'][0],b['p'][1],b['v'][0],b['v'][1],b['radius'],b['mass'],b['restitution'],b['friction'],b['hp'],b['attack'],b['kind']))
        for b in bodies:
            if not b['active']:continue
            speed=norm(b['v'])
            if b is cue:
                # MCP uses FireAutomatedShot: skipSimulationOnNextFixedUpdate=false.
                # The logged launch velocity precedes this first friction update.
                if any('player' in pair and norm(sub(cue['p'],next(x['p'] for x in bodies if x['id'] in pair and x is not cue)))<=cue['radius']+next(x['radius'] for x in bodies if x['id'] in pair and x is not cue)+.001 for pair in pierced):
                    b['v']=mul(unit(b['v']),max(.2,f(speed-b['friction'])));stop_count=0;cue_simulating=True
                elif not cue_simulating:
                    continue  # Native Idle cue is moved by late contacts without re-entering friction simulation.
                elif fdot(b['v'],b['v'])<float(stats.get('anchorStopSpeedSquared',.03) if stats.get('anchor') else .03):
                    stop_count+=1
                    if stop_count>10:b['v']=(0.,0.);cue_simulating=False
                else:
                    stop_count=0;decel=f(min(f(b['friction']*stats.get('anchorBrakeMultiplier',1) if stats.get('anchor') else b['friction']),speed))
                    b['v']=add(b['v'],mul(unit(b['v']),-decel))
            elif fdot(b['v'],b['v'])>.001:
                b['v']=add(b['v'],mul(unit(b['v']),-b['friction'])) if fdot(b['v'],b['v'])>=.03 else (0.,0.)
        resolve();remaining=1.
        while remaining>1e-9 and iterations<max_iterations and len(contacts)<max_contacts:
            earliest=remaining
            for i,a in enumerate(bodies):
                if not a['active']:continue
                for center,r in pockets:earliest=min(earliest,sphere_time(sub(a['p'],center),a['v'],a['radius']+r,remaining,True))
                for w in walls:earliest=min(earliest,wall_time(a['p'],a['v'],a['radius'],w,remaining))
                for b in bodies[i+1:]:
                    if b['active'] and frozenset((a['id'],b['id'])) not in pierced:
                        earliest=min(earliest,sphere_time(sub(a['p'],b['p']),sub(a['v'],b['v']),a['radius']+b['radius'],remaining))
            starts={b['id']:b['p'] for b in bodies}
            for b in bodies:
                if not b['active']:continue
                start=b['p'];b['p']=add(start,mul(b['v'],f(earliest)));distances[b['id']]+=norm(sub(b['p'],start))
                if b is not cue and not b['dead']:living_distances[b['id']]+=norm(sub(b['p'],start))
                if b is cue:
                    for center,r in pockets:
                        delta=sub(b['p'],start);t=max(0.,min(1.,dot(sub(center,start),delta)/max(1e-20,dot(delta,delta))))
                        clearance=min(clearance,norm(sub(center,add(start,mul(delta,t))))-b['radius']-r)
            elapsed+=earliest;remaining-=earliest;iterations+=1
            for b in bodies:environment(b,starts[b['id']])
            resolve()
        if iterations>=max_iterations or len(contacts)>=max_contacts:break
        if not cue['active'] and all(not b['active'] or norm(b['v'])<1e-9 for b in bodies):complete=True;break
    return dict(contacts=contacts,pocketed=sorted(pocketed),complete=complete,from_launch=True,friction_mode='fixed_tick',
        solver='native_contact_episode_float32',iterations=iterations,elapsed=elapsed,player_wall_hits=wall_counts['player'],
        distances=distances,living_enemy_distances=living_distances,wall_hits_by_body=wall_counts,post_wall_contacts_by_body=post_wall,
        minimum_player_pocket_clearance=clearance if math.isfinite(clearance) else None,player_pocket_time=pocket_time,
        velocities={b['id']:b['v'] for b in bodies},positions={b['id']:b['p'] for b in bodies},player_wall_events=wall_events,estimate_only=True)
