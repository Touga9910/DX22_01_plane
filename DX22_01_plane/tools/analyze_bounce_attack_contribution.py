"""Aggregate reflection-axis attack timing from deterministic capture logs."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
import json
import math
from pathlib import Path
from statistics import mean


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def capture_shots(root: Path) -> list[dict]:
    shots: list[dict] = []
    for case_dir in sorted(path for path in root.iterdir() if path.is_dir()):
        logs = sorted((case_dir / "logs" / "debug_battle").glob("*.json"))
        if len(logs) != 1:
            raise RuntimeError(f"Expected one debug log in {case_dir}, got {logs}")
        run = read_json(logs[0])
        for stage in run.get("stages", []):
            for shot in stage.get("shots", []):
                copy = dict(shot)
                copy["capture_case"] = case_dir.name
                copy["capture_root"] = root.name
                shots.append(copy)
    return shots


def reflection_bin(count: int) -> str:
    if count == 0:
        return "0"
    if count <= 2:
        return "1-2"
    if count <= 4:
        return "3-4"
    if count <= 6:
        return "5-6"
    return "7+"


def phase(shot: dict, name: str) -> dict:
    return shot["bounce_axis"]["attack_timing"][name]


def correlation(xs: list[float], ys: list[float]) -> float | None:
    if len(xs) != len(ys) or len(xs) < 2:
        return None
    x_mean = mean(xs)
    y_mean = mean(ys)
    numerator = sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys))
    x_square = sum((x - x_mean) ** 2 for x in xs)
    y_square = sum((y - y_mean) ** 2 for y in ys)
    if x_square <= 0.0 or y_square <= 0.0:
        return None
    return numerator / math.sqrt(x_square * y_square)


def summarize_group(shots: list[dict]) -> dict:
    after = [phase(shot, "after_first_reflection") for shot in shots]
    before = [phase(shot, "before_first_reflection") for shot in shots]
    reflected = [
        shot for shot in shots
        if shot["bounce_axis"]["physical_reflection_count"] > 0
    ]
    succeeded = [
        shot for shot in reflected
        if phase(shot, "after_first_reflection")["effective_attack_count"] > 0
    ]
    return {
        "shot_count": len(shots),
        "reflected_shot_count": len(reflected),
        "post_reflection_success_shot_count": len(succeeded),
        "post_reflection_zero_effective_attack_shot_count":
            len(reflected) - len(succeeded),
        "post_reflection_success_rate":
            len(succeeded) / len(reflected) if reflected else None,
        "before_reflection": {
            "attack_check_count": sum(item["attack_check_count"] for item in before),
            "hit_count": sum(item["hit_count"] for item in before),
            "effective_attack_count": sum(
                item["effective_attack_count"] for item in before
            ),
            "applied_hp_damage": sum(item["applied_hp_damage"] for item in before),
            "unique_effective_target_count_sum": sum(
                item["unique_effective_target_count"] for item in before
            ),
        },
        "after_reflection": {
            "attack_check_count": sum(item["attack_check_count"] for item in after),
            "hit_count": sum(item["hit_count"] for item in after),
            "effective_attack_count": sum(
                item["effective_attack_count"] for item in after
            ),
            "applied_hp_damage": sum(item["applied_hp_damage"] for item in after),
            "unique_effective_target_count_sum": sum(
                item["unique_effective_target_count"] for item in after
            ),
        },
        "average_post_reflection_effective_attacks_per_shot": (
            sum(item["effective_attack_count"] for item in after) / len(shots)
            if shots else 0.0
        ),
        "average_post_reflection_applied_damage_per_shot": (
            sum(item["applied_hp_damage"] for item in after) / len(shots)
            if shots else 0.0
        ),
        "average_shot_duration_ms": (
            mean(shot["bounce_axis"]["shot_duration_ms"] for shot in shots)
            if shots else 0.0
        ),
    }


def attack_sources(shot: dict) -> dict:
    result: dict[str, dict] = {}
    timeline = shot["bounce_axis"]["attack_timeline"]
    damage_events = shot.get("enemy_damage_events", [])
    sources = [
        damage_events[index]["source"]
        if index < len(damage_events)
        else event["source"]
        for index, event in enumerate(timeline)
    ]
    for source in sorted(set(sources)):
        events = [
            event for event, event_source in zip(timeline, sources)
            if event_source == source
        ]
        result[source] = {
            "attack_check_count": len(events),
            "hit_count": sum(event["target_was_alive"] for event in events),
            "effective_attack_count": sum(
                event["applied_hp_damage"] > 0 for event in events
            ),
            "applied_hp_damage": sum(
                event["applied_hp_damage"] for event in events
            ),
        }
    return result


def target_repeats(shot: dict) -> list[dict]:
    effective = Counter(
        event["target_index"]
        for event in shot["bounce_axis"]["attack_timeline"]
        if event["applied_hp_damage"] > 0
    )
    checks = Counter(
        event["target_index"]
        for event in shot["bounce_axis"]["attack_timeline"]
    )
    return [
        {
            "target_index": target,
            "attack_check_count": checks[target],
            "effective_attack_count": effective[target],
        }
        for target in sorted(checks)
    ]


def extreme_case(shot: dict) -> dict:
    physics = shot["physics_chain"]
    bounce = shot["bounce_axis"]
    velocity = shot["velocity"]
    speed = math.hypot(velocity[0], velocity[2])
    return {
        "capture_case": shot["capture_case"],
        "shot_no": shot["shot_no"],
        "ball_id": shot["ball_id"],
        "upgrade_level": shot["upgrade_level"],
        "power": shot["power"],
        "start_position_xyz": shot["position"],
        "direction_xz": [
            velocity[0] / speed if speed else 0.0,
            velocity[2] / speed if speed else 0.0,
        ],
        "initial_reflection_stacks": bounce["reflection_stacks_before"],
        "ending_reflection_stacks": bounce["reflection_stacks_after"],
        "physical_reflection_count": bounce["physical_reflection_count"],
        "effective_attack_count": bounce["attacks"]["effective_attack_count"],
        "applied_hp_damage": bounce["attacks"]["applied_hp_damage"],
        "shot_duration_ms": bounce["shot_duration_ms"],
        "attack_timing": bounce["attack_timing"],
        "attack_sources": attack_sources(shot),
        "per_target_repetition": target_repeats(shot),
        "enemy_enemy_collision_count":
            physics["effective_enemy_enemy_collision_count"],
        "wall_reflections": physics["wall_reflections"],
        "enemy_initial_state_and_movement": physics["enemy_distances"],
        "enemy_minimum_distance_from_player_path": None,
        "enemy_minimum_distance_note":
            "Not observed by the existing telemetry; no inferred value is filled in.",
    }


def pinball_upgrades(shots: list[dict]) -> list[dict]:
    ready = [
        shot for shot in shots
        if shot["ball_id"] == "player_ricochet_finisher"
        and shot["capture_case"].endswith("_ready")
    ]
    return [
        {
            "upgrade_level": shot["upgrade_level"],
            "physical_reflection_count":
                shot["bounce_axis"]["physical_reflection_count"],
            "post_reflection_effective_attack_count":
                phase(shot, "after_first_reflection")["effective_attack_count"],
            "effective_attack_count":
                shot["bounce_axis"]["attacks"]["effective_attack_count"],
            "applied_hp_damage":
                shot["bounce_axis"]["attacks"]["applied_hp_damage"],
            "pinball_damage_event_count":
                shot["bounce_axis"]["pinball_damage_event_count"],
            "pinball_maximum_damage_stage":
                shot["bounce_axis"]["pinball_maximum_damage_stage"],
            "pinball_applied_bonus_damage":
                shot["bounce_axis"]["pinball_applied_bonus_damage"],
            "reflection_stacks_consumed":
                shot["bounce_axis"]["reflection_stacks_consumed"],
            "shot_duration_ms": shot["bounce_axis"]["shot_duration_ms"],
        }
        for shot in sorted(ready, key=lambda item: item["upgrade_level"])
    ]


def pinball_activation_cases(shots: list[dict]) -> list[dict]:
    finishers = [
        shot for shot in shots
        if shot["ball_id"] == "player_ricochet_finisher"
    ]
    return [
        {
            "capture_case": shot["capture_case"],
            "upgrade_level": shot["upgrade_level"],
            "stacks_before": shot["bounce_axis"]["reflection_stacks_before"],
            "activation_attempt_count":
                shot["bounce_axis"]["pinball_activation_attempt_count"],
            "activation_count": shot["bounce_axis"]["pinball_activation_count"],
            "fizzle_count": shot["bounce_axis"]["pinball_fizzle_count"],
            "stacks_consumed":
                shot["bounce_axis"]["reflection_stacks_consumed"],
            "stacks_after": shot["bounce_axis"]["reflection_stacks_after"],
        }
        for shot in sorted(finishers, key=lambda item: item["capture_case"])
    ]


def economy(measurements: dict) -> list[dict]:
    rows = []
    for result in measurements["results"]:
        turns = result["turns"]
        telemetry = [turn["telemetry"] for turn in turns]
        opportunities = sum(
            turn["ball_id"] == "player_ricochet_finisher" for turn in turns
        )
        rows.append({
            "scenario_id": result["scenario_id"],
            **result["summary"],
            "maximum_balance": max(
                [0] + [item["reflection_stacks_after"] for item in telemetry]
            ),
            "consumption_opportunity_count": opportunities,
            "activation_count": sum(
                item["pinball_activation_count"] for item in telemetry
            ),
            "fizzle_count": sum(item["pinball_fizzle_count"] for item in telemetry),
            "turn_balances": [
                {
                    "turn": turn["turn"],
                    "ball_id": turn["ball_id"],
                    "before": turn["telemetry"]["reflection_stacks_before"],
                    "generated": turn["telemetry"]["reflection_stacks_generated"],
                    "consumed": turn["telemetry"]["reflection_stacks_consumed"],
                    "after": turn["telemetry"]["reflection_stacks_after"],
                }
                for turn in turns
            ],
        })
    return rows


def measurement_telemetry(path: Path) -> list[dict]:
    result = []
    for item in read_json(path)["results"]:
        if "telemetry" in item:
            result.append(item["telemetry"])
        else:
            result.extend(turn["telemetry"] for turn in item.get("turns", []))
    return result


def parity(current: list[Path], baseline: list[Path]) -> dict:
    current_rows = [item for path in current for item in measurement_telemetry(path)]
    baseline_rows = [item for path in baseline for item in measurement_telemetry(path)]
    fields = [
        "physical_reflection_count",
        "effective_reflection_count",
        "reflection_stacks_generated",
        "reflection_stacks_consumed",
    ]
    mismatches = []
    for index, (new, old) in enumerate(zip(current_rows, baseline_rows)):
        for field in fields:
            if new[field] != old[field]:
                mismatches.append({
                    "shot_index": index,
                    "field": field,
                    "baseline": old[field],
                    "current": new[field],
                })
        for field in ("effective_attack_count", "applied_hp_damage"):
            if new["attacks"][field] != old["attacks"][field]:
                mismatches.append({
                    "shot_index": index,
                    "field": f"attacks.{field}",
                    "baseline": old["attacks"][field],
                    "current": new["attacks"][field],
                })
    return {
        "baseline_shot_count": len(baseline_rows),
        "current_shot_count": len(current_rows),
        "all_compared_fields_equal":
            len(current_rows) == len(baseline_rows) and not mismatches,
        "mismatches": mismatches,
    }


def runtime_log_parity(current: list[dict], baseline: list[dict]) -> dict:
    mismatches = []
    for index, (new, old) in enumerate(zip(current, baseline)):
        integer_fields = [
            ("bounce_axis", "physical_reflection_count"),
            ("bounce_axis", "effective_reflection_count"),
            ("bounce_axis", "reflection_stacks_generated"),
            ("bounce_axis", "reflection_stacks_consumed"),
            ("physics_chain", "effective_enemy_enemy_collision_count"),
        ]
        for section, field in integer_fields:
            if new[section][field] != old[section][field]:
                mismatches.append({
                    "shot_index": index,
                    "field": f"{section}.{field}",
                    "baseline": old[section][field],
                    "current": new[section][field],
                })
        for field in ("effective_attack_count", "applied_hp_damage"):
            if new["bounce_axis"]["attacks"][field] != old["bounce_axis"]["attacks"][field]:
                mismatches.append({
                    "shot_index": index,
                    "field": f"bounce_axis.attacks.{field}",
                    "baseline": old["bounce_axis"]["attacks"][field],
                    "current": new["bounce_axis"]["attacks"][field],
                })
        for field in ("total_enemy_distance_xz", "maximum_enemy_distance_xz"):
            new_value = float(new["physics_chain"][field])
            old_value = float(old["physics_chain"][field])
            if not math.isclose(new_value, old_value, rel_tol=1e-6, abs_tol=1e-5):
                mismatches.append({
                    "shot_index": index,
                    "field": f"physics_chain.{field}",
                    "baseline": old_value,
                    "current": new_value,
                })
    return {
        "baseline_shot_count": len(baseline),
        "current_shot_count": len(current),
        "physical_damage_and_enemy_movement_equal":
            len(current) == len(baseline) and not mismatches,
        "mismatches": mismatches,
    }


def write_csv(path: Path, shots: list[dict]) -> None:
    fields = [
        "capture_root", "capture_case", "shot_no", "ball_id", "upgrade_level",
        "power", "physical_reflections", "before_attack_checks", "before_hits",
        "before_effective_attacks", "before_applied_damage", "after_attack_checks",
        "after_hits", "after_effective_attacks", "after_applied_damage",
        "after_unique_targets", "total_effective_attacks", "total_applied_damage",
        "reflection_derived_events", "stacks_before", "stacks_generated",
        "stacks_consumed", "stacks_after", "duration_ms",
    ]
    with path.open("w", encoding="utf-8-sig", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for shot in shots:
            bounce = shot["bounce_axis"]
            before = phase(shot, "before_first_reflection")
            after = phase(shot, "after_first_reflection")
            writer.writerow({
                "capture_root": shot["capture_root"],
                "capture_case": shot["capture_case"],
                "shot_no": shot["shot_no"],
                "ball_id": shot["ball_id"],
                "upgrade_level": shot["upgrade_level"],
                "power": shot["power"],
                "physical_reflections": bounce["physical_reflection_count"],
                "before_attack_checks": before["attack_check_count"],
                "before_hits": before["hit_count"],
                "before_effective_attacks": before["effective_attack_count"],
                "before_applied_damage": before["applied_hp_damage"],
                "after_attack_checks": after["attack_check_count"],
                "after_hits": after["hit_count"],
                "after_effective_attacks": after["effective_attack_count"],
                "after_applied_damage": after["applied_hp_damage"],
                "after_unique_targets": after["unique_effective_target_count"],
                "total_effective_attacks": bounce["attacks"]["effective_attack_count"],
                "total_applied_damage": bounce["attacks"]["applied_hp_damage"],
                "reflection_derived_events": sum(
                    event["reflection_derived"] for event in bounce["attack_timeline"]
                ),
                "stacks_before": bounce["reflection_stacks_before"],
                "stacks_generated": bounce["reflection_stacks_generated"],
                "stacks_consumed": bounce["reflection_stacks_consumed"],
                "stacks_after": bounce["reflection_stacks_after"],
                "duration_ms": bounce["shot_duration_ms"],
            })


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--single", type=Path, required=True)
    parser.add_argument("--pinball", type=Path, required=True)
    parser.add_argument("--economy", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, action="append", default=[])
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-csv", type=Path, required=True)
    args = parser.parse_args()

    roots = [args.single, args.pinball, args.economy]
    shots = [shot for root in roots for shot in capture_shots(root)]
    groups = {}
    for label in ("0", "1-2", "3-4", "5-6", "7+"):
        group = [
            shot for shot in shots
            if reflection_bin(shot["bounce_axis"]["physical_reflection_count"]) == label
        ]
        groups[label] = summarize_group(group)
    exact = {}
    for count in sorted({
        shot["bounce_axis"]["physical_reflection_count"] for shot in shots
    }):
        exact[str(count)] = summarize_group([
            shot for shot in shots
            if shot["bounce_axis"]["physical_reflection_count"] == count
        ])

    six_zero = next(
        shot for shot in shots
        if shot["bounce_axis"]["physical_reflection_count"] == 6
        and shot["bounce_axis"]["attacks"]["effective_attack_count"] == 0
    )
    zero_forty_two = next(
        shot for shot in shots
        if shot["bounce_axis"]["physical_reflection_count"] == 0
        and shot["bounce_axis"]["attacks"]["effective_attack_count"] == 42
    )
    reflection_counts = [
        shot["bounce_axis"]["physical_reflection_count"] for shot in shots
    ]
    total_attacks = [
        shot["bounce_axis"]["attacks"]["effective_attack_count"] for shot in shots
    ]
    total_damage = [
        shot["bounce_axis"]["attacks"]["applied_hp_damage"] for shot in shots
    ]
    after_attacks = [
        phase(shot, "after_first_reflection")["effective_attack_count"]
        for shot in shots
    ]
    after_damage = [
        phase(shot, "after_first_reflection")["applied_hp_damage"]
        for shot in shots
    ]
    timeline_events = [
        event
        for shot in shots
        for event in shot["bounce_axis"]["attack_timeline"]
    ]
    analysis = {
        "schema_version": 1,
        "sample": {
            "seed": 20260904,
            "shot_count": len(shots),
            "capture_roots": [str(root) for root in roots],
        },
        "overall": summarize_group(shots),
        "by_reflection_range": groups,
        "by_exact_reflection_count": exact,
        "correlations": {
            "physical_reflections_vs_total_effective_attacks":
                correlation(reflection_counts, total_attacks),
            "physical_reflections_vs_total_applied_damage":
                correlation(reflection_counts, total_damage),
            "physical_reflections_vs_post_reflection_effective_attacks":
                correlation(reflection_counts, after_attacks),
            "physical_reflections_vs_post_reflection_applied_damage":
                correlation(reflection_counts, after_damage),
        },
        "reflection_derived_attack_attributes": {
            "event_count": sum(event["reflection_derived"] for event in timeline_events),
            "effective_attack_count": sum(
                event["reflection_derived"] and event["applied_hp_damage"] > 0
                for event in timeline_events
            ),
            "attack_applied_hp_damage": sum(
                event["applied_hp_damage"]
                for event in timeline_events if event["reflection_derived"]
            ),
            "isolated_bonus_applied_hp_damage": sum(
                event["reflection_bonus_applied_hp_damage"] or 0
                for event in timeline_events
            ),
        },
        "extreme_cases": {
            "six_reflections_zero_effective_attacks": extreme_case(six_zero),
            "zero_reflections_forty_two_effective_attacks":
                extreme_case(zero_forty_two),
        },
        "pinball_upgrade_comparison": pinball_upgrades(
            [shot for shot in shots if shot["capture_root"] == args.pinball.name]
        ),
        "pinball_activation_cases": pinball_activation_cases(
            [shot for shot in shots if shot["capture_root"] == args.pinball.name]
        ),
        "economy": economy(read_json(args.economy / "measurements.json")),
    }
    if args.baseline:
        analysis["measurement_parity"] = parity(
            [root / "measurements.json" for root in roots],
            args.baseline,
        )
        baseline_shots = [
            shot
            for measurement_path in args.baseline
            for shot in capture_shots(measurement_path.parent)
        ]
        analysis["runtime_log_parity"] = runtime_log_parity(
            shots,
            baseline_shots,
        )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(
        json.dumps(analysis, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_csv(args.output_csv, shots)
    print(json.dumps({
        "shots": len(shots),
        "overall": analysis["overall"],
        "correlations": analysis["correlations"],
        "parity": analysis.get("measurement_parity"),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
