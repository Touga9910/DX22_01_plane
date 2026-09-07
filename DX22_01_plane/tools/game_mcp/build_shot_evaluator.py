"""Bounded geometric shot estimates, not a second game simulation.

Compare reachable contact paths using the exposed ball mechanics. Predictions
are capped by enemy HP and annotated as estimates; execution adds human error.
"""
from __future__ import annotations

import math
from typing import Any

MODEL_VERSION = "build_geometry_v1"
DEFAULT_WEIGHTS = {
    "damage": 1.0, "kills": 4.0, "prevented_attack": 2.0,
    "pierce_followups": 0.5, "chain_collisions": 0.5,
    "bank_damage_gain": 0.5, "pocket_control": 1.0,
    "safety": 2.0, "stable_stop": 0.5, "precision": 1.0,
}


def pos(value):
    return float(value.get("x", 0)), float(value.get("z", 0))


def add(a, b):
    return a[0] + b[0], a[1] + b[1]


def sub(a, b):
    return a[0] - b[0], a[1] - b[1]


def mul(a, k):
    return a[0] * k, a[1] * k


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def length(a):
    return math.hypot(*a)


def unit(a):
    return mul(a, 1 / max(1e-9, length(a)))


def point(a):
    return {"x": round(a[0], 5), "z": round(a[1], 5)}


def relic_owned(state, index):
    return any(r.get("index") == index and r.get("owned")
               for r in state.get("relics", []))


def selected_ball(state):
    offers = state.get("offered_balls", [])
    selected = next((b for b in offers if b.get("selected")), None)
    return selected or state.get("player", {}).get("ball")


def ball_stats(state, ball):
    stats = dict(ball.get("status", {}))
    stats.update(stats.get("abilities", {}))
    effects = state.get("relic_effects", {})
    stats["attack"] = stats.get("attack", 1) + effects.get("all_ball_attack_bonus", 0)
    stats["defense"] = stats.get("defense", 0) + effects.get("all_ball_defense_bonus", 0)
    if stats.get("pierce") and ball.get("definition_id") == "player_pierce" and relic_owned(state, 7):
        stats["pierceMaxUses"] = stats.get("pierceMaxUses", 1) + 1
        stats["pierceSpeedRetention"] = 1.0
    return stats


def drag(stats):
    return max(0.001, float(stats.get("friction", 0.02)) * (
        float(stats.get("anchorBrakeMultiplier", 1.0)) if stats.get("anchor") else 1.0))


def stopping_distance(speed, stats):
    threshold = float(stats.get("anchorStopSpeedSquared", 0.03)) if stats.get("anchor") else 0.03
    return max(0.0, (speed * speed - threshold) / (2 * drag(stats)))


def remaining_speed(speed, distance, stats):
    return math.sqrt(max(0, speed * speed - 2 * drag(stats) * distance))


def ray_contact(start, direction, center, radius):
    offset = sub(center, start)
    along = dot(offset, direction)
    across2 = max(0, dot(offset, offset) - along * along)
    if along < 0 or across2 > radius * radius + 1e-7:
        return None
    return max(0, along - math.sqrt(max(0, radius * radius - across2)))


def first_contact(start, direction, distance, enemies, radius, excluded=()):
    hits = []
    for e in enemies:
        if e["target_id"] in excluded:
            continue
        t = ray_contact(start, direction, pos(e["position"]), radius + float(e.get("radius", 2.4)))
        if t is not None and t <= distance + 1e-6:
            hits.append((t, e))
    return min(hits, key=lambda h: h[0]) if hits else None


def path_risk(start, end, radius, pockets):
    delta = sub(end, start)
    risk = 0.0
    for p in pockets:
        center = pos(p["position"])
        fraction = max(0, min(1, dot(sub(center, start), delta) / max(1e-9, dot(delta, delta))))
        gap = length(sub(center, add(start, mul(delta, fraction))))
        trigger = radius + float(p.get("radius", 3))
        risk = max(risk, max(0, min(1, (trigger * 1.5 - gap) / max(0.1, trigger * 0.5))))
    return risk


def wall_contact(start, direction, walls):
    hits = []
    for index, wall in enumerate(walls):
        a, b = pos(wall["start"]), pos(wall["end"])
        edge = sub(b, a)
        cross = direction[0] * edge[1] - direction[1] * edge[0]
        if abs(cross) < 1e-8:
            continue
        v = sub(a, start)
        t = (v[0] * edge[1] - v[1] * edge[0]) / cross
        u = (v[0] * direction[1] - v[1] * direction[0]) / cross
        if t > 1e-4 and 0 <= u <= 1:
            normal = unit((-edge[1], edge[0]))
            hits.append((t, add(start, mul(direction, t)), normal, index))
    return min(hits, key=lambda h: h[0]) if hits else None


def collision_damage(raw, enemy, source):
    incoming = unit(sub(source, pos(enemy["position"])))
    if -incoming[1] >= 0.5:
        raw = max(1, math.ceil(raw * float(enemy.get("frontal_damage_multiplier", 1))))
    return max(1, raw - float(enemy.get("defense", 0)))


def candidate_paths(state, target, stats, shot_type, wall_index, shot_goal, pocket_index):
    # Reuse the finite-wall mirror geometry; import at call time to avoid cycles.
    from shot_planner import _plan_bank_shot
    start, center = pos(state["player"]["position"]), pos(target["position"])
    radius = float(stats.get("radius", 2.4)) + float(target.get("radius", 2.4))
    d = unit(sub(center, start))
    side = (-d[1], d[0])
    if shot_type in ("auto", "direct"):
        if shot_goal != "pocket":
            yield {"aim": center, "mode": "direct", "kind": "center"}
            for sign in (-1, 1):
                yield {"aim": add(center, mul(side, radius * 0.35 * sign)),
                       "mode": "direct", "kind": "offset"}
            if not stats.get("pierce"):
                for other in state.get("enemies", []):
                    if other["target_id"] == target["target_id"] or other.get("defeated") or other.get("pocketed"):
                        continue
                    push = unit(sub(pos(other["position"]), center))
                    aim = sub(center, mul(push, radius))
                    if dot(unit(sub(aim, start)), push) > 0.25:
                        yield {"aim": aim, "mode": "direct", "kind": "chain_contact"}
        if shot_goal != "damage" and not (stats.get("pierce") and stats.get("pierceMaxUses", 1) > 0):
            for i, pocket in enumerate(state.get("table", {}).get("pockets", [])):
                idx = int(pocket.get("index", i))
                if pocket_index >= 0 and pocket_index != idx:
                    continue
                push = unit(sub(pos(pocket["position"]), center))
                aim = sub(center, mul(push, radius))
                if dot(unit(sub(aim, start)), push) > 0.25:
                    yield {"aim": aim, "mode": "direct", "kind": "pocket",
                           "pocket": pocket, "pocket_index": idx}
    if shot_type in ("auto", "bank") and shot_goal != "pocket":
        for i, wall in enumerate(state.get("table", {}).get("walls", [])):
            if wall_index >= 0 and wall_index != i:
                continue
            bank = _plan_bank_shot(start, center, wall, i)
            if bank:
                yield {"aim": center, "mode": "bank", "kind": "bank",
                       "wall": bank["contact_point"], "wall_index": i}


def score_candidate(state, ball, target, stats, path, power, profile):
    start = pos(state["player"]["position"])
    enemies = [e for e in state.get("enemies", []) if not e.get("defeated") and not e.get("pocketed")]
    pockets = state.get("table", {}).get("pockets", [])
    walls = state.get("table", {}).get("walls", [])
    radius = float(stats.get("radius", 2.4))
    restitution = float(stats.get("restitution", 0.8))
    origin = start
    speed = power
    pre_risk = 0.0
    travelled = 0.0
    if path["mode"] == "bank":
        to_wall = unit(sub(path["wall"], start))
        distance = length(sub(path["wall"], start))
        if first_contact(start, to_wall, distance, enemies, radius):
            return None  # A supposed bank path cannot hit an enemy before the wall.
        first_wall = wall_contact(start, to_wall, walls)
        if first_wall and first_wall[0] < distance - 1e-3:
            return None
        if distance > stopping_distance(speed, stats):
            return None
        speed = remaining_speed(speed, distance, stats) * restitution
        pre_risk = path_risk(start, path["wall"], radius, pockets)
        origin = path["wall"]
        travelled = distance
    direction = unit(sub(path["aim"], origin))
    max_distance = stopping_distance(speed, stats)
    wall = wall_contact(origin, direction, walls)
    if wall:
        max_distance = min(max_distance, wall[0])
    hit = first_contact(origin, direction, max_distance, enemies, radius)
    if not hit or hit[1]["target_id"] != target["target_id"]:
        return None
    hit_distance, _ = hit
    contact = add(origin, mul(direction, hit_distance))
    speed = remaining_speed(speed, hit_distance, stats)
    normal = unit(sub(pos(target["position"]), contact))
    quality = max(0, dot(direction, normal))
    if quality < 0.25:
        return None
    pre_risk = max(pre_risk, path_risk(origin, contact, radius, pockets))
    travelled += hit_distance
    hp = {e["target_id"]: max(0, float(e.get("hp", e.get("max_hp", 1)))) for e in enemies}
    damage_by_id = {key: 0.0 for key in hp}
    contacts = 0

    def damage(enemy, raw, source):
        key = enemy["target_id"]
        amount = min(hp[key] - damage_by_id[key], collision_damage(raw, enemy, source))
        damage_by_id[key] += max(0, amount)

    attack = float(stats.get("attack", 1))
    definition = ball.get("definition_id", "")
    bank_gain = 0.0
    precision = definition == "player_standard" and relic_owned(state, 5) and path["mode"] == "direct" and power <= 4
    bonus = int(precision) + int(definition == "player_heavy" and relic_owned(state, 6))
    if definition == "player_bounce" and relic_owned(state, 8) and path["mode"] == "bank":
        bonus += 1
        bank_gain += 1
    damage(target, attack + bonus + (attack if path["mode"] == "bank" and relic_owned(state, 3) else 0), contact)
    if path["mode"] == "bank":
        spring_bonus = int(definition == "player_bounce" and relic_owned(state, 8))
        baseline_damage = min(hp[target["target_id"]], collision_damage(attack + bonus - spring_bonus, target, contact))
        bank_gain = max(0, damage_by_id[target["target_id"]] - baseline_damage)
    contacts += 1
    pierce_followups = 0
    chain_collisions = 0
    followup_hits = 0
    controlled = set()
    enemy_mass = max(0.1, float(target.get("mass", 1)))
    player_mass = max(0.1, float(stats.get("mass", 2)))
    incoming = mul(direction, speed)
    normal_speed = dot(incoming, normal)
    push_speed = 2 * player_mass / (player_mass + enemy_mass) * normal_speed * restitution * float(stats.get("knockbackTransfer", 1))
    outgoing = sub(incoming, mul(normal, 2 * enemy_mass / (player_mass + enemy_mass) * normal_speed * restitution))
    if stats.get("anchor"):
        # The game transfers the impulse first, then stops the anchor at contact.
        outgoing = (0.0, 0.0)
    hit_ids = {target["target_id"]}

    if stats.get("pierce") and int(stats.get("pierceMaxUses", 1)) > 0:
        # Each pierced contact consumes one use. After the last use one final
        # ordinary impact is possible, then stop the straight-line estimate.
        retention = max(0, min(1, float(stats.get("pierceSpeedRetention", 0.75))))
        speed *= retention
        cursor = add(contact, mul(direction, 0.001))
        last_contact = contact
        for use in range(int(stats.get("pierceMaxUses", 1))):
            reach = stopping_distance(speed, stats)
            limit = wall_contact(cursor, direction, walls)
            if limit:
                reach = min(reach, limit[0])
            next_hit = first_contact(cursor, direction, reach, enemies, radius, hit_ids)
            if not next_hit:
                break
            distance, other = next_hit
            last_contact = add(cursor, mul(direction, distance))
            impact_bonus = contacts if relic_owned(state, 2) else 0
            damage(other, attack + impact_bonus, last_contact)
            contacts += 1
            pierce_followups += 1
            hit_ids.add(other["target_id"])
            speed = remaining_speed(speed, distance, stats)
            if use < int(stats.get("pierceMaxUses", 1)) - 1:
                speed *= retention
            else:
                speed *= 0.5  # Conservative residual after the final ordinary impact.
            cursor = add(last_contact, mul(direction, 0.001))
        outgoing = mul(direction, speed)
        pre_risk = max(pre_risk, path_risk(contact, last_contact, radius, pockets))
        contact = last_contact
    else:
        # Estimate one pushed enemy collision, using actual mass/transfer and
        # each enemy's attack. Never credit an unlimited cascade.
        target_stats = {"friction": target.get("friction", 0.02)}
        push_reach = stopping_distance(push_speed, target_stats)
        center = pos(target["position"])
        push_wall = wall_contact(center, normal, walls)
        if push_wall:
            push_reach = min(push_reach, push_wall[0])
        secondary = first_contact(center, normal, push_reach, enemies, float(target.get("radius", 2.4)), hit_ids)
        if secondary:
            distance, other = secondary
            impact = add(center, mul(normal, distance))
            impact_bonus = contacts if relic_owned(state, 2) else 0
            damage(other, float(target.get("attack", 1)) + impact_bonus, impact)
            damage(target, float(other.get("attack", 1)) + impact_bonus, pos(other["position"]))
            chain_collisions = 1
            contacts += 1
        if path["kind"] == "pocket":
            pocket = path["pocket"]
            distance = length(sub(pos(pocket["position"]), center))
            alignment = dot(normal, unit(sub(pos(pocket["position"]), center)))
            if alignment < 0.85 or push_reach + float(pocket.get("radius", 3)) < distance:
                return None
            if secondary and secondary[0] < distance:
                return None  # Another enemy blocks this simple pocket estimate.
            ratio = float(target.get("pocket_damage_ratio", 0))
            pocket_damage = math.ceil(float(target.get("max_hp", hp[target["target_id"]])) * ratio)
            if target.get("pocket_finisher_eligible"):
                pocket_damage = hp[target["target_id"]]
            key = target["target_id"]
            damage_by_id[key] = min(hp[key], damage_by_id[key] + pocket_damage)
            controlled.add(key)

    post_speed = length(outgoing)
    post_direction = unit(outgoing)
    post_distance = stopping_distance(post_speed, stats)
    stop = add(contact, mul(post_direction, post_distance))
    first_post_wall = wall_contact(contact, post_direction, walls)
    initial_post_limit = min(post_distance, first_post_wall[0]) if first_post_wall else post_distance
    residual_hit = first_contact(contact, post_direction, initial_post_limit, enemies, radius, hit_ids) if post_speed > 0 else None
    if first_post_wall and first_post_wall[0] < post_distance:
        # Bound the stop estimate to one rebound; do not extrapolate through walls.
        distance, wall_point, wall_normal, _ = first_post_wall
        pre_risk = max(pre_risk, path_risk(contact, wall_point, radius, pockets))
        post_speed = remaining_speed(post_speed, distance, stats) * restitution
        post_direction = sub(post_direction, mul(wall_normal, 2 * dot(post_direction, wall_normal)))
        remaining = stopping_distance(post_speed, stats)
        second_wall = wall_contact(add(wall_point, mul(post_direction, 0.001)), post_direction, walls)
        if second_wall:
            remaining = min(remaining, second_wall[0])
        stop = add(wall_point, mul(post_direction, remaining))
        post_origin = wall_point
    else:
        post_origin = contact
        remaining = post_distance
    if not stats.get("pierce"):
        followup = residual_hit or (first_contact(post_origin, post_direction, remaining, enemies, radius, hit_ids) if post_speed > 0 else None)
        if followup:
            distance, other = followup
            source = add(contact, mul(unit(outgoing), distance)) if residual_hit else add(post_origin, mul(post_direction, distance))
            # Only one further contact is credited, discounted for moving targets.
            before = damage_by_id[other["target_id"]]
            damage(other, attack + (contacts if relic_owned(state, 2) else 0), source)
            damage_by_id[other["target_id"]] = before + (damage_by_id[other["target_id"]] - before) * 0.6
            followup_hits = 1
            contacts += 0.6
    risk = max(pre_risk, path_risk(post_origin, stop, radius, pockets))
    defense = float(stats.get("defense", 0))
    killed = {key for key in hp if hp[key] > 0 and damage_by_id[key] >= hp[key] - 1e-6}
    prevented = sum(max(1, float(e.get("attack", 1)) - defense) for e in enemies
                    if e["target_id"] in killed | controlled and e.get("can_attack_this_turn", True))
    incoming_damage = sum(max(1, float(e.get("attack", 1)) - defense) for e in enemies
                          if e["target_id"] not in killed | controlled and e.get("can_attack_this_turn", True))
    player = state["player"]
    current_hp = float(player.get("current_hp", 50))
    max_hp = max(1, float(player.get("max_hp", 50)))
    pocket_cost = state.get("pocket_rules", {}).get("player", {}).get("damage_amount", math.ceil(max_hp * 0.04))
    repair = min(max(0, max_hp - current_hp), 1.0) if relic_owned(state, 4) and contacts >= 3 else 0.0
    net_loss = incoming_damage + risk * pocket_cost - repair
    urgency = 1 + max(0, 0.5 - current_hp / max_hp) * 4
    stable = max(0, 1 - min(1, post_distance / 35)) * (1 - risk) if stats.get("anchor") else 0
    metrics = {
        "damage": sum(damage_by_id.values()), "kills": len(killed),
        "prevented_attack": prevented, "pierce_followups": pierce_followups,
        "chain_collisions": chain_collisions, "bank_damage_gain": min(bank_gain, damage_by_id[target["target_id"]]),
        "pocket_control": len(controlled - killed), "safety": -net_loss * urgency,
        "stable_stop": stable, "precision": quality,
    }
    weights = {**DEFAULT_WEIGHTS, **profile.get("build_policy", {}).get("shot_evaluation", {})}
    breakdown = {key: round(value * float(weights.get(key, 0)), 4) for key, value in metrics.items()}
    breakdown["power_cost"] = round(-0.12 * power, 4)
    breakdown["lethal_risk"] = -150.0 if net_loss >= current_hp else 0.0
    return {
        "model": MODEL_VERSION, "estimate_only": True,
        "offer_index": ball.get("index", -1), "instance_id": ball.get("instance_id"),
        "definition_id": definition, "target_id": target["target_id"],
        "shot_type": path["mode"], "shot_goal": "pocket" if path["kind"] == "pocket" else "damage",
        "contact_kind": path["kind"], "wall_index": path.get("wall_index", -1),
        "pocket_index": path.get("pocket_index", -1), "power": round(power, 3),
        "aim_point": point(path["aim"]),
        "wall_contact_point": point(path["wall"]) if "wall" in path else None,
        "score": round(sum(breakdown.values()), 4), "score_breakdown": breakdown,
        "metrics": {k: round(v, 4) for k, v in metrics.items()},
        "expected_damage_by_target": {k: round(v, 3) for k, v in damage_by_id.items() if v > 0},
        "expected_incoming_damage": round(incoming_damage, 3),
        "self_pocket_risk": round(risk, 3), "expected_repair": repair,
        "estimated_stop_position": point(stop), "followup_hits": followup_hits,
        "reason": f"{definition}:{path['kind']}:damage={metrics['damage']:.1f},pierce={pierce_followups},chain={chain_collisions},incoming={incoming_damage:.1f},pocket_risk={risk:.2f}",
    }


def evaluate_build_shots(state: dict[str, Any], profile: dict[str, Any], *,
                         ball=None, target_id="", shot_type="auto", wall_index=-1,
                         fixed_power=None, shot_goal="auto", pocket_index=-1):
    ball = ball or selected_ball(state)
    if not ball or not ball.get("status") or not state.get("player", {}).get("position"):
        return []
    if shot_type == "bank" and not profile.get("allow_bank_shots", False):
        return []
    if shot_type == "auto" and not profile.get("allow_bank_shots", False):
        shot_type = "direct"
    stats = ball_stats(state, ball)
    limits = profile.get("recommended_power", {})
    lo, hi = float(limits.get("min", 3)), float(limits.get("max", 7))
    safe_precision_power = 4.0 / (1 + float(profile.get("human_error", {}).get("power_ratio", 0)))
    powers = [float(fixed_power)] if fixed_power is not None else sorted({lo, hi, (lo + hi) / 2, max(lo, min(hi, 4.0)), max(lo, min(hi, safe_precision_power))})
    candidates = []
    for target in state.get("enemies", []):
        if target.get("defeated") or target.get("pocketed") or not target.get("position"):
            continue
        if target_id and target.get("target_id") != target_id:
            continue
        for path in candidate_paths(state, target, stats, shot_type, wall_index, shot_goal, pocket_index):
            for power in powers:
                scored = score_candidate(state, ball, target, stats, path, power, profile)
                if scored is not None:
                    candidates.append(scored)
    candidates.sort(key=lambda c: (-c["score"], c["power"], c["target_id"]))
    return candidates


def build_joint_shot_context(state, profile):
    choices = []
    offers = state.get("offered_balls", [])
    for ball in offers:
        candidates = evaluate_build_shots(state, profile, ball=ball)
        if candidates:
            choices.append(candidates[0])
    choices.sort(key=lambda c: c["score"], reverse=True)
    return {"model": MODEL_VERSION, "estimate_only": True,
            "recommended": choices[0] if choices else None, "choices": choices,
            "limitations": "Geometry estimates: at most one pushed-enemy collision and one residual hit; moving chains and multi-wall paths are not fully simulated."}
