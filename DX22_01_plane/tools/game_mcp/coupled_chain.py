"""Bounded moving-disc estimate after first contact, never game-state mutation.

Uses BallMechanics::ResolveNormalImpact's legacy 2*restitution response.
Friction is integrated in bounded time slices; special ability spawning and
the complete game solver are outside this model. Truncation is explicit.
"""
import math


def add(a, b): return (a[0] + b[0], a[1] + b[1])
def sub(a, b): return (a[0] - b[0], a[1] - b[1])
def mul(a, k): return (a[0] * k, a[1] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1]
def norm(a): return math.hypot(*a)
def unit(a): return mul(a, 1 / max(1e-12, norm(a)))
def xy(p): return (float(p.get("x", 0)), float(p.get("z", 0)))


def contact_time(p, v, radius, limit):
    speed2 = dot(v, v)
    closing = dot(p, v)
    if speed2 < 1e-12 or closing >= -1e-9:
        return None
    gap = dot(p, p) - radius * radius
    if gap <= 1e-7:
        return 0.0
    disc = closing * closing - speed2 * gap
    if disc < 0:
        return None
    t = (-closing - math.sqrt(disc)) / speed2
    return t if 0 <= t <= limit else None


def normal_impact(v1, v2, m1, m2, restitution, transfer1=1., transfer2=1.):
    relative = v1 - v2
    total = max(.01, m1 + m2)
    return (v1 - 2 * m2 / total * relative * restitution * transfer2,
            v2 + 2 * m1 / total * relative * restitution * transfer1)


def trigger_time(p, v, radius, limit):
    # Pocket triggers fire even when already overlapping and moving outwards.
    if dot(p, p) <= (radius + .0001) ** 2:
        return 0.
    return contact_time(p, v, radius + .0001, limit)


def project(state, stats, cue_position, cue_velocity, target_id, target_velocity,
            *, on_contact=None, max_contacts=24, max_iterations=512, pierce_remaining=0,
            from_launch=False):
    if from_launch:
        from launch_physics import project_launch
        return project_launch(state,stats,cue_position,cue_velocity,on_contact=on_contact,
            max_contacts=max_contacts,max_iterations=max_iterations,pierce_remaining=pierce_remaining)
    cue = {"id": "player", "p": tuple(cue_position), "v": tuple(cue_velocity),
           "radius": float(stats.get("radius", 2.4)), "mass": float(stats.get("mass", 2)),
           "friction": float(stats.get("friction", .02)) * (float(stats.get("anchorBrakeMultiplier", 1)) if stats.get("anchor") else 1),
           "restitution": float(stats.get("restitution", .8)), "attack": float(stats.get('attack',1)), "dead": False, "active": True}
    bodies = [cue]
    for enemy in state.get("enemies", []):
        if enemy.get("pocketed") or not enemy.get("position"):
            continue
        bodies.append({"id": enemy["target_id"], "p": xy(enemy["position"]),
                       "v": tuple(target_velocity) if enemy["target_id"] == target_id else xy(enemy.get("velocity", {})),
                       "radius": float(enemy.get("radius", 2.4)), "mass": max(.1, float(enemy.get("mass", 1))),
                       "friction": float(enemy.get("friction", .02)), "restitution": float(enemy.get("restitution", .8)),
                       "attack": 0. if enemy.get('defeated') else float(enemy.get('attack',1)),
                       "dead": bool(enemy.get("defeated")), "active": True})
    touching = set() if from_launch else {frozenset(("player", target_id))}
    by_id = {b['id']: b for b in bodies}
    pairs_by_body = [[(b, frozenset((a['id'], b['id']))) for b in bodies[i+1:]] for i,a in enumerate(bodies)]
    wall_geometry = []
    for wall in state.get('table', {}).get('walls', []):
        start, end = xy(wall['start']), xy(wall['end'])
        edge = sub(end, start)
        size = norm(edge)
        if size >= 1e-8:
            tangent = mul(edge, 1/size)
            wall_geometry.append((start, end, size, tangent, (-tangent[1], tangent[0])))
    pocket_geometry = [(p, xy(p['position']), float(p.get('radius',3))) for p in state.get('table', {}).get('pockets', [])]
    pocketed, contacts = set(), []
    distances = {b["id"]: 0. for b in bodies}
    wall_counts = {b["id"]: 0 for b in bodies}
    post_wall_contacts = {b["id"]: 0 for b in bodies}
    elapsed, walls_hit = 0., 0
    complete = False
    minimum_pocket_clearance = float('inf')
    player_pocket_time = None
    next_tick, stopped_ticks, player_stop_count = 0., 0, 0
    for iteration in range(max_iterations):
        if from_launch and elapsed >= next_tick - 1e-9:
            # Confirm the tick that has actually finished BEFORE applying the
            # next tick's friction. Counting after braking can retire a cue
            # one tick early when braking first crosses the stop threshold.
            if next_tick > 0:
                stopped_ticks = stopped_ticks+1 if all(not b['active'] or dot(b['v'],b['v'])<.03 for b in bodies) else 0
                if stopped_ticks > 10:
                    complete = True
                    break
            # PlayerFriction/EnemyFriction run once BEFORE a fixed tick, not
            # once per collision substep. A just-fired player skips tick 0.
            for b in bodies:
                if not b['active']: continue
                speed = norm(b['v'])
                if b['id']=='player':
                    if next_tick > 0 and speed >= math.sqrt(.03):
                        player_stop_count = 0
                        b['v'] = mul(b['v'], max(0., speed-b['friction'])/max(1e-12,speed))
                    elif next_tick > 0:
                        player_stop_count += 1
                        if player_stop_count > 10: b['v'] = (0.,0.)
                elif speed >= math.sqrt(.03):
                    b['v'] = mul(b['v'], max(0.,speed-b['friction'])/max(1e-12,speed))
                elif speed*speed > .001:
                    b['v'] = (0.,0.)
            next_tick += 1.
        moving = [b for b in bodies if b["active"] and norm(b["v"]) >= (1e-9 if from_launch else math.sqrt(.03))]
        if not moving:
            complete = True
            break
        for key in tuple(touching):
            first, second = tuple(key)
            a, b = by_id[first], by_id[second]
            if norm(sub(a['p'], b['p'])) > a['radius'] + b['radius'] + (.001 if from_launch else .02):
                touching.discard(key)
        limit = next_tick-elapsed if from_launch else min(1., min(norm(b["v"]) / max(.001, b["friction"]) for b in moving))
        event = None
        soonest = limit
        def offer(t, kind, a, b=None, normal=None):
            nonlocal event, soonest
            if t is not None and t <= soonest:
                soonest, event = max(0., t), (kind, a, b, normal)
        for i, a in enumerate(bodies):
            if not a["active"]:
                continue
            for b, key in pairs_by_body[i]:
                if not b["active"] or key in touching:
                    continue
                if a['v'] == (0., 0.) and b['v'] == (0., 0.):
                    continue
                offer(contact_time(sub(a["p"], b["p"]), sub(a["v"], b["v"]), a["radius"] + b["radius"], soonest), "pair", a, b)
            for pocket, center, pocket_radius in pocket_geometry:
                offer(trigger_time(sub(a["p"], center), a["v"], a["radius"] + pocket_radius, soonest), "pocket", a, pocket)
            if norm(a["v"]) < 1e-8:
                continue
            for start, end, size, tangent, base_normal in wall_geometry:
                normal = base_normal
                signed = dot(sub(a["p"], start), normal)
                if signed < 0: normal = mul(normal, -1)
                inward_speed = dot(a["v"], normal)
                if inward_speed < -1e-8:
                    t = max(0., (abs(signed) - a["radius"]) / -inward_speed)
                    along = dot(sub(add(a["p"], mul(a["v"], t)), start), tangent)
                    if 0 <= along <= size:
                        offer(t if t <= soonest else None, "wall", a, normal=normal)
                for end_point in (start, end):
                    t = contact_time(sub(a["p"], end_point), a["v"], a["radius"], soonest)
                    if t is not None:
                        offer(t, "wall", a, normal=unit(sub(add(a["p"], mul(a["v"], t)), end_point)))
        dt = max(soonest, 0. if from_launch else .0001)
        for b in bodies:
            if not b["active"]:
                continue
            previous_position = b["p"]
            b["p"] = add(b["p"], mul(b["v"], dt))
            if b["id"] == "player":
                delta = sub(b["p"], previous_position)
                for pocket, center, pocket_radius in pocket_geometry:
                    fraction = max(0., min(1., dot(sub(center, previous_position), delta) / max(1e-12, dot(delta, delta))))
                    clearance = norm(sub(center, add(previous_position, mul(delta, fraction)))) - b['radius'] - pocket_radius
                    minimum_pocket_clearance = min(minimum_pocket_clearance, clearance)
            speed = norm(b["v"])
            distances[b["id"]] += speed * dt
            if not from_launch:
                b["v"] = mul(b["v"], max(0, speed - b["friction"] * dt) / max(1e-12, speed))
                if norm(b["v"]) < math.sqrt(.03): b["v"] = (0., 0.)
        elapsed += dt
        if event is None:
            continue
        kind, a, b, normal = event
        if kind == "wall":
            wall_counts[a["id"]] += 1
            a["v"] = mul(sub(a["v"], mul(normal, 2 * dot(a["v"], normal))), a["restitution"])
            a["p"] = add(a["p"], mul(normal, .002))
            if a["id"] == "player": walls_hit += 1
        elif kind == "pocket":
            # Game::HandleEnemyPocket removes living enemies AND corpses.
            # Ordinary collision defeats remain physical until pocketed.
            pocketed.add(a["id"])
            if a['id'] == 'player': player_pocket_time = elapsed
            a["dead"] = True
            a["active"] = False
            a["v"] = (0., 0.)
        else:
            normal = unit(sub(b["p"], a["p"]))
            ua, ub = dot(a["v"], normal), dot(b["v"], normal)
            if ua - ub <= 1e-8:
                touching.add(frozenset((a["id"], b["id"])))
                continue
            player_contact = a["id"] == "player" or b["id"] == "player"
            other = b if a["id"] == "player" else a
            pierced = player_contact and pierce_remaining > 0 and not other["dead"]
            if pierced:
                pierce_remaining -= 1
                if stats.get("refractAfterPierce"):
                    cue["v"] = mul(unit(sub(other["p"], cue["p"])), norm(cue["v"]))
                cue["v"] = mul(cue["v"], float(stats.get("pierceSpeedRetention", .75)))
            else:
                restitution = cue["restitution"] if player_contact else min(a["restitution"], b["restitution"])
                va, vb = normal_impact(ua, ub, a["mass"], b["mass"], restitution,
                                       float(stats.get("knockbackTransfer", 1)) if a["id"] == "player" and not b['dead'] else 1,
                                       float(stats.get("knockbackTransfer", 1)) if b["id"] == "player" and not a['dead'] else 1)
                a["v"] = add(a["v"], mul(normal, va - ua))
                b["v"] = add(b["v"], mul(normal, vb - ub))
                if player_contact and stats.get("anchor"): cue["v"] = (0., 0.)
                # Shared legacy low-restitution contacts are projected apart.
                closing = dot(sub(a["v"], b["v"]), normal)
                if closing > 0:
                    a["v"] = sub(a["v"], mul(normal, closing * b["mass"] / (a["mass"] + b["mass"])))
                    b["v"] = add(b["v"], mul(normal, closing * a["mass"] / (a["mass"] + b["mass"])))
                a["p"] = sub(a["p"], mul(normal, .001))
                b["p"] = add(b["p"], mul(normal, .001))
            record = {"first": a["id"], "second": b["id"], "first_position": list(a["p"]), "second_position": list(b["p"]),
                      "pierced": pierced, "player_contact": player_contact, "player_wall_hits": walls_hit, "time": elapsed,
                      "first_dead":a['dead'],"second_dead":b['dead'],
                      "first_attack":a['attack'],"second_attack":b['attack'],
                      "living_enemy_pair": not player_contact and not a['dead'] and not b['dead']}
            contacts.append(record)
            for body in (a, b):
                if wall_counts[body["id"]]: post_wall_contacts[body["id"]] += 1
            if on_contact:
                defeated = set(on_contact(record) or [])
                for body in bodies:
                    if body["id"] in defeated:
                        body["dead"] = True
                        body['attack'] = 0.
            touching.add(frozenset((a["id"], b["id"])))
            if len(contacts) >= max_contacts:
                break
    return {"contacts": contacts, "pocketed": sorted(pocketed), "complete": complete,
            "from_launch": from_launch, "friction_mode": "fixed_tick" if from_launch else "continuous_estimate",
            "iterations": iteration + 1, "elapsed": elapsed, "player_wall_hits": walls_hit,
            "distances": distances, "wall_hits_by_body": wall_counts, "post_wall_contacts_by_body": post_wall_contacts,
            "minimum_player_pocket_clearance": minimum_pocket_clearance if math.isfinite(minimum_pocket_clearance) else None,
            "player_pocket_time": player_pocket_time,
            "velocities": {b['id']: b['v'] for b in bodies},
            "positions": {b["id"]: b["p"] for b in bodies}, "estimate_only": True}
