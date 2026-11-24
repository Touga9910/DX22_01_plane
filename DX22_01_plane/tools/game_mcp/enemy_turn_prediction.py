"""Forecast the enemy action after a phase shot, without advancing game state."""
from __future__ import annotations

import math


def forecast_enemy_turn(state):
    phase = (state.get("battle_combat_mode") == "phase_prototype"
             or (state.get("phase_battle") or {}).get("combat_mode") == "phase_prototype")
    if not phase or state.get("_enemy_turn_forecast_applied"):
        return state
    result = dict(state)
    result["_enemy_turn_forecast_applied"] = True
    enemies = []
    for original in state.get("enemies", []):
        enemy = dict(original)
        active = (not enemy.get("defeated") and not enemy.get("pocketed")
                  and enemy.get("combat_active", True))
        due = active and not enemy.get("stunned", False) and int(enemy.get("turns_until_attack", 1)) <= 1
        charging = str(enemy.get("charge_state", "")).lower() == "charging"
        can_charge = bool(enemy.get("can_charge", False))
        action = ("charge_attack" if charging else "charge_start") if due and can_charge else "normal_attack" if due else "wait"
        attacks = action in ("normal_attack", "charge_attack")
        multiplier = float(state.get("enemy_action_rules", {}).get("charge_damage_multiplier", 2.0)) if action == "charge_attack" else 1.0
        damage = math.ceil(float(enemy.get("attack", 1)) * multiplier) if attacks else 0
        enemy["can_attack_this_turn"] = attacks
        enemy["forecast_attack_damage"] = damage
        tactics = dict(enemy.get("tactics", {}))
        tactics["next_action_prediction"] = {**tactics.get("next_action_prediction", {}),
            "action": action, "executes_this_turn": attacks,
            "predicted_player_damage_before_defense": damage, "source": "phase_attack_timer"}
        tactics["defeat_prevention"] = {**tactics.get("defeat_prevention", {}),
            "immediate_attack_damage": damage,
            "future_charged_attack_damage": math.ceil(float(enemy.get("attack", 1)) * float(state.get("enemy_action_rules", {}).get("charge_damage_multiplier", 2.0))) if action == "charge_start" else 0}
        enemy["tactics"] = tactics
        enemies.append(enemy)
    result["enemies"] = enemies
    result["enemy_turn_forecast"] = {
        "source": "phase_attack_timer", "estimate_only": True,
        "limitations": "Shot-induced stuns, geometry and phase reinforcements are estimates; no hidden state is read.",
        "actions": [{"target_id": e.get("target_id"), "action": e["tactics"]["next_action_prediction"]["action"],
                     "damage_before_defense": e["forecast_attack_damage"]} for e in enemies]}
    return result


def attack_damage(enemy, defense):
    if not enemy.get("can_attack_this_turn", True):
        return 0.0
    amount = enemy.get("forecast_attack_damage", enemy.get("tactics", {}).get("defeat_prevention", {}).get("immediate_attack_damage", 0))
    if amount <= 0:
        amount = enemy.get("attack", 1)
    return max(1.0, float(amount) - defense)
