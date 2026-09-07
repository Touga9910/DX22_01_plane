"""Summarize one contiguous F8 autoplay session from balance run logs."""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from statistics import mean, median


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--logs", type=Path, default=Path("logs/balance"))
    parser.add_argument("--first", required=True, help="First run filename in the session")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    files = sorted(path for path in args.logs.glob("run_*.json") if path.name >= args.first)
    runs = [json.loads(path.read_text(encoding="utf-8-sig")) for path in files]
    runs = [run for run in runs if run.get("controller_type") == "autoplay"]
    terminal = [run for run in runs if run.get("run_result", {}).get("result") in {"completed", "game_over"}]

    results = Counter(run["run_result"]["result"] for run in runs)
    cleared = [run["run_result"]["cleared_stage_count"] for run in terminal]
    durations = [run["run_result"]["duration_ms"] / 1000 for run in terminal]
    final_hp = [run["run_result"]["player_hp"] for run in terminal]
    all_stages = [stage for run in terminal for stage in run.get("stages", [])]
    all_shots = [shot for stage in all_stages for shot in stage.get("shots", [])]

    ball = defaultdict(lambda: {"shots": 0, "damage": 0, "kills": 0, "no_hits": 0})
    for shot in all_shots:
        item = ball[shot.get("ball_id", "unknown")]
        item["shots"] += 1
        item["damage"] += shot.get("total_enemy_damage", 0)
        item["kills"] += shot.get("enemies_defeated", 0)
        item["no_hits"] += int(shot.get("total_enemy_damage", 0) <= 0)

    stage_by_index = defaultdict(lambda: {"attempts": 0, "clears": 0, "damage_taken": 0, "shots": 0})
    stage_by_id = defaultdict(lambda: {"attempts": 0, "clears": 0, "damage_taken": 0, "shots": 0})
    for stage in all_stages:
        index = str(stage.get("stage_index", 0))
        result = stage.get("stage_result", {})
        for item in (stage_by_index[index], stage_by_id[stage.get("stage_id", "unknown")]):
            item["attempts"] += 1
            item["clears"] += int(result.get("result") == "clear")
            item["damage_taken"] += result.get("player_damage_taken", 0)
            item["shots"] += result.get("total_shots", 0)
    for group in (stage_by_index, stage_by_id):
        for item in group.values():
            item["clear_rate"] = round(item["clears"] / item["attempts"], 4)
            item["average_damage_taken"] = round(item["damage_taken"] / item["attempts"], 2)
            item["average_shots"] = round(item["shots"] / item["attempts"], 2)

    reward_choices = Counter()
    upgrades = Counter()
    rests = 0
    boss_decisions = 0
    route_choices = Counter()
    relics = Counter()
    for run in terminal:
        for event in run.get("events", []):
            kind = event.get("event_type")
            details = event.get("details", {})
            if kind == "clear_reward_choice":
                reward_choices[details.get("reward", "unknown")] += 1
            elif kind == "ball_upgraded":
                upgrades[details.get("ball_id", "unknown")] += 1
            elif kind == "rest_heal":
                rests += 1
            elif kind == "boss_ai_decision":
                boss_decisions += 1
            elif kind == "route_choice":
                route_choices[details.get("selected_route", "unknown")] += 1
            elif kind == "relic_acquired":
                relics[details.get("relic_name", "unknown")] += 1

    summary = {
        "files": [path.name for path in files],
        "run_count": len(runs),
        "terminal_run_count": len(terminal),
        "result_counts": dict(results),
        "clear_rate": round(results["completed"] / len(terminal), 4) if terminal else 0,
        "cleared_battles": {
            "values": cleared,
            "average": round(mean(cleared), 2) if cleared else 0,
            "median": median(cleared) if cleared else 0,
        },
        "duration_seconds": {
            "average": round(mean(durations), 2) if durations else 0,
            "median": round(median(durations), 2) if durations else 0,
        },
        "average_terminal_hp": round(mean(final_hp), 2) if final_hp else 0,
        "shot_count": len(all_shots),
        "total_damage": sum(shot.get("total_enemy_damage", 0) for shot in all_shots),
        "no_hit_rate": round(sum(shot.get("total_enemy_damage", 0) <= 0 for shot in all_shots) / len(all_shots), 4) if all_shots else 0,
        "ball_performance": dict(ball),
        "stage_by_index": dict(stage_by_index),
        "stage_by_id": dict(stage_by_id),
        "reward_choices": dict(reward_choices),
        "upgrades": dict(upgrades),
        "rest_heal_count": rests,
        "boss_ai_decision_count": boss_decisions,
        "route_choices": dict(route_choices),
        "relic_acquisitions": dict(relics),
        "run_seeds": [run.get("run_context", {}).get("randomness", {}).get("run_seed") for run in runs],
        "configuration_fingerprints": sorted({run.get("configuration", {}).get("fingerprint", "") for run in runs}),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
