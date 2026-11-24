"""Frozen, pooled damage references; never looks up a seed's outcome."""
import json
import math
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def reference():
    return json.loads(Path(__file__).with_name("recovery_risk_reference.json").read_text(encoding="utf-8"))


def battles_until_recovery(state):
    run_map = state.get("run_map", {})
    nodes = {n["node_id"]: n for n in run_map.get("nodes", [])}
    active = next((n for n in nodes.values() if n.get("active")), None)
    if active is None and run_map.get("path"):
        active = nodes.get(run_map["path"][-1])
    if active is None:
        return 2
    queue = [(node, 0) for node in active.get("next_node_ids", [])]
    seen = set()
    terminal = []
    while queue:
        node_id, count = queue.pop(0)
        if (node_id, count) in seen or count > 3:
            continue
        seen.add((node_id, count))
        node = nodes.get(node_id, {})
        kind = node.get("destination")
        if kind in {"rest", "boss_preparation"}:
            terminal.append(count)
            continue
        count += int(kind in {"battle", "midboss", "final_boss"})
        next_ids = node.get("next_node_ids", [])
        if not next_ids:
            terminal.append(count)
        queue.extend((child, count) for child in next_ids)
    return min(3, min(terminal)) if terminal else 3


def recovery_reserve(state):
    player = state.get("player", {})
    if "progress" not in player:
        return None  # Legacy/minimal callers retain their configured fallback.
    progress = int(player["progress"])
    group = "late" if progress >= 10 else "mid" if progress >= 4 else "early"
    stats = reference()["groups"][group]
    nodes = {n["node_id"]: n for n in state.get("run_map", {}).get("nodes", [])}
    active = next((n for n in nodes.values() if n.get("active")), None)
    paths = []
    def visit(node_id, fights, depth, seen):
        if node_id in seen or depth > 8:
            paths.append(fights)
            return
        node = nodes.get(node_id, {})
        kind = node.get("destination")
        if kind in {"rest", "boss_preparation"}:
            paths.append(fights)
            return
        if kind in {"battle", "midboss", "final_boss"}:
            future = max(progress, int(node.get("area", progress + depth)))
            fights = fights + [{"node_id": node_id, "progress": future,
                                "kind": kind, **loss_reference(future, kind)}]
        children = node.get("next_node_ids", [])
        if not children:
            paths.append(fights)
        for child in children:
            visit(child, fights, depth + 1, seen | {node_id})
    if active:
        for child in active.get("next_node_ids", []):
            visit(child, [], 1, set())
    if not paths:
        paths = [[{"progress": progress + i + 1, "kind": "battle",
                   **loss_reference(progress + i + 1, "battle")}
                  for i in range(battles_until_recovery(state))]]
    max_hp = max(1, int(player.get("max_hp", 50)))
    # Protect against a large next loss, then typical further fights, plus
    # a 12% HP margin. Nearby consecutive rests do not require two heals.
    def needed(fights):
        return max(max_hp * .4, sum(f["mean"] for f in fights)
                   + max((f["p90"] - f["mean"] for f in fights), default=0)
                   + (max_hp * .12 if fights else 0))
    fights = min(paths, key=needed)
    count = len(fights)
    reserve = needed(fights)
    return {"group": group, "samples": stats["samples"], "p90_loss": stats["p90"],
            "mean_loss": stats["mean"], "battles_until_recovery": count,
            "required_hp": round(min(max_hp * .9, reserve), 3),
            "uncapped_required_hp": round(reserve, 3), "future_fights": fights,
            "source": "pooled_previous_campaign", "estimate_only": True}


def loss_reference(progress, kind="battle"):
    group = ("midBoss" if kind == "midboss" else "boss" if kind == "final_boss"
             else "late" if progress >= 10 else "mid" if progress >= 4 else "early")
    return {"group": group, **reference()["groups"][group]}


def project_path_health(state, path, first_rest_action=None):
    """Roll HP forward in order; a rest can heal only once and cannot erase past risk."""
    player = state.get("player", {})
    hp = float(player.get("current_hp", 0))
    max_hp = max(1, int(player.get("max_hp", 50)))
    heal = state.get("rest_heal", {}).get("configured_heal_amount", math.ceil(max_hp * .25))
    progress = int(player.get("progress", 0))
    minimum = hp
    loss = 0.0
    tail = 0.0
    worst_shortfall = 0.0
    steps = []
    for index, node in enumerate(path):
        kind = node.get("destination")
        future = max(progress, int(node.get("area", progress + index + 1)))
        entry = hp
        if kind in {"battle", "midboss", "final_boss"}:
            stats = loss_reference(future, kind)
            worst_shortfall = max(worst_shortfall, stats["p90"] + max_hp * .12 - hp)
            loss += stats["mean"]
            tail = max(tail, stats["p90"] - stats["mean"])
            hp -= stats["mean"]
            minimum = min(minimum, hp - tail)
        elif kind in {"rest", "boss_preparation"}:
            # A rest resets accumulated variance, never the recorded low-water mark.
            projected = {**state, "player": {**player, "current_hp": max(0, hp), "progress": future},
                         "run_map": {**state.get("run_map", {}), "nodes": [
                             {**n, "active": n.get("node_id") == node.get("node_id")}
                             for n in state.get("run_map", {}).get("nodes", [])]}}
            reserve = recovery_reserve(projected)
            action = first_rest_action if index == 0 and first_rest_action else (
                "heal" if hp <= (reserve or {}).get("required_hp", max_hp * .4) else "upgrade_ball")
            if action == "heal":
                hp = min(max_hp, hp + heal)
            tail = 0.0
        steps.append({"node_id": node.get("node_id"), "kind": kind, "progress": future,
                      "entry_hp": round(entry, 3), "exit_hp": round(hp, 3)})
    shortfall = max(0, worst_shortfall, max_hp * .12 - minimum)
    return {"expected_loss": round(loss, 3), "exit_hp": round(hp, 3),
            "minimum_tail_hp": round(minimum, 3), "reserve_shortfall": round(shortfall, 3),
            "risk_cost": round(loss + shortfall * 4 + (150 if minimum <= 0 else 0), 3),
            "steps": steps, "estimate_only": True}
