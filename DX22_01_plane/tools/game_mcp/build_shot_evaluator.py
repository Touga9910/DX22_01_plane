"""Bounded geometric shot estimates, not a second game simulation.

Compare reachable contact paths using the exposed ball mechanics. Predictions
are capped by enemy HP and annotated as estimates; execution adds human error.
"""
from __future__ import annotations

import math
from typing import Any

MODEL_VERSION = "build_tactics_v4_p1_tactics"
DEFAULT_WEIGHTS = {
    "damage": 1.0, "kills": 4.0, "prevented_attack": 2.0,
    "pierce_followups": 0.5, "chain_collisions": 0.5,
    "bank_damage_gain": 0.5, "pocket_control": 1.0,
    "safety": 2.0, "stable_stop": 0.5, "precision": 1.0,
    "protection_release": 1.25, "uncertainty": 1.0,
    # P1-only benefits: these are damage-equivalent scores, not real damage.
    "resource_delta_value": 1.0, "finisher_value": 1.0,
    "next_shot_setup_value": 1.0, "waste_penalty": 1.0,
    "anchor_target_focus": 1.0, "anchor_followup_position": 1.0,
}


def tactical_ai_enabled(state):
    return bool(state.get("balance_validation", {}).get("tactical_ai", True))


def tactical_ai_profile(state):
    return str(state.get("balance_validation", {}).get(
        "tactical_ai_profile", "tactics_v1"))


def tactics(enemy):
    return enemy.get("tactics", {})


def board_ability(enemy):
    return tactics(enemy).get("board_ability", {})


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


# P1: the following helpers only read P0's game-state snapshot.  They never
# mutate the state, simulate game physics or affect the player's preview line.
_HEAVY_BALLS = {"player_heavy", "player_chain_impact", "player_collision_boost", "player_lightweight"}
_PIERCE_BALLS = {"player_pierce", "player_refractive_pierce", "player_trace_driver",
                 "player_pierce_finisher", "player_minion_finisher"}
_BOUNCE_BALLS = {"player_cushion_charge", "player_bounce", "player_ricochet_finisher",
                 "player_reflection_setup"}
_ANCHOR_BALLS = {"player_anchor", "player_stop_shield", "player_anchor_finisher",
                 "player_origin_anchor"}


def _resource_state(state):
    data = state.get("ball_synergies", {})
    return data if isinstance(data, dict) else {}


def _number(value, default=0.0):
    try:
        num = float(value)
        return num if math.isfinite(num) else float(default)
    except (ValueError, TypeError, OverflowError):
        return float(default)


def _whole(value, default=0):
    return max(0, int(_number(value, default)))


def _present_ball(state, ids):
    for collection in ("deck_balls", "offered_balls"):
        for ball in state.get(collection, []):
            if isinstance(ball, dict) and ball.get("definition_id") in ids:
                return True
    return False


def _anchor_stacks_by_target(state):
    synergies = _resource_state(state)
    rows = synergies.get("enemy_anchor_stacks", [])
    result = {}
    if isinstance(rows, list):
        for item in rows:
            if isinstance(item, dict) and item.get("target_id"):
                result[str(item["target_id"])] = _whole(item.get("stacks"))
    for enemy in state.get("enemies", []):
        key = enemy.get("target_id")
        if key and key not in result:
            result[key] = _whole(enemy.get("anchor_stacks"))
    return result


def _reflection_count(state):
    synergies = _resource_state(state)
    reflection = synergies.get("reflection", {})
    if isinstance(reflection, dict) and "shared_stack_count" in reflection:
        return _whole(reflection["shared_stack_count"])
    return _whole(state.get("table", {}).get("reflection_stacks", 0))


def _trace_vector(value):
    # P0 traces may expose {x,y,z} or arrays [x,y,z].
    if isinstance(value, dict):
        return _number(value.get("x")), _number(value.get("z"))
    if isinstance(value, (list, tuple)) and len(value) >= 3:
        return _number(value[0]), _number(value[2])
    return (0.0, 0.0)


def _minion_expectation(state, count_consumed, finisher, enemies):
    """Estimate *future* minion hits on existing traces (not guaranteed damage).

    Uses PierceTraceRules' 1/4/7 launch thresholds and launch count. A direct
    ray is an optimistic prefilter; the returned score is discounted because
    real moving targets, balls and collision resolution are not simulated.
    """
    traces = _resource_state(state).get("pierce_traces", [])
    if not isinstance(traces, list) or not traces or count_consumed <= 0:
        return 0.0, 0
    launches = min(3, len(traces), (count_consumed + 2) // 3)
    expected = 0.0
    predicted_hits = 0
    for idx in range(launches):
        trace = traces[-1-idx]
        if not isinstance(trace, dict):
            continue
        origin = _trace_vector(trace.get("start"))
        direction = unit(_trace_vector(trace.get("direction")))
        if length(direction) < 0.5:
            continue
        # 5 is the fixed minion launch speed; simulate only a conservative ray.
        first = first_contact(origin, direction, stopping_distance(5.0, {"friction": 0.02}),
                              enemies, 0.5, ())
        if not first:
            continue
        _, enemy = first
        damage_bonus = 1 + int(count_consumed >= idx * 3 + 2)
        damage_bonus += int(finisher and count_consumed >= 10 + idx * 2)
        raw = max(0.0, damage_bonus - _number(enemy.get("defense")))
        expected += min(_number(enemy.get("hp", 1)), raw) * 0.50
        predicted_hits += 1
    return expected, predicted_hits


def _future_discount(current_hp, max_hp, incoming_damage):
    # Setup only matters if the player is likely to survive this enemy turn.
    hp = max(1.0, current_hp)
    survivability = max(0.0, min(1.0, (hp - incoming_damage) / hp))
    health = max(0.25, min(1.0, current_hp / max_hp * 1.5))
    return 0.55 * survivability * health


def _pushed_enemy_trajectory(enemies, target, direction, speed, walls,
                             max_wall_bounces=3):
    """Bounded *enemy* motion estimate after the cue ball's first impact.

    Accumulate travel through at most three wall rebounds. Stop at the first
    enemy collision: predicting the resulting multi-ball dynamics here would be
    misleading. Only post-wall enemy contacts count as a pinball follow-up.
    This is not a physics simulator, and never creates guaranteed damage.
    """
    travel = 0.0
    rebounds = 0
    post_wall_contacts = 0
    target_center = pos(target["position"])
    moving = target_center
    velocity = max(0.0, _number(speed))
    heading = unit(direction)
    enemy_rules = {"friction": _number(target.get("friction", 0.02), 0.02)}
    # On each segment, the closest contact wins (wall, ball, or stop).
    for _ in range(max_wall_bounces + 1):
        if velocity <= 0.05 or length(heading) < 0.5:
            break
        reach = stopping_distance(velocity, enemy_rules)
        if reach < 0.001:
            break
        wall = wall_contact(moving, heading, walls)
        distance_to_wall = wall[0] if wall else math.inf
        first_enemy = first_contact(
            moving, heading, reach, enemies,
            _number(target.get("radius", 2.4), 2.4),
            (target["target_id"],),
        )
        distance_to_enemy = first_enemy[0] if first_enemy else math.inf
        segment = min(reach, distance_to_wall, distance_to_enemy)
        travel += max(0.0, segment)
        if distance_to_enemy <= min(reach, distance_to_wall) + 1e-6:
            post_wall_contacts = int(rebounds > 0)
            break
        if not wall or distance_to_wall >= reach - 1e-5:
            break
        if rebounds >= max_wall_bounces:
            break
        velocity = remaining_speed(velocity, distance_to_wall, enemy_rules)
        velocity *= max(0.0, min(1.0, _number(target.get("restitution", 0.8), 0.8)))
        wall_point, normal = wall[1], wall[2]
        heading = unit(sub(heading, mul(normal, 2.0 * dot(heading, normal))))
        # Advance a tiny amount from the wall to prevent immediately finding
        # the same surface again. The shift is not a credited displacement.
        moving = add(wall_point, mul(heading, 0.002))
        rebounds += 1
    return {"travel": travel, "wall_bounces": rebounds,
            "post_wall_contacts": post_wall_contacts}


def _valuable_stack_chain(state, axis, gained, current):
    """Only reward exceptionally useful setup when a real consumer is owned.

    Consuming a stack is valuable when the proposed gain crosses an activation
    threshold. This is a bonus *to future resource utility*, not damage.
    """
    if gained <= 0:
        return False
    consumers = {
        "heavy": ({"player_chain_impact", "player_collision_boost"}, (3, 5)),
        "pierce": ({"player_trace_driver", "player_minion_finisher"}, (1, 4, 7, 10)),
        "reflection": ({"player_bounce", "player_ricochet_finisher"}, (1, 6)),
        "anchor": ({"player_stop_shield", "player_anchor_finisher"}, (4, 5)),
    }
    owned, thresholds = consumers[axis]
    return _present_ball(state, owned) and any(
        current < threshold <= current + gained for threshold in thresholds
    )


def _line_of_sight_to_enemy(start, enemy, enemies, radius, walls, power):
    """Cheap next-turn direct-hit feasibility, not a future-turn guarantee."""
    center = pos(enemy["position"])
    distance = length(sub(center, start))
    if distance < 0.001:
        return False
    heading = unit(sub(center, start))
    max_reach = stopping_distance(max(0.0, power), {"friction": 0.02})
    if distance > max_reach + radius + _number(enemy.get("radius", 2.4), 2.4):
        return False
    wall = wall_contact(start, heading, walls)
    if wall and wall[0] < distance - radius - _number(enemy.get("radius", 2.4), 2.4):
        return False
    first = first_contact(start, heading, max_reach, enemies, radius)
    return bool(first and first[1]["target_id"] == enemy["target_id"])


def _anchor_target_policy(state, ball, enemies, target, damage_by_id,
                          stop, start, power, profile, quality, path):
    """Focus already marked enemies; otherwise prepare to reach the most marked.

    A target with positive enemy anchor stacks represents a prior application.
    Raw historical contacts without a remaining mark are not in the P0 state,
    so we do not fabricate a past target or introduce cross-turn Python state.
    """
    name = ball.get("definition_id", "")
    if name not in _ANCHOR_BALLS:
        return 0.0, 0.0, {}
    marks = _anchor_stacks_by_target(state)
    living = [e for e in enemies if _number(e.get("hp", 0)) > damage_by_id.get(e["target_id"], 0) + 1e-5]
    marked = [e for e in living if marks.get(e["target_id"], 0) > 0]
    if not marked:
        return 0.0, 0.0, {"preferred_marked_target_id": None}
    marked.sort(key=lambda e: (-marks[e["target_id"]], e["target_id"]))
    best = marked[0]
    target_marks = marks.get(target["target_id"], 0)
    # Truly reachable marks are preferred, not marks reached through a risky,
    # low-quality shot; score_candidate already rejects impossible paths.
    aim_quality = max(0.0, min(1.0, quality))
    focus = (min(2.4, 0.90 + min(10, target_marks) * 0.15)
             * max(0.25, aim_quality)
             * (0.8 if path["mode"] == "bank" else 1.0)) if target_marks else 0.0
    # Only add positional value when the best marked enemy is hard to hit now,
    # and the current chosen shot is hitting a different living enemy.
    if target["target_id"] == best["target_id"]:
        return focus, 0.0, {"preferred_marked_target_id": best["target_id"],
                            "target_existing_anchor_stacks": target_marks}
    limit = profile.get("recommended_power", {})
    next_power = _number(limit.get("max", 7.0), 7.0)
    player_radius = _number(ball.get("status", {}).get("radius", 2.4), 2.4)
    walls = state.get("table", {}).get("walls", [])
    reachable_now = _line_of_sight_to_enemy(
        start, best, enemies, player_radius, walls, next_power)
    reachable_next = _line_of_sight_to_enemy(
        stop, best, living, player_radius, walls, next_power)
    followup = 0.0
    if not reachable_now and reachable_next:
        before = length(sub(pos(best["position"]), start))
        after = length(sub(pos(best["position"]), stop))
        # Reachability is the priority, not minimizing distance alone.
        followup = (0.45 + 0.65 * min(1.0, max(0.0, before - after) / 20.0))
        # Avoid valuing a one-turn setup as highly as a confirmed kill.
        followup *= 0.7
    return focus, followup, {
        "preferred_marked_target_id": best["target_id"],
        "target_existing_anchor_stacks": target_marks,
        "best_marked_reachable_now": reachable_now,
        "best_marked_reachable_after_shot": reachable_next,
    }


def _special_score(state, ball, stats, enemies, target, path, hp,
                   damage_by_id, contacted_ids, pierce_followups,
                   pushed_distance, chain_collisions, incoming_damage,
                   reflection_hit, predicted_shield, pushed_motion):
    """Return bounded, damage-equivalent P1 tactical benefits and diagnostics.

    Confirmed ability damage is handled in score_candidate.damage(). The score
    here only rewards resource *options* and discounted uncertain effects.
    """
    sy = _resource_state(state)
    definition = ball.get("definition_id", "")
    heavy = _whole(sy.get("heavy_stacks"))
    pierce = _whole(sy.get("pierce_stacks"))
    reflection = _reflection_count(state)
    anchor_targets = _anchor_stacks_by_target(state)
    anchor_player = _whole(sy.get("player_anchor_stacks"))
    target_id = target["target_id"]
    current_hp = _number(state.get("player", {}).get("current_hp", 50), 50)
    max_hp = max(1, _number(state.get("player", {}).get("max_hp", 50), 50))
    future = _future_discount(current_hp, max_hp, incoming_damage)
    resource_gain = 0.0
    finisher = 0.0
    setup = 0.0
    waste = 0.0
    facts = {}
    reasons = []
    alive_touched = [key for key in contacted_ids
                     if hp.get(key, 0) - damage_by_id.get(key, 0) > 0.01]
    facts["exceptional_stack_setup"] = False

    if definition == "player_heavy" and stats.get("heavyDistanceStacks", False):
        divisor = max(0.1, _number(sy.get("heavy_stack_distance_per_stack", 40), 40))
        remaining = max(0, _whole(sy.get("heavy_stack_gain_limit_per_turn", 10), 10)
                        - _whole(sy.get("heavy_stacks_gained_this_turn")))
        # Heavy gains stacks from accumulated movement, including rebounds.
        # The estimate stops on enemy contact and never goes beyond 3 walls.
        travel = max(0.0, pushed_motion["travel"])
        generated = min(remaining, int(travel // divisor))
        facts["heavy_stacks_generated_estimate"] = generated
        facts["heavy_estimated_travel"] = round(travel, 3)
        facts["heavy_predicted_wall_bounces"] = pushed_motion["wall_bounces"]
        if generated:
            exceptional = _valuable_stack_chain(state, "heavy", generated, heavy)
            facts["exceptional_stack_setup"] |= exceptional
            has_spender = _present_ball(state, {"player_chain_impact", "player_collision_boost"})
            resource_gain += generated * (0.55 if has_spender else 0.18) * future
            if exceptional:
                resource_gain += 1.1 * future
            reasons.append("敵の移動距離（最大3反射予測）から重量スタック生成を狙う")

    if definition == "player_lightweight":
        count = len(alive_touched)
        facts["heavy_micro_stack_estimate"] = len(contacted_ids)
        resource_gain += len(contacted_ids) * (0.35 if _present_ball(state, {"player_chain_impact", "player_collision_boost"}) else 0.13) * future
        exceptional = _valuable_stack_chain(state, "heavy", len(contacted_ids), heavy)
        facts["exceptional_stack_setup"] |= exceptional
        if exceptional: resource_gain += 0.9 * future
        setup += count * min(1.0, max(0.0, _number(stats.get("lightweightKnockbackMultiplier", 1.5)) - 1)) * 0.8 * future
        if count: reasons.append("異なる敵への軽量化付与と次ショットの押し出しを準備する")

    if definition == "player_collision_boost":
        consume = _whole(stats.get("heavySetupStackConsume", 3), 3)
        if consume > 0 and heavy >= consume:
            has_pair = len(enemies) > 1
            has_heavy = _present_ball(state, _HEAVY_BALLS)
            setup += min(2.2, max(0, _number(stats.get("heavyEnemyCollisionMultiplier", 1.25)) - 1) * 4.0) * future if has_pair and has_heavy else 0.0
            waste -= consume * (0.14 if has_pair and has_heavy else 0.32)
            facts["heavy_setup_consumed"] = consume
            if has_pair and has_heavy: reasons.append("重量3を消費して次ショットの敵同士衝突を強化する")

    if definition in {"player_pierce", "player_refractive_pierce", "player_pierce_finisher", "player_minion_finisher"} and stats.get("pierce"):
        count = min(max(0, _whole(sy.get("pierce_stack_max", 15), 15) - pierce), len(contacted_ids))
        facts["pierce_stacks_generated_estimate"] = count
        if count:
            has_spender = _present_ball(state, {"player_trace_driver", "player_minion_finisher"})
            resource_gain += count * (0.45 if has_spender else 0.15) * future
            exceptional = _valuable_stack_chain(state, "pierce", count, pierce)
            facts["exceptional_stack_setup"] |= exceptional
            if exceptional: resource_gain += 1.0 * future
            if count > 1: reasons.append("複数貫通でスタックと射線記録を増やす")

    if definition in {"player_trace_driver", "player_minion_finisher"}:
        consume = min(pierce, (15 if definition == "player_minion_finisher"
                               else _whole(stats.get("pierceStackConsumeLimit", 3), 3)))
        predicted, launched = _minion_expectation(state, consume,
                                                  definition == "player_minion_finisher", enemies)
        facts["pierce_stacks_consumed"] = consume
        facts["minion_predicted_hits"] = launched
        finisher += predicted
        if consume and not launched:
            waste -= min(3.0, consume * 0.22)
        elif launched:
            reasons.append("既存トレースからのミニオン命中見込みを評価する")

    if definition == "player_cushion_charge" and reflection_hit:
        generated = max(0, _whole(stats.get("cushionStackGenerateAmount", 1)))
        facts["reflection_stacks_generated_estimate"] = generated
        if generated:
            resource_gain += generated * (0.45 if _present_ball(state, {"player_bounce", "player_ricochet_finisher"}) else 0.13) * future
            exceptional = _valuable_stack_chain(state, "reflection", generated, reflection)
            facts["exceptional_stack_setup"] |= exceptional
            if exceptional: resource_gain += 1.0 * future
            reasons.append("撃破・命中を維持しながら反射スタックを蓄積する")

    if definition == "player_bounce" and reflection_hit:
        consumed = min(1, reflection, _whole(stats.get("cushionConsumptionLimit", 1), 1))
        facts["reflection_stacks_consumed_estimate"] = consumed
        # Actual bonus damage was already applied to damage_by_id.
        if consumed: reasons.append("共有反射スタックを消費して強化命中する")

    if definition == "player_ricochet_finisher" and reflection >= _whole(stats.get("ricochetStackConsume", 6), 6):
        consume = _whole(stats.get("ricochetStackConsume", 6), 6)
        facts["pinball_activation_estimate"] = 1
        facts["reflection_stacks_consumed_estimate"] = consume
        # A wall hit alone has NO reward. Only a contact occurring after
        # a predicted wall rebound provides extra future pinball value.
        possible = pushed_motion["post_wall_contacts"]
        facts["pinball_wall_bounces_estimate"] = pushed_motion["wall_bounces"]
        facts["pinball_post_wall_contacts_estimate"] = possible
        finisher += possible * 2.0 * future
        if not possible:
            waste -= 0.5
        reasons.append("壁反射後に敵へ再衝突できるピンボール経路を評価する" if possible else
                       "反射スタック6でピンボールを起動するが再衝突は未確認")

    if definition == "player_reflection_setup":
        amount = _whole(stats.get("reflectionSetupAmount", 1), 1)
        if state.get("table", {}).get("walls") and _present_ball(state, _BOUNCE_BALLS - {definition}):
            setup += min(3, amount) * 0.52 * future
            reasons.append("次ショットの反射スタック生成を予約する")
        facts["reflection_setup_armed_estimate"] = amount

    if definition in {"player_anchor", "player_origin_anchor"} and target_id in alive_touched:
        if definition == "player_anchor":
            grant = (_whole(stats.get("anchorEnemyStackGenerate", 2), 2)
                     + anchor_player // max(1, _whole(stats.get("anchorPlayerStacksPerBonus", 4), 4)))
            grant += _whole(sy.get("anchor_next_grant_bonus"))
        else:
            grant = _whole(stats.get("anchorEnemyStackGenerate", 1), 1)
            grant += _whole(sy.get("anchor_next_grant_bonus"))
        grant = min(grant, max(0, _whole(stats.get("anchorStackMax", 99), 99) - anchor_targets.get(target_id, 0)))
        facts["anchor_enemy_stacks_generated_estimate"] = grant
        if grant:
            has_consumer = _present_ball(state, {"player_anchor_finisher", "player_stop_shield"})
            resource_gain += grant * (0.48 if has_consumer else 0.13) * future
            exceptional = _valuable_stack_chain(state, "anchor", grant,
                                                anchor_targets.get(target_id, 0))
            facts["exceptional_stack_setup"] |= exceptional
            if exceptional: resource_gain += 1.0 * future
            reasons.append("既接触対象を優先し、敵を倒さずにアンカー印を付与する")

    if definition == "player_stop_shield":
        consumed = min(anchor_targets.get(target_id, 0), _whole(stats.get("anchorEnemyConsumeLimit", 5), 5))
        facts["anchor_enemy_stacks_consumed_estimate"] = consumed
        facts["shield_gain_estimate"] = predicted_shield
        # Shield was already applied to incoming_damage and hence survival.
        if consumed and predicted_shield:
            reasons.append("敵印を消費してシールドを獲得する")

    if definition == "player_anchor_finisher":
        consumed = anchor_targets.get(target_id, 0)
        facts["anchor_enemy_stacks_consumed_estimate"] = consumed
        # The direct and AoE bonuses were already included in damage_by_id.
        if consumed: reasons.append("アンカー印を全回収して追加・範囲ダメージを狙う")

    if definition == "player_origin_anchor":
        setup += (0.45 + (0.35 if _whole(stats.get("nextAnchorGrantBonus")) and
                _present_ball(state, {"player_anchor", "player_origin_anchor"}) else 0)) * future
        reasons.append("始点復帰で次ショットの位置を維持する")

    # All long-term value is bounded by the same unit as immediate damage.
    return ({
        "resource_delta_value": min(4.0, max(0.0, resource_gain)),
        "finisher_value": min(5.0, max(0.0, finisher)),
        "next_shot_setup_value": min(3.0, max(0.0, setup)),
        "waste_penalty": max(-4.0, min(0.0, waste)),
    }, facts, reasons)


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

    def damage(enemy, raw, source, directional=True):
        key = enemy["target_id"]
        resolved = (collision_damage(raw, enemy, source) if directional else
                    max(1, raw - float(enemy.get("defense", 0))))
        amount = min(hp[key] - damage_by_id[key], resolved)
        damage_by_id[key] += max(0, amount)

    attack = float(stats.get("attack", 1))
    definition = ball.get("definition_id", "")
    bank_gain = 0.0
    shared_reflection = _reflection_count(state)
    bank_reflection = path["mode"] == "bank"
    # Bank bonuses require a real shared stack. Their damage is direct damage,
    # not a separate finisher bonus, avoiding double-counting.
    bounce_empowered = (definition == "player_bounce" and bank_reflection
                        and shared_reflection > 0 and
                        _whole(stats.get("cushionConsumptionLimit", 1), 1) > 0)
    precision = definition == "player_standard" and relic_owned(state, 5) and path["mode"] == "direct" and power <= 4
    bonus = int(precision) + int(definition == "player_heavy" and relic_owned(state, 6))
    if definition == "player_bounce" and relic_owned(state, 8) and path["mode"] == "bank":
        bonus += 1
        bank_gain += 1
    # Anchor finisher's direct bonus shares a single damage application and
    # therefore pays target defense once, not twice.
    first_anchor_stacks = (_anchor_stacks_by_target(state).get(target["target_id"], 0)
                           if definition == "player_anchor_finisher" else 0)
    anchor_damage_bonus = first_anchor_stacks * (2 + min(
        2, _whole(_resource_state(state).get("player_anchor_stacks")) // 5))
    damage(target, attack + bonus + (attack if path["mode"] == "bank" and relic_owned(state, 3) else 0)
           + (_number(stats.get("cushionBounceAttackBonus", 1)) if bounce_empowered else 0)
           + anchor_damage_bonus, contact)
    # ChainImpact: the actual game consumes once and affects the two *closest*
    # other enemies. Do not reward everyone inside the radius.
    chain_impact_hits = 0
    heavy_stacks = _whole(_resource_state(state).get("heavy_stacks"))
    chain_radius = max(0.0, _number(stats.get("chainImpactRadius", 0)))
    if definition == "player_chain_impact" and chain_radius > 0:
        stacks_used = min(heavy_stacks, _whole(stats.get("heavyCollisionConsumeAmount", 5), 5))
        bonus_damage = stacks_used * _number(stats.get("heavyFinisherDamagePerCollision", 1), 1)
        center = pos(target["position"])
        nearby = [e for e in enemies if e["target_id"] != target["target_id"]
                  and length(sub(pos(e["position"]), center)) <= chain_radius]
        nearby.sort(key=lambda e: length(sub(pos(e["position"]), center)))
        for other in nearby[:2]:
            damage(other, attack + bonus + bonus_damage, center, directional=False)
            chain_impact_hits += 1
    if path["mode"] == "bank":
        spring_bonus = int(definition == "player_bounce" and relic_owned(state, 8))
        baseline_damage = min(hp[target["target_id"]], collision_damage(attack + bonus - spring_bonus + (_number(stats.get("cushionBounceAttackBonus", 1)) if bounce_empowered else 0), target, contact))
        bank_gain = max(0, damage_by_id[target["target_id"]] - baseline_damage)
    contacts += 1
    pierce_followups = 0
    chain_collisions = 0
    followup_hits = 0
    brittle_triggered = False
    brittle_wall_speed = 0.0
    pushed_distance = 0.0
    predicted_target_position = pos(target["position"])
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
    if definition == "player_ricochet_finisher" and shared_reflection >= _whole(stats.get("ricochetStackConsume", 6), 6):
        # Successful pinball activation stops the cue ball; do not count
        # an ordinary-ball residual hit after activation.
        outgoing = (0.0, 0.0)
    hit_ids = {target["target_id"]}

    if stats.get("pierce") and int(stats.get("pierceMaxUses", 1)) > 0:
        # Each pierced contact consumes one use. After the last use one final
        # ordinary impact is possible, then stop the straight-line estimate.
        retention = max(0, min(1, float(stats.get("pierceSpeedRetention", 0.75))))
        speed *= retention
        if stats.get("refractAfterPierce"):
            # Follow the line-of-centres direction the struck enemy would take
            # in an ordinary impact, while leaving that enemy stationary.
            direction = normal
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
            finisher_bonus = 0.0
            if definition == "player_pierce_finisher" and pierce_followups >= 0:
                finisher_bonus = (_number(stats.get("pierceFinisherBaseBonus", 1), 1)
                                  + max(0, len(hit_ids) - 1) *
                                  _number(stats.get("pierceFinisherMultiTargetBonus", 1), 1))
            damage(other, attack + impact_bonus + finisher_bonus, last_contact)
            contacts += 1
            pierce_followups += 1
            hit_ids.add(other["target_id"])
            if stats.get("refractAfterPierce"):
                direction = unit(sub(pos(other["position"]), last_contact))
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
        pushed_distance = push_reach
        center = pos(target["position"])
        push_wall = wall_contact(center, normal, walls)
        if push_wall:
            if push_wall[0] <= push_reach:
                brittle_wall_speed = remaining_speed(
                    push_speed, push_wall[0], target_stats)
                ability = board_ability(target)
                brittle_triggered = (
                    tactical_ai_enabled(state)
                    and ability.get("type") == "impact_brittle"
                    and not ability.get("brittle_active", False)
                    and brittle_wall_speed >= float(
                        ability.get("brittle_wall_speed_threshold", math.inf))
                )
            push_reach = min(push_reach, push_wall[0])
            pushed_distance = push_reach
        predicted_target_position = add(center, mul(normal, pushed_distance))
        secondary = first_contact(center, normal, push_reach, enemies, float(target.get("radius", 2.4)), hit_ids)
        if secondary:
            # Estimate only free movement until the next enemy collision.
            pushed_distance = min(pushed_distance, secondary[0])
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
    # P1: first anchor-marked target can be consumed immediately on contact.
    # This bounded estimate only covers the first guaranteed contact, not
    # arbitrary later collisions or position changes.
    if definition == "player_anchor_finisher":
        stacks = _anchor_stacks_by_target(state).get(target["target_id"], 0)
        if stacks > 0:
            sy = _resource_state(state)
            multiplier = 2 + min(2, _whole(sy.get("player_anchor_stacks")) // 5)
            finisher_bonus = stacks * multiplier
            # Direct target bonus was already included in the first collision.
            threshold = _whole(stats.get("anchorFinisherAoeThreshold", 5), 5)
            aoe_radius = _number(stats.get("anchorFinisherAoeRadius", 8), 8)
            if stacks >= threshold and aoe_radius > 0:
                for e in enemies:
                    if (e["target_id"] != target["target_id"] and
                            length(sub(pos(e["position"]), pos(target["position"]))) <= aoe_radius):
                        damage(e, math.floor(finisher_bonus / 2), pos(target["position"]), directional=False)
    risk = max(pre_risk, path_risk(post_origin, stop, radius, pockets))
    defense = float(stats.get("defense", 0))
    killed = {key for key in hp if hp[key] > 0 and damage_by_id[key] >= hp[key] - 1e-6}
    prevented_by_target = {}
    prevention_reasons = {}
    for enemy in enemies:
        key = enemy["target_id"]
        if key in killed | controlled and enemy.get("can_attack_this_turn", True):
            prevented_by_target[key] = max(1, float(enemy.get("attack", 1)) - defense)
            prevention_reasons[key] = "defeat_or_pocket_control"

    tactical = tactical_ai_enabled(state)
    charge_interruptions = []
    swarm_reductions = []
    brittle_reductions = []
    protection_releases = []
    protection_release_value = 0.0
    if tactical:
        for enemy in enemies:
            key = enemy["target_id"]
            if key in killed | controlled:
                continue
            info = tactics(enemy)
            charge = info.get("charge", {})
            interrupted = False
            if charge.get("interruptible_now"):
                if (ball.get("category") == "heavy" or definition in _HEAVY_BALLS) and key == target["target_id"]:
                    interrupted = pushed_distance >= float(
                        charge.get("heavy_required_enemy_movement", math.inf))
                elif stats.get("pierce"):
                    interrupted = damage_by_id.get(key, 0) >= float(
                        charge.get("pierce_required_damage", math.inf))
            if interrupted:
                amount = max(0.0, float(
                    charge.get("attack_damage_prevented_if_interrupted", 0)) - defense)
                if amount > 0:
                    prevented_by_target[key] = max(
                        prevented_by_target.get(key, 0), amount)
                    prevention_reasons[key] = "charge_interruption"
                    charge_interruptions.append(key)

            ability = info.get("board_ability", {})
            if key == target["target_id"] and brittle_triggered:
                before = float(ability.get("attack_before_brittle", enemy.get("attack", 1)))
                after = float(ability.get("attack_while_brittle", before))
                amount = max(0.0, max(1, before - defense) - max(1, after - defense))
                if amount > 0 and enemy.get("can_attack_this_turn", True):
                    prevented_by_target[key] = max(
                        prevented_by_target.get(key, 0), amount)
                    prevention_reasons[key] = "impact_brittle"
                    brittle_reductions.append(key)

        removed = killed | controlled
        for enemy in enemies:
            key = enemy["target_id"]
            ability = board_ability(enemy)
            if ability.get("type") == "protection_resonator" and key in removed:
                count = int(ability.get("resonator_protected_target_count", 0))
                reduction = float(ability.get("resonator_damage_reduction", 0))
                if count > 0:
                    protection_releases.append(key)
                    protection_release_value += count * max(1.0, reduction)
            elif (ability.get("type") == "protection_resonator"
                  and target["target_id"] not in removed
                  and pushed_distance > 0):
                radius = float(ability.get("radius", 0))
                affected = set(ability.get("affected_ally_target_ids", []))
                released = 0
                if key == target["target_id"]:
                    for ally in enemies:
                        if (ally["target_id"] in affected
                                and length(sub(pos(ally["position"]),
                                               predicted_target_position)) > radius):
                            released += 1
                elif target["target_id"] in affected:
                    if length(sub(predicted_target_position,
                                  pos(enemy["position"]))) > radius:
                        released = 1
                if released > 0:
                    protection_releases.append(target["target_id"])
                    reduction = float(
                        ability.get("resonator_damage_reduction", 0))
                    protection_release_value += released * max(1.0, reduction)
            if (ability.get("type") == "swarm_amplifier"
                    and key not in removed
                    and (removed.intersection(
                            ability.get("affected_ally_target_ids", []))
                         or (target["target_id"] not in removed
                             and pushed_distance > 0
                             and target["target_id"] in ability.get(
                                 "affected_ally_target_ids", [])
                             and length(sub(predicted_target_position,
                                            pos(enemy["position"]))) > float(
                                 ability.get("radius", 0))))):
                before = float(ability.get("swarm_attack", enemy.get("attack", 1)))
                after = float(ability.get("swarm_attack_after_one_ally_removed", before))
                amount = max(0.0, max(1, before - defense) - max(1, after - defense))
                if amount > 0 and enemy.get("can_attack_this_turn", True):
                    prevented_by_target[key] = max(
                        prevented_by_target.get(key, 0), amount)
                    prevention_reasons[key] = "swarm_stage_reduction"
                    swarm_reductions.append(key)

    prevented = sum(prevented_by_target.values())
    next_prevention_before_defense = {}
    next_prevention_reasons = {}
    future_charge_prevention = {}
    for enemy in enemies:
        key = enemy["target_id"]
        info = tactics(enemy)
        defeat = info.get("defeat_prevention", {})
        if key in killed | controlled:
            immediate = max(0.0, float(defeat.get(
                "immediate_attack_damage", 0)))
            if immediate <= 0 and enemy.get("can_attack_this_turn", True):
                immediate = max(1.0, float(enemy.get("attack", 1)))
            if immediate > 0:
                next_prevention_before_defense[key] = immediate
                next_prevention_reasons[key] = "defeat_or_pocket_control"
            future = max(0.0, float(defeat.get(
                "future_charged_attack_damage", 0)))
            if future > 0 and immediate <= 0:
                future_charge_prevention[key] = future
        if key in charge_interruptions:
            charge_amount = max(0.0, float(info.get("charge", {}).get(
                "attack_damage_prevented_if_interrupted", 0)))
            if charge_amount > next_prevention_before_defense.get(key, 0):
                next_prevention_before_defense[key] = charge_amount
                next_prevention_reasons[key] = "charge_interruption"
        ability = board_ability(enemy)
        if key in brittle_reductions:
            reduction = max(0.0, float(ability.get(
                "attack_before_brittle", enemy.get("attack", 1))) - float(
                    ability.get("attack_while_brittle", enemy.get("attack", 1))))
            if reduction > next_prevention_before_defense.get(key, 0):
                next_prevention_before_defense[key] = reduction
                next_prevention_reasons[key] = "impact_brittle"
        if key in swarm_reductions:
            reduction = max(0.0, float(ability.get(
                "swarm_attack", enemy.get("attack", 1))) - float(
                    ability.get("swarm_attack_after_one_ally_removed",
                                enemy.get("attack", 1))))
            if reduction > next_prevention_before_defense.get(key, 0):
                next_prevention_before_defense[key] = reduction
                next_prevention_reasons[key] = "swarm_stage_reduction"
    incoming_before_prevention = sum(
        max(1, float(e.get("attack", 1)) - defense) for e in enemies
        if e.get("can_attack_this_turn", True))
    incoming_damage = max(0.0, incoming_before_prevention - prevented)
    predicted_shield = 0
    if definition == "player_stop_shield":
        stacks = _anchor_stacks_by_target(state).get(target["target_id"], 0)
        consumed = min(stacks, _whole(stats.get("anchorEnemyConsumeLimit", 5), 5))
        max_shield = _whole(stats.get("stopShieldAmount"))
        if consumed > 0 and max_shield > 0:
            predicted_shield = max(1, int(math.floor(consumed * max_shield / 5 + 0.5)))
        # Preserve the game's shield cap and any currently carried shield.
        existing_shield = _number(state.get("player", {}).get("shield", 0))
        predicted_shield = max(0, min(predicted_shield, max_shield - existing_shield))
    incoming_damage = max(0.0, incoming_damage - predicted_shield)
    player = state["player"]
    current_hp = float(player.get("current_hp", 50))
    max_hp = max(1, float(player.get("max_hp", 50)))
    pocket_cost = state.get("pocket_rules", {}).get("player", {}).get("damage_amount", math.ceil(max_hp * 0.04))
    repair = min(max(0, max_hp - current_hp), 1.0) if relic_owned(state, 4) and contacts >= 3 else 0.0
    net_loss = incoming_damage + risk * pocket_cost - repair
    urgency = 1 + max(0, 0.5 - current_hp / max_hp) * 4
    stable = max(0, 1 - min(1, post_distance / 35)) * (1 - risk) if stats.get("anchor") else 0
    uncertainty = 0.0
    if path["kind"] in ("offset", "chain_contact"):
        uncertainty += 0.15
    if path["mode"] == "bank":
        uncertainty += 0.12
    uncertainty += 0.10 * followup_hits + 0.12 * chain_collisions
    uncertainty += 0.08 * pierce_followups
    ai_profile = tactical_ai_profile(state)
    future_setup_raw = protection_release_value if tactical else 0.0
    future_setup = future_setup_raw
    danger_priority = 0.0
    if tactical and ai_profile == "survival_v2":
        # Future board setup remains useful, but cannot outrank a reliable
        # immediate hit by scaling only with the number of protected allies.
        future_cap = 0.75 + 0.5 * sum(damage_by_id.values()) + 1.5 * len(killed)
        future_setup = min(future_setup_raw, future_cap)
        hp_ratio = current_hp / max_hp
        future_setup *= max(0.30, min(1.0, hp_ratio * 1.5))
        for enemy in enemies:
            key = enemy["target_id"]
            if key not in killed | controlled:
                continue
            info = tactics(enemy)
            future_charge = float(info.get("defeat_prevention", {}).get(
                "future_charged_attack_damage", 0))
            affected = len(info.get("board_ability", {}).get(
                "affected_ally_target_ids", []))
            danger_priority += (
                max(0.0, float(enemy.get("attack", 1)) - 1.0) * 0.25
                + future_charge * 0.35
                + affected * 0.20
            ) * urgency

    # Enemy-only travel prediction: up to 3 rebounds. Effects remain
    # estimates; they do not create guaranteed damage or kills.
    pushed_motion = ({"travel": pushed_distance, "wall_bounces": 0,
                      "post_wall_contacts": 0})
    if (definition in {"player_heavy", "player_ricochet_finisher"}
            and push_speed > 0.0):
        pushed_motion = _pushed_enemy_trajectory(
            enemies, target, normal, push_speed, walls, max_wall_bounces=3)

    # Score P1 effects separately from confirmed immediate damage.
    contact_ids = set(hit_ids)
    if followup_hits:
        contact_ids.add(other["target_id"])
    special_metrics, special_facts, special_reasons = _special_score(
        state, ball, stats, enemies, target, path, hp, damage_by_id,
        contact_ids, pierce_followups, pushed_distance, chain_collisions,
        incoming_damage, bank_reflection, predicted_shield, pushed_motion)
    # Anchor targets with existing marks are preferred. If no direct path to
    # the most marked target exists, reward finishing this shot at a position
    # that makes a direct next-turn approach possible.
    followup_stop = (start if definition == "player_origin_anchor" else stop)
    anchor_focus, anchor_followup, anchor_facts = _anchor_target_policy(
        state, ball, enemies, target, damage_by_id, followup_stop, start,
        power, profile, quality, path)
    special_facts.update(anchor_facts)
    if anchor_focus:
        special_reasons.append("既にアンカー印を付けた敵を再び狙う")
    if anchor_followup:
        special_reasons.append("次ターンに印が最も多い敵を狙える位置へつなぐ")

    metrics = {
        "damage": sum(damage_by_id.values()), "kills": len(killed),
        "prevented_attack": prevented, "pierce_followups": pierce_followups,
        "chain_collisions": chain_collisions, "bank_damage_gain": min(bank_gain, damage_by_id[target["target_id"]]),
        "pocket_control": len(controlled - killed), "safety": -net_loss * urgency,
        "stable_stop": stable, "precision": quality,
        "protection_release": future_setup,
        "danger_priority": danger_priority,
        "uncertainty": -uncertainty * (
            1.0 + (future_setup_raw / max(1.0, sum(damage_by_id.values())))
            if ai_profile == "survival_v2" else 1.0
        ) if tactical else 0.0,
    }
    # Area damage is already counted in 'damage'; it is not an actual
    # enemy-enemy collision and should not earn chain collision points.
    metrics.update(special_metrics)
    metrics["anchor_target_focus"] = anchor_focus
    metrics["anchor_followup_position"] = anchor_followup
    weights = {**DEFAULT_WEIGHTS, **profile.get("build_policy", {}).get("shot_evaluation", {})}
    if tactical and ai_profile == "survival_v2":
        weights.update({
            "damage": float(weights.get("damage", 1.0)) * 1.15,
            "kills": float(weights.get("kills", 4.0)) * 1.25,
            "prevented_attack": float(weights.get("prevented_attack", 2.0)) * 1.25,
            "chain_collisions": float(weights.get("chain_collisions", 0.5)) * 0.55,
            "protection_release": 0.60,
            "danger_priority": 1.0,
            "uncertainty": 1.40,
            "safety": float(weights.get("safety", 2.0)) * 1.20,
        })
    breakdown = {key: round(value * float(weights.get(key, 0)), 4) for key, value in metrics.items()}
    breakdown["power_cost"] = round(-0.12 * power, 4)
    breakdown["lethal_risk"] = -150.0 if net_loss >= current_hp else 0.0
    decision_reasons = []
    if killed:
        if float(target.get("attack", 0)) >= 2:
            decision_reasons.append("高攻撃力の敵を先に倒す")
        else:
            decision_reasons.append("敵を撃破して次の攻撃を減らす")
    if charge_interruptions:
        decision_reasons.append("Chargeを妨害して攻撃を阻止する")
    if protection_releases:
        decision_reasons.append("防護共鳴を解除して後続攻撃を通しやすくする")
    if swarm_reductions:
        decision_reasons.append("味方を範囲外にして群体の攻撃段階を下げる")
    if brittle_reductions:
        decision_reasons.append("敵を壁へぶつけて攻撃3から1へ脆化させる")
    if chain_collisions or chain_impact_hits:
        decision_reasons.append("複数の敵への連鎖衝突を狙う")
    if pierce_followups:
        decision_reasons.append("貫通後の後続敵への命中を狙う")
    if path["mode"] == "bank":
        decision_reasons.append("壁反射経由で敵へ命中する")
    if stats.get("anchor") and stable > 0:
        decision_reasons.append("アンカー停止と防御で次ターンの被害を抑える")
    decision_reasons.extend(special_reasons)
    if not decision_reasons:
        decision_reasons.append("到達可能な候補の中でダメージと安全性が高い")

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
        "expected_attack_prevention": round(prevented, 3),
        "expected_attack_prevention_by_target": {
            key: {"amount": round(value, 3), "reason": prevention_reasons[key]}
            for key, value in prevented_by_target.items()
        },
        "expected_next_turn_prevention_before_defense": round(
            sum(next_prevention_before_defense.values()), 3),
        "expected_next_turn_prevention_by_target": {
            key: {
                "amount": round(value, 3),
                "reason": next_prevention_reasons[key],
            }
            for key, value in next_prevention_before_defense.items()
        },
        "expected_future_charge_prevention": round(
            sum(future_charge_prevention.values()), 3),
        "expected_future_charge_prevention_by_target": {
            key: round(value, 3)
            for key, value in future_charge_prevention.items()
        },
        "self_pocket_risk": round(risk, 3), "expected_repair": repair,
        "estimated_stop_position": point(stop), "followup_hits": followup_hits,
        "predicted_chain_count": chain_collisions + chain_impact_hits,
        "predicted_kill_count": len(killed),
        "prediction_uncertainty": round(uncertainty, 3),
        "special_ability_estimates": dict(special_facts, chain_impact_aoe_targets=chain_impact_hits),
        "tactical_ai_profile": ai_profile,
        "future_setup_raw": round(future_setup_raw, 3),
        "future_setup_scored": round(future_setup, 3),
        "tactical_outcomes": {
            "charge_interruptions": charge_interruptions,
            "protection_releases": protection_releases,
            "swarm_stage_reductions": swarm_reductions,
            "impact_brittle_reductions": brittle_reductions,
            "brittle_wall_speed": round(brittle_wall_speed, 3),
        },
        "shot_path": {
            "route": path["mode"], "contact": path["kind"],
            "wall_index": path.get("wall_index", -1),
            "aim_point": point(path["aim"]),
        },
        "decision_reasons": decision_reasons,
        "reason": " / ".join(decision_reasons),
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
    if fixed_power is not None:
        powers = [float(fixed_power)]  # Respect an explicit manual power.
    elif ball.get("definition_id") == "player_heavy":
        # Heavy resource gain is based on enemy movement. Always evaluate the
        # full power permitted by the current player's profile (7 for the
        # intermediate MCP profile), instead of an arbitrary weak shot.
        powers = [hi]
    else:
        powers = sorted({lo, hi, (lo + hi) / 2, max(lo, min(hi, 4.0)),
                         max(lo, min(hi, safe_precision_power))})
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
    # A stack-generating shot should not beat a clear kill on stack points
    # alone. Keep the option for rare threshold-crossing synergies.
    if (ball.get("definition_id") in {"player_heavy", "player_lightweight",
                                       "player_pierce", "player_refractive_pierce",
                                       "player_pierce_finisher", "player_minion_finisher",
                                       "player_cushion_charge", "player_anchor",
                                       "player_origin_anchor"}
            and any(c.get("predicted_kill_count", 0) > 0 for c in candidates)):
        for c in candidates:
            if (c.get("predicted_kill_count", 0) == 0
                    and not c.get("special_ability_estimates", {}).get("exceptional_stack_setup")):
                c["score_breakdown"]["available_kill_priority"] = -2.5
                c["score"] = round(c["score"] - 2.5, 4)
                c["decision_reasons"].append("撃破可能な別候補があるため資源準備を控えめに評価")
                c["reason"] = " / ".join(c["decision_reasons"])
    candidates.sort(key=lambda c: (-c["score"], c["power"], c["target_id"]))
    return candidates


def build_joint_shot_context(state, profile):
    choices = []
    reachable_candidates = 0
    charge_candidates = 0
    charge_targets = set()
    offers = state.get("offered_balls", [])
    for ball in offers:
        candidates = evaluate_build_shots(state, profile, ball=ball)
        reachable_candidates += len(candidates)
        for candidate in candidates:
            targets = candidate.get("tactical_outcomes", {}).get(
                "charge_interruptions", [])
            if targets:
                charge_candidates += 1
                charge_targets.update(targets)
        if candidates:
            choices.append(candidates[0])
    choices.sort(key=lambda c: c["score"], reverse=True)
    # Shared priority for *all* resource-generating balls: a reasonably close
    # confirmed kill beats speculative stack farming. Exceptional setups that
    # unlock an owned consumer remain valid alternatives.
    lethal_preference_used = False
    generators = {"player_heavy", "player_lightweight", "player_pierce",
                  "player_refractive_pierce", "player_pierce_finisher",
                  "player_minion_finisher", "player_cushion_charge",
                  "player_anchor", "player_origin_anchor"}
    if choices and choices[0].get("definition_id") in generators:
        best = choices[0]
        exceptional = best.get("special_ability_estimates", {}).get(
            "exceptional_stack_setup", False)
        if best.get("predicted_kill_count", 0) == 0 and not exceptional:
            lethal = next((c for c in choices if c.get("predicted_kill_count", 0) > 0), None)
            if lethal and best["score"] - lethal["score"] <= 3.0:
                choices.remove(lethal)
                choices.insert(0, lethal)
                lethal_preference_used = True
    candidate_generation = {
        "reachable_candidates": reachable_candidates,
        "charge_interruption_candidates": charge_candidates,
        "charge_interruption_target_ids": sorted(charge_targets),
        "lethal_preference_over_stack_farming": lethal_preference_used,
    }
    return {"model": MODEL_VERSION, "estimate_only": True,
            "recommended": choices[0] if choices else None, "choices": choices,
            "candidate_generation": candidate_generation,
            "limitations": "Geometry estimates: at most one pushed-enemy collision and one residual hit; up to three pushed-enemy wall rebounds are heuristic only, not a moving multi-ball simulation."}
