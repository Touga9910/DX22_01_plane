"""Reachability-aware route and rest-site value evaluation for MCP autoplay."""

from __future__ import annotations

import math
from typing import Any

from build_decision import (
    build_progress_snapshot,
    evaluate_shop_ball_removal,
    evaluate_upgrade_choices,
    role_composition_snapshot,
)


DEFAULT_ROUTE_VALUE_WEIGHTS = {
    "recovery_value": 1.0,
    "free_upgrade_value": 1.0,
    "deck_compression_value": 1.0,
    "generator_acquisition_value": 0.8,
    "finisher_acquisition_value": 0.7,
    "support_acquisition_value": 0.6,
    "relic_value": 0.5,
    "future_shop_access_value": 0.8,
    "expected_hp_risk": 1.0,
    "money_cost": 0.5,
}


def _as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def _weights(profile: dict[str, Any]) -> dict[str, float]:
    settings = _as_dict(profile.get("stage_choice_policy"))
    configured = _as_dict(settings.get("route_value_weights"))
    return {
        key: max(0.0, float(configured.get(key, default)))
        for key, default in DEFAULT_ROUTE_VALUE_WEIGHTS.items()
    }


def _upgrade_effect_benefit(
    state: dict[str, Any], candidate: dict[str, Any],
) -> dict[str, Any]:
    instance_id = candidate.get("instance_id")
    ball = next(
        (
            value for value in _as_list(state.get("deck_balls"))
            if isinstance(value, dict) and value.get("instance_id") == instance_id
        ),
        {},
    )
    current = _as_dict(ball.get("status"))
    upgraded = _as_dict(ball.get("next_upgrade"))
    attack_gain = max(
        0.0,
        float(upgraded.get("attack", current.get("attack", 0)))
        - float(current.get("attack", 0)),
    )
    improved_fields: list[str] = []
    for key, next_value in upgraded.items():
        current_value = current.get(key)
        if isinstance(next_value, bool):
            if next_value and not bool(current_value):
                improved_fields.append(key)
        elif (
            isinstance(next_value, (int, float))
            and not isinstance(next_value, bool)
            and isinstance(current_value, (int, float))
            and float(next_value) > float(current_value)
            and key != "attack"
        ):
            improved_fields.append(key)
    return {
        "attack_gain": round(attack_gain, 3),
        "improved_fields": improved_fields,
        "effect_value": round(attack_gain * 12.0 + len(improved_fields) * 2.5, 3),
    }


def _reachable_paths(
    state: dict[str, Any], start_id: int, horizon: int,
) -> list[list[dict[str, Any]]]:
    nodes = {
        int(node.get("node_id")): node
        for node in _as_list(_as_dict(state.get("run_map")).get("nodes"))
        if isinstance(node, dict) and isinstance(node.get("node_id"), int)
    }
    start = nodes.get(start_id)
    if start is None:
        return []
    paths: list[list[dict[str, Any]]] = []

    def visit(node: dict[str, Any], path: list[dict[str, Any]]) -> None:
        current = [*path, node]
        if len(current) >= horizon:
            paths.append(current)
            return
        next_nodes = [
            nodes.get(int(node_id))
            for node_id in _as_list(node.get("next_node_ids"))
        ]
        next_nodes = [
            candidate for candidate in next_nodes
            if candidate is not None
            and not bool(candidate.get("visited", False))
            and candidate.get("reachable") is not False
        ]
        if not next_nodes:
            paths.append(current)
            return
        for candidate in next_nodes:
            visit(candidate, current)

    visit(start, [])
    return paths


def evaluate_rest_site_action(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    profile_id: str,
) -> dict[str, Any]:
    player = _as_dict(state.get("player"))
    current_hp = max(0, int(player.get("current_hp", 0)))
    maximum_hp = max(1, int(player.get("max_hp", max(1, current_hp))))
    configured_amount = int(
        _as_dict(state.get("rest_site_rule")).get(
            "configured_heal_amount", math.ceil(maximum_hp * 0.25)
        )
    )
    available_actions = set(_as_list(state.get("available_actions")))
    at_rest_site = state.get("scene") == "rest_site"
    can_heal = not at_rest_site or "heal" in available_actions
    can_upgrade = not at_rest_site or "upgrade_ball" in available_actions
    recovery_amount = min(maximum_hp - current_hp, max(0, configured_amount))
    if not can_heal:
        recovery_amount = 0
    recovery_ratio = recovery_amount / maximum_hp
    policy = _as_dict(profiles.get(profile_id, {}).get("stage_choice_policy"))
    expected_loss = max(0.0, float(policy.get("expected_battle_hp_loss", 6.0)))
    danger_ratio = max(0.0, float(policy.get("danger_hp_ratio", 0.4)))
    hp_ratio = current_hp / maximum_hp
    recovery_value = recovery_ratio * 100.0
    if current_hp < expected_loss * 2.0 or hp_ratio <= danger_ratio:
        recovery_value += 100.0

    upgrades = evaluate_upgrade_choices(state, profiles, profile_id)
    upgrade = upgrades.get("recommended")
    if not can_upgrade:
        upgrade = None
    upgrade_value = 0.0
    upgrade_effect = {"attack_gain": 0.0, "improved_fields": [], "effect_value": 0.0}
    if isinstance(upgrade, dict):
        breakdown = _as_dict(upgrade.get("breakdown"))
        role_gain = max(0.0, float(breakdown.get("missing_role_bonus", 0.0)))
        upgrade_effect = _upgrade_effect_benefit(state, upgrade)
        upgrade_value = 16.0 + float(upgrade_effect["effect_value"])
        upgrade_value += min(24.0, float(upgrade.get("score", 0.0)) * 0.04)
        upgrade_value += min(24.0, role_gain * 0.2)

    if recovery_amount <= 0 and not isinstance(upgrade, dict):
        return {
            "recommended_action": None,
            "reason": "no_rest_benefit",
            "recovery_value": 0.0,
            "free_upgrade_value": 0.0,
        }
    choose_heal = recovery_amount > 0 and (
        hp_ratio <= danger_ratio or recovery_value >= upgrade_value
    )
    return {
        "recommended_action": "heal" if choose_heal else "upgrade_ball",
        "reason": (
            "danger_survival_priority" if choose_heal and hp_ratio <= danger_ratio
            else "recovery_value_exceeds_upgrade" if choose_heal
            else "free_upgrade_value_exceeds_recovery"
        ),
        "current_hp": current_hp,
        "maximum_hp": maximum_hp,
        "configured_heal_amount": configured_amount,
        "recovery_amount": recovery_amount,
        "recovery_ratio": round(recovery_ratio, 4),
        "expected_next_battle_hp_loss": expected_loss,
        "recovery_value": round(recovery_value, 3),
        "free_upgrade_value": round(upgrade_value, 3),
        "upgrade_candidate": upgrade,
        "upgrade_effect": upgrade_effect,
    }


def evaluate_route_options(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    profile_id: str,
    wanted_rewards: list[str] | None = None,
    preferred_route_index: int = -1,
    horizon: int = 3,
) -> dict[str, Any]:
    profile = profiles.get(profile_id, {})
    weights = _weights(profile)
    player = _as_dict(state.get("player"))
    current_hp = max(0, int(player.get("current_hp", 0)))
    maximum_hp = max(1, int(player.get("max_hp", max(1, current_hp))))
    money = max(0, int(player.get("money", 0)))
    hp_ratio = current_hp / maximum_hp
    policy = _as_dict(profile.get("stage_choice_policy"))
    expected_loss = max(0.0, float(policy.get("expected_battle_hp_loss", 6.0)))
    danger_ratio = max(0.0, float(policy.get("danger_hp_ratio", 0.4)))
    progress = build_progress_snapshot(state, profiles, profile_id)
    roles = role_composition_snapshot(state, profile_id)

    simulated_shop = dict(state)
    simulated_shop["scene"] = "shop"
    simulated_shop["available_actions"] = ["remove_ball"] if money >= 15 else []
    # Future stock is unknown: current relic and ball offers are deliberately
    # excluded from route planning.
    simulated_shop["relics"] = []
    simulated_shop["shop_ball_offers"] = []
    removal = evaluate_shop_ball_removal(
        simulated_shop, profiles, profile_id,
    )
    removable = isinstance(removal.get("recommended"), dict)
    removal_score = float(
        _as_dict(removal.get("recommended")).get("removal_score", 0.0)
    )
    upgrades = evaluate_upgrade_choices(state, profiles, profile_id)
    upgrade = upgrades.get("recommended")
    free_upgrade_base = (
        16.0
        + float(_upgrade_effect_benefit(state, upgrade)["effect_value"])
        + min(24.0, float(upgrade.get("score", 0.0)) * 0.04)
        if isinstance(upgrade, dict) else 0.0
    )
    role_counts = _as_dict(roles.get("counts"))
    deficits = {
        "Generator": int(role_counts.get("Generator", 0)) == 0,
        "Finisher": int(role_counts.get("Finisher", 0)) == 0,
        "Support": int(role_counts.get("Support", 0)) == 0,
    }
    if profile_id == "standard":
        deficits["Generator"] = False
        deficits["Finisher"] = False
    wanted = list(wanted_rewards or [])
    wanted_destination = {
        "hp_recovery": "rest", "ball_upgrade": "rest", "relic": "shop",
    }
    options: list[dict[str, Any]] = []
    immediate_rest_exists = any(
        option.get("destination") == "rest"
        for option in _as_list(state.get("route_options"))
        if isinstance(option, dict)
    )

    for option in _as_list(state.get("route_options")):
        if not isinstance(option, dict) or not isinstance(option.get("route_index"), int):
            continue
        route_index = int(option["route_index"])
        node_id = int(option.get("node_id", -1))
        destination = str(option.get("destination", "unknown"))
        paths = _reachable_paths(state, node_id, max(1, min(3, horizon)))
        if not paths:
            paths = [[{"node_id": node_id, "destination": destination, "next_node_ids": option.get("next_node_ids", [])}]]
        reachable_rest: dict[int, int] = {}
        reachable_shop: dict[int, int] = {}
        battle_counts: list[int] = []
        for path in paths:
            battle_count = 0
            for distance, node in enumerate(path):
                kind = str(node.get("destination", "unknown"))
                candidate_id = int(node.get("node_id", -1))
                if kind == "rest":
                    reachable_rest[candidate_id] = min(reachable_rest.get(candidate_id, 99), distance)
                elif kind == "shop":
                    reachable_shop[candidate_id] = min(reachable_shop.get(candidate_id, 99), distance)
                elif kind in {"battle", "midboss", "final_boss"}:
                    battle_count += 1
            battle_counts.append(battle_count)
        guaranteed_shop = bool(paths) and all(
            any(str(node.get("destination")) == "shop" for node in path)
            for path in paths
        )
        guaranteed_rest = bool(paths) and all(
            any(str(node.get("destination")) == "rest" for node in path)
            for path in paths
        )
        expected_battles = sum(battle_counts) / max(1, len(battle_counts))
        hp_risk = expected_battles * expected_loss
        if hp_ratio <= danger_ratio:
            hp_risk *= 1.5
        recovery_value = 0.0
        if destination == "rest":
            recoverable = min(maximum_hp - current_hp, math.ceil(maximum_hp * 0.25))
            recovery_value = max(0.0, recoverable / maximum_hp * 100.0)
        elif reachable_rest:
            nearest = min(reachable_rest.values())
            recovery_value = max(0.0, 12.0 / (nearest + 1))
        free_upgrade_value = free_upgrade_base if destination == "rest" else (
            free_upgrade_base * 0.25 / (min(reachable_rest.values()) + 1)
            if reachable_rest else 0.0
        )
        shop_factor = 1.0 if destination == "shop" else (
            (0.7 if guaranteed_shop else 0.4) / (min(reachable_shop.values()) + 1)
            if reachable_shop else 0.0
        )
        purchase_factor = shop_factor if money >= 20 else 0.0
        deck_value = (35.0 + min(30.0, removal_score * 0.25)) * shop_factor if removable else 0.0
        generator_value = 28.0 * purchase_factor if deficits["Generator"] else 0.0
        finisher_value = 20.0 * purchase_factor if deficits["Finisher"] else 0.0
        support_value = 14.0 * purchase_factor if deficits["Support"] else 0.0
        relic_value = 10.0 * purchase_factor
        future_shop_value = (
            (16.0 if guaranteed_shop else 9.0) / (min(reachable_shop.values()) + 1)
            if reachable_shop and destination != "shop" else 0.0
        )
        money_cost = 15.0 * shop_factor if removable else 0.0
        values = {
            "recovery_value": recovery_value,
            "free_upgrade_value": free_upgrade_value,
            "deck_compression_value": deck_value,
            "generator_acquisition_value": generator_value,
            "finisher_acquisition_value": finisher_value,
            "support_acquisition_value": support_value,
            "relic_value": relic_value,
            "future_shop_access_value": future_shop_value,
            "expected_hp_risk": hp_risk,
            "money_cost": money_cost,
        }
        score = sum(
            weights[key] * value * (-1.0 if key in {"expected_hp_risk", "money_cost"} else 1.0)
            for key, value in values.items()
        )
        for rank, reward in enumerate(wanted):
            if wanted_destination.get(reward) == destination:
                score += max(0.0, 5.0 - rank)
                break
        if route_index == preferred_route_index:
            score += 1.0
        safety_eligible = not (
            hp_ratio <= danger_ratio and immediate_rest_exists and destination != "rest"
        )
        options.append({
            "route_index": route_index,
            "node_id": node_id,
            "destination": destination,
            "score": round(score, 3),
            "safety_eligible": safety_eligible,
            "values": {key: round(value, 3) for key, value in values.items()},
            "reachable_rest_nodes": [
                {"node_id": key, "distance": value}
                for key, value in sorted(reachable_rest.items(), key=lambda item: item[1])
            ],
            "reachable_shop_nodes": [
                {"node_id": key, "distance": value}
                for key, value in sorted(reachable_shop.items(), key=lambda item: item[1])
            ],
            "future_shop_access": "guaranteed" if guaranteed_shop else "possible" if reachable_shop else "unreachable",
            "future_rest_access": "guaranteed" if guaranteed_rest else "possible" if reachable_rest else "unreachable",
            "path_count": len(paths),
            "expected_battles": round(expected_battles, 3),
            "projected_money": money,
        })

    safe = [option for option in options if option["safety_eligible"]]
    selectable = safe or options
    selectable.sort(key=lambda option: float(option["score"]), reverse=True)
    selected = selectable[0] if selectable else None
    return {
        "build_profile": profile_id,
        "current_hp": current_hp,
        "maximum_hp": maximum_hp,
        "money": money,
        "build_progress": progress,
        "role_composition": roles,
        "deck_compression": removal,
        "weights": weights,
        "lookahead_horizon": max(1, min(3, horizon)),
        "routes": options,
        "selected": selected,
        "selected_route_index": selected.get("route_index") if selected else -1,
        "selection_reason": (
            "survival_safe_highest_integrated_value"
            if hp_ratio <= danger_ratio else "highest_integrated_route_value"
        ),
    }
