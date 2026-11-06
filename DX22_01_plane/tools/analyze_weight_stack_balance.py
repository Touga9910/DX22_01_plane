"""Recalculate Heavy distance-stack candidates from existing per-enemy telemetry."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


DISTANCE_CANDIDATES = (5.0, 20.0, 40.0, 60.0)
TURN_GAIN_LIMIT = 10


def read(path: Path) -> dict:
    with path.open("r", encoding="utf-8-sig") as source:
        return json.load(source)


def write(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )


def calculate_gain(
    enemy_distances: list[float],
    distance_per_stack: float,
    lightweight_gain: int = 0,
    turn_limit: int = TURN_GAIN_LIMIT,
) -> dict:
    if not math.isfinite(distance_per_stack) or distance_per_stack <= 0.0:
        raise ValueError("distance_per_stack must be finite and positive")
    per_enemy = [
        max(0, math.floor(max(0.0, float(distance)) / distance_per_stack))
        for distance in enemy_distances
    ]
    distance_uncapped = sum(per_enemy)
    micro_uncapped = max(0, int(lightweight_gain))
    combined_uncapped = distance_uncapped + micro_uncapped
    actual = min(max(0, int(turn_limit)), combined_uncapped)
    return {
        "per_enemy": per_enemy,
        "distance_uncapped": distance_uncapped,
        "lightweight_uncapped": micro_uncapped,
        "combined_uncapped": combined_uncapped,
        "actual": actual,
        "discarded_by_turn_limit": combined_uncapped - actual,
        "discarded_by_storage_limit": 0,
    }


def expected_logged_distance_gain(shot: dict) -> int:
    if "heavy_stacks_uncapped_gain" in shot:
        return int(shot["heavy_stacks_uncapped_gain"])
    return int(shot["heavy_stacks_generated"])


def summarize(values: list[int]) -> dict:
    return {
        "sample_count": len(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "minimum": min(values),
        "maximum": max(values),
        "turn_limit_reached_count": sum(value >= TURN_GAIN_LIMIT for value in values),
        "turn_limit_reached_ratio": sum(value >= TURN_GAIN_LIMIT for value in values) / len(values),
        "mean_actual": statistics.fmean(min(value, TURN_GAIN_LIMIT) for value in values),
        "mean_discarded": statistics.fmean(max(0, value - TURN_GAIN_LIMIT) for value in values),
    }


def pearson(left: list[float], right: list[float]) -> float | None:
    left_mean = statistics.fmean(left)
    right_mean = statistics.fmean(right)
    left_delta = [value - left_mean for value in left]
    right_delta = [value - right_mean for value in right]
    denominator = math.sqrt(
        sum(value * value for value in left_delta)
        * sum(value * value for value in right_delta)
    )
    if denominator <= 0.0:
        return None
    return sum(
        left_value * right_value
        for left_value, right_value in zip(left_delta, right_delta)
    ) / denominator


def build_row(source: Path, result: dict) -> dict:
    case = result["case"]
    shot = result["shot"]
    chain = shot["physics_chain"]
    enemy_distances = [
        float(enemy["distance_xz"])
        for enemy in chain["enemy_distances"]
    ]
    gains = {
        str(int(distance)): calculate_gain(enemy_distances, distance)
        for distance in DISTANCE_CANDIDATES
    }
    logged_distance_gain = expected_logged_distance_gain(shot)
    if gains["5"]["distance_uncapped"] != logged_distance_gain:
        raise AssertionError(
            f"{case['id']}: distance-5 recalculation "
            f"{gains['5']['distance_uncapped']} does not match logged "
            f"{logged_distance_gain}"
        )

    return {
        "source": str(source),
        "case_id": case["id"],
        "seed": result.get("seed"),
        "enemy_count": int(shot["initial_enemy_count"]),
        "layout": case.get("layout", "unknown"),
        "hp_multiplier": float(case.get("hp", 1.0)),
        "upgrade_level": int(case.get("upgrade", shot["upgrade_level"])),
        "direction_strategy": case.get("direction", "nearest"),
        "direction_x": float(shot["direction_x"]),
        "direction_z": float(shot["direction_z"]),
        "power": float(shot["power"]),
        "effective_collisions": int(chain["effective_enemy_enemy_collision_count"]),
        "unique_pairs": int(chain["unique_enemy_pair_count"]),
        "involved_enemies": int(chain["involved_enemy_count"]),
        "total_enemy_distance_xz": float(chain["total_enemy_distance_xz"]),
        "defeated_enemies": int(shot["enemies_defeated"]),
        "lightweight_uncapped": 0,
        "turn_gain_limit": TURN_GAIN_LIMIT,
        "gains": gains,
    }


def grouped_statistics(rows: list[dict], field: str) -> dict:
    groups: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        groups[str(row[field])].append(row)
    return {
        key: {
            threshold: summarize([
                row["gains"][threshold]["distance_uncapped"]
                for row in group
            ])
            for threshold in ("5", "20", "40", "60")
        }
        for key, group in sorted(groups.items())
    }


def write_csv(path: Path, rows: list[dict]) -> None:
    fixed_fields = [
        "case_id", "seed", "enemy_count", "layout", "hp_multiplier",
        "upgrade_level", "direction_strategy", "direction_x", "direction_z",
        "power", "effective_collisions", "unique_pairs", "involved_enemies",
        "total_enemy_distance_xz", "defeated_enemies", "lightweight_uncapped",
    ]
    gain_fields = [
        f"distance_{distance}_{kind}"
        for distance in (5, 20, 40, 60)
        for kind in ("uncapped", "actual", "discarded")
    ]
    with path.open("w", encoding="utf-8-sig", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fixed_fields + gain_fields)
        writer.writeheader()
        for row in rows:
            flat = {field: row[field] for field in fixed_fields}
            for distance in (5, 20, 40, 60):
                gain = row["gains"][str(distance)]
                flat[f"distance_{distance}_uncapped"] = gain["distance_uncapped"]
                flat[f"distance_{distance}_actual"] = gain["actual"]
                flat[f"distance_{distance}_discarded"] = gain[
                    "discarded_by_turn_limit"
                ]
            writer.writerow(flat)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--exclude-case", action="append", default=[])
    args = parser.parse_args()

    excluded = set(args.exclude_case)
    rows = [
        build_row(path, result)
        for path in args.inputs
        for result in read(path).get("results", [])
        if result["case"]["id"] not in excluded
    ]
    if not rows:
        parser.error("No measurements were found.")

    summary = {
        "distance_candidates": DISTANCE_CANDIDATES,
        "turn_gain_limit": TURN_GAIN_LIMIT,
        "calculation": "sum(floor(each_enemy_distance_xz / distance_per_stack))",
        "lightweight_note": "All analyzed shots use Heavy distance generation; lightweight gain is kept as a separate zero-valued field.",
        "sample_count": len(rows),
        "overall": {
            threshold: summarize([
                row["gains"][threshold]["distance_uncapped"]
                for row in rows
            ])
            for threshold in ("5", "20", "40", "60")
        },
        "by_enemy_count": grouped_statistics(rows, "enemy_count"),
        "by_layout": grouped_statistics(rows, "layout"),
        "by_upgrade_level": grouped_statistics(rows, "upgrade_level"),
        "by_power": grouped_statistics(rows, "power"),
        "by_direction_strategy": grouped_statistics(rows, "direction_strategy"),
        "quality_correlations": {
            threshold: {
                metric: {
                    "uncapped": pearson(
                        [float(row["gains"][threshold]["distance_uncapped"]) for row in rows],
                        [float(row[metric]) for row in rows],
                    ),
                    "actual": pearson(
                        [float(row["gains"][threshold]["actual"]) for row in rows],
                        [float(row[metric]) for row in rows],
                    ),
                }
                for metric in (
                    "effective_collisions",
                    "unique_pairs",
                    "involved_enemies",
                    "total_enemy_distance_xz",
                    "defeated_enemies",
                )
            }
            for threshold in ("5", "20", "40", "60")
        },
        "rows": rows,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write(args.output_dir / "weight_stack_balance_analysis.json", summary)
    write_csv(args.output_dir / "weight_stack_balance_analysis.csv", rows)
    print(json.dumps(summary["overall"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
