#!/usr/bin/env python3
"""Aggregate progression, encounter, damage-source and tactical-AI metrics."""

from __future__ import annotations

import json
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ABILITY_IDS = {
    "protection_resonance": "enemy_protection_resonator",
    "swarm_amplifier": "enemy_swarm_amplifier",
    "impact_brittle": "enemy_impact_brittle",
}


def _records(directory: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in (directory / "all_runs.jsonl").read_text(
        encoding="utf-8").splitlines() if line.strip()]


def _rate(part: int | float, total: int | float) -> float:
    return part / total if total else 0.0


def _rounded(value: float) -> float:
    return round(value, 4)


def analyze(directory: Path) -> dict[str, Any]:
    records = _records(directory)
    progress: dict[int, Counter] = defaultdict(Counter)
    stages: dict[str, Counter] = defaultdict(Counter)
    compositions: dict[tuple[str, ...], Counter] = defaultdict(Counter)
    abilities: dict[str, Counter] = defaultdict(Counter)
    damage_sources: Counter = Counter()
    damage_enemy_ids: Counter = Counter()
    action_plans: Counter = Counter()
    builds: dict[str, Counter] = defaultdict(Counter)
    decisions: Counter = Counter()
    next_prediction_error = 0.0
    future_prediction_error = 0.0
    prediction_samples = 0
    total_shots = 0
    successful_shots = 0
    actual_chains = 0
    actual_damage = 0.0

    for record in records:
        build = str(record.get("build_profile", "unknown"))
        builds[build]["runs"] += 1
        builds[build]["wins"] += record.get("run_result") == "completed"
        builds[build]["boss_reached"] += bool(record.get("final_boss_reached"))
        source = json.loads(Path(record["source_log"]).read_text(encoding="utf-8"))
        for event in source.get("events", []):
            if event.get("event_type") != "enemy_action_plan":
                continue
            details = event.get("details", {})
            action_plans["plans"] += 1
            action_plans["normal_attack"] += int(
                details.get("normal_attack_count", 0))
            action_plans["charge_start"] += int(
                details.get("charge_start_count", 0))
            action_plans["charge_attack"] += int(
                details.get("charge_attack_count", 0))

        for stage in source.get("stages", []):
            context = stage.get("stage_context", {})
            result = stage.get("stage_result", {})
            p = int(context.get("progress", 0))
            clear = result.get("result") == "clear"
            start_hp = float(stage.get("stage_start", {}).get("player_hp", 0))
            end_hp = float(result.get("player_hp", 0))
            consumed = float(result.get("player_damage_taken", 0))
            row = progress[p]
            row["reached"] += 1
            row["cleared"] += clear
            row["lost"] += not clear
            row["start_hp"] += start_hp
            row["end_hp"] += end_hp
            row["hp_consumed"] += consumed
            row["turns"] += int(result.get("turn_count", 0))
            row["shots"] += int(result.get("total_shots", 0))

            stage_id = str(stage.get("stage_id", "unknown"))
            stage_row = stages[stage_id]
            stage_row["reached"] += 1
            stage_row["cleared"] += clear
            stage_row["lost"] += not clear
            stage_row["hp_consumed"] += consumed
            stage_row["shots"] += int(result.get("total_shots", 0))

            enemy_ids = tuple(sorted(
                str(enemy.get("id", "unknown"))
                for enemy in stage.get("stage_start", {}).get("enemies", [])))
            comp = compositions[enemy_ids]
            comp["reached"] += 1
            comp["cleared"] += clear
            comp["lost"] += not clear
            comp["hp_consumed"] += consumed
            for label, enemy_id in ABILITY_IDS.items():
                if enemy_id in enemy_ids:
                    ability = abilities[label]
                    ability["reached"] += 1
                    ability["cleared"] += clear
                    ability["lost"] += not clear
                    ability["hp_consumed"] += consumed

            for damage in stage.get("damage_events", []):
                amount = float(damage.get("damage", 0))
                damage_sources[str(damage.get("source", "unknown"))] += amount
                if damage.get("source_id"):
                    damage_enemy_ids[str(damage["source_id"])] += amount

            for shot in stage.get("shots", []):
                total_shots += 1
                successful_shots += (
                    int(shot.get("player_enemy_hit_count", 0))
                    + int(shot.get("enemy_enemy_hit_count", 0))) > 0
                chain = shot.get("physics_chain", {})
                actual_chains += int(chain.get(
                    "effective_enemy_enemy_collision_count", 0))
                actual_damage += float(shot.get("total_enemy_damage") or 0)
                decision = shot.get("decision_log", {})
                outcomes = decision.get("actual_tactical_outcomes", {})
                actual_outcome = sum(int(value) for value in outcomes.values())
                reasons = decision.get("decision_reasons", [])
                tactical_reason = any(any(token in str(reason) for token in (
                    "Charge", "防護", "群体", "脆化", "連鎖"))
                    for reason in reasons)
                decisions["tactical_reason_shots"] += tactical_reason
                decisions["tactical_without_outcome_or_kill"] += (
                    tactical_reason and actual_outcome == 0
                    and int(decision.get("actual_kills", 0)) == 0)
                decisions["charge_candidates_generated"] += int(
                    decision.get("charge_interruption_candidates_generated", 0))
                decisions["charge_candidates_adopted"] += bool(
                    decision.get("charge_interruption_candidate_adopted", False))
                decisions["charge_successes"] += bool(
                    decision.get("charge_interruption_success", False))
                if "predicted_next_turn_prevention_before_defense" in decision:
                    prediction_samples += 1
                    next_prediction_error += abs(float(decision.get(
                        "predicted_next_turn_prevention_before_defense", 0))
                        - float(decision.get(
                            "actual_next_turn_prevention_before_defense", 0)))
                    future_prediction_error += abs(float(decision.get(
                        "predicted_future_charge_prevention", 0))
                        - float(decision.get(
                            "actual_future_charge_prevention", 0)))

    def encounter_rows(source: dict[Any, Counter], composition=False):
        result: dict[str, Any] = {}
        for key, value in source.items():
            reached = value["reached"]
            label = "+".join(key) if composition else str(key)
            result[label] = {
                "reached": reached,
                "cleared": value["cleared"],
                "lost": value["lost"],
                "clear_rate": _rounded(_rate(value["cleared"], reached)),
                "average_hp_consumed": _rounded(_rate(
                    value["hp_consumed"], reached)),
                "average_shots": _rounded(_rate(value["shots"], reached)),
            }
        return result

    progress_rows = {}
    for key, value in sorted(progress.items()):
        reached = value["reached"]
        progress_rows[str(key)] = {
            "reached": reached,
            "cleared": value["cleared"],
            "lost": value["lost"],
            "clear_rate": _rounded(_rate(value["cleared"], reached)),
            "average_start_hp": _rounded(_rate(value["start_hp"], reached)),
            "average_end_hp": _rounded(_rate(value["end_hp"], reached)),
            "average_hp_consumed": _rounded(_rate(
                value["hp_consumed"], reached)),
            "average_turns": _rounded(_rate(value["turns"], reached)),
            "average_shots": _rounded(_rate(value["shots"], reached)),
        }

    run_count = len(records)
    wins = sum(record.get("run_result") == "completed" for record in records)
    boss_reached = sum(bool(record.get("final_boss_reached")) for record in records)
    boss_defeated = sum(bool(record.get("final_boss_defeated")) for record in records)
    return {
        "directory": str(directory),
        "variant": records[0].get("validation_variant", "") if records else "",
        "runs": run_count,
        "wins": wins,
        "win_rate": _rounded(_rate(wins, run_count)),
        "boss": {
            "reached": boss_reached,
            "reach_rate": _rounded(_rate(boss_reached, run_count)),
            "defeated": boss_defeated,
            "defeat_rate": _rounded(_rate(boss_defeated, run_count)),
            "lost_after_reaching": boss_reached - boss_defeated,
        },
        "progress": progress_rows,
        "stages": encounter_rows(stages),
        "enemy_compositions": encounter_rows(compositions, composition=True),
        "ability_involvement": encounter_rows(abilities),
        "damage_by_source": dict(damage_sources),
        "damage_by_enemy_id": dict(damage_enemy_ids),
        "enemy_action_plans": dict(action_plans),
        "build_survival": {
            key: {
                "runs": value["runs"],
                "wins": value["wins"],
                "win_rate": _rounded(_rate(value["wins"], value["runs"])),
                "boss_reach_rate": _rounded(_rate(
                    value["boss_reached"], value["runs"])),
            }
            for key, value in builds.items()
        },
        "shots": {
            "total": total_shots,
            "successful": successful_shots,
            "success_rate": _rounded(_rate(successful_shots, total_shots)),
            "actual_chains": actual_chains,
            "chains_per_shot": _rounded(_rate(actual_chains, total_shots)),
            "actual_damage": _rounded(actual_damage),
            "damage_per_shot": _rounded(_rate(actual_damage, total_shots)),
        },
        "tactical_decisions": {
            **dict(decisions),
            "prevention_prediction_samples": prediction_samples,
            "next_turn_prevention_mae": _rounded(_rate(
                next_prediction_error, prediction_samples)),
            "future_charge_prevention_mae": _rounded(_rate(
                future_prediction_error, prediction_samples)),
        },
    }


def main(arguments: list[str]) -> int:
    if not arguments or any("=" not in argument for argument in arguments):
        print("Expected LABEL=COMPARISON_DIRECTORY arguments", file=sys.stderr)
        return 2
    output = {}
    for argument in arguments:
        label, directory = argument.split("=", 1)
        output[label] = analyze(Path(directory))
    print(json.dumps(output, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
