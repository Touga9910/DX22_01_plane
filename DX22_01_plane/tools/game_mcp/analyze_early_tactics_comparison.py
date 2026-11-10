#!/usr/bin/env python3
"""Aggregate the A/B/C/D early-stage tactical comparison logs.

Usage:
  python analyze_early_tactics_comparison.py \
      A=C:/.../comparison_A B=C:/.../comparison_B

Each directory must contain all_runs.jsonl.  The referenced source run logs are
used for per-progress HP consumption, shot success, and tactical interference.
The JSON result is written to stdout so the same report can be regenerated.
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


TACTICAL_KEYS = (
    "charge_interruptions",
    "protection_removals",
    "swarm_attack_reduction",
    "impact_brittle_attack_reduction",
)


def _read_json_lines(path: Path) -> list[dict[str, Any]]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def _as_number(value: Any) -> float:
    return float(value) if isinstance(value, (int, float)) else 0.0


def analyze_condition(directory: Path) -> dict[str, Any]:
    records = _read_json_lines(directory / "all_runs.jsonl")
    if not records:
        raise ValueError(f"No accepted runs in {directory}")

    wins = sum(record.get("run_result") == "completed" for record in records)
    early_losses = sum(
        record.get("run_result") != "completed"
        and int(record.get("final_progress", 0)) <= 3
        for record in records
    )
    boss_reached = sum(bool(record.get("final_boss_reached")) for record in records)
    total_shots = 0
    successful_shots = 0
    total_chains = 0
    predicted_chains = 0
    expected_prevention = 0.0
    actual_prevention = 0.0
    tactical_counts = {key: 0 for key in TACTICAL_KEYS}
    hp_by_progress: dict[int, list[float]] = defaultdict(list)

    for record in records:
        source_path = Path(str(record["source_log"]))
        source = json.loads(source_path.read_text(encoding="utf-8"))
        for stage in source.get("stages", []):
            progress = int(stage.get("stage_context", {}).get("progress", 0))
            if 1 <= progress <= 6:
                hp_by_progress[progress].append(
                    _as_number(stage.get("stage_result", {}).get("player_damage_taken"))
                )
            for shot in stage.get("shots", []):
                total_shots += 1
                hit_count = int(shot.get("player_enemy_hit_count", 0)) + int(
                    shot.get("enemy_enemy_hit_count", 0)
                )
                successful_shots += hit_count > 0
                chain = shot.get("physics_chain", {})
                total_chains += int(chain.get("effective_enemy_enemy_collision_count", 0))
                decision = shot.get("decision_log", {})
                predicted_chains += int(decision.get("predicted_chain_count", 0))
                expected_prevention += _as_number(
                    decision.get("predicted_attack_prevention")
                )
                actual_prevention += _as_number(
                    decision.get("actual_attack_prevention")
                )
                actual_tactics = decision.get("actual_tactical_outcomes", {})
                for key in TACTICAL_KEYS:
                    tactical_counts[key] += int(actual_tactics.get(key, 0))

    run_count = len(records)
    return {
        "validation_variant": records[0].get("validation_variant", ""),
        "runs": run_count,
        "wins": wins,
        "win_rate": wins / run_count,
        "losses_at_progress_3_or_earlier": early_losses,
        "final_boss_reached": boss_reached,
        "final_boss_reach_rate": boss_reached / run_count,
        "shot_success": {
            "successful_shots": successful_shots,
            "total_shots": total_shots,
            "rate": successful_shots / total_shots if total_shots else 0.0,
        },
        "chains": {
            "predicted": predicted_chains,
            "actual": total_chains,
            "actual_per_run": total_chains / run_count,
        },
        "attack_prevention": {
            "predicted_damage": expected_prevention,
            "actual_damage": actual_prevention,
        },
        "tactical_interference": {
            **tactical_counts,
            "total": sum(tactical_counts.values()),
        },
        "hp_consumption_by_progress": {
            str(progress): {
                "encounters": len(values),
                "total": sum(values),
                "average": sum(values) / len(values),
            }
            for progress, values in sorted(hp_by_progress.items())
        },
    }


def main(arguments: list[str]) -> int:
    if not arguments or any("=" not in argument for argument in arguments):
        print("Expected one or more LABEL=COMPARISON_DIRECTORY arguments", file=sys.stderr)
        return 2
    result: dict[str, Any] = {}
    for argument in arguments:
        label, directory_text = argument.split("=", 1)
        result[label] = analyze_condition(Path(directory_text))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
