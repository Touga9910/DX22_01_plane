"""Aggregate per-run balance logs and score each stage.

Run from the game project directory:
    python tools/analyze_balance_logs.py
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_LOG_DIRECTORY = Path("logs/balance")
DEFAULT_TARGET_FILE = Path("assets/data/balance_targets.json")
DEFAULT_OUTPUT_FILE = Path("logs/balance/balance_report.json")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Aggregate run JSON files into a stage balance report."
    )
    parser.add_argument(
        "--logs",
        type=Path,
        default=DEFAULT_LOG_DIRECTORY,
        help="Directory containing run_*.json files.",
    )
    parser.add_argument(
        "--targets",
        type=Path,
        default=DEFAULT_TARGET_FILE,
        help="Balance target configuration JSON.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT_FILE,
        help="Output report JSON.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def band_score(value: float, target: dict[str, Any]) -> float:
    lower = float(target["min"])
    upper = float(target["max"])
    tolerance = float(target["tolerance"])

    if lower <= value <= upper:
        return 100.0

    distance = lower - value if value < lower else value - upper
    if tolerance <= 0.0:
        return 0.0

    return 100.0 * max(0.0, 1.0 - distance / tolerance)


def signed_deviation(
    value: float,
    target: dict[str, Any],
    higher_means_easier: bool,
) -> float:
    lower = float(target["min"])
    upper = float(target["max"])
    tolerance = max(float(target["tolerance"]), 0.000001)

    if lower <= value <= upper:
        return 0.0

    if value < lower:
        raw = -(lower - value) / tolerance
    else:
        raw = (value - upper) / tolerance

    raw = max(-1.0, min(1.0, raw))
    return raw if higher_means_easier else -raw


def collect_stage_records(
    log_directory: Path,
) -> tuple[dict[tuple[str, int], list[dict[str, Any]]], int]:
    grouped: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    analyzed_file_count = 0

    if not log_directory.exists():
        return grouped, analyzed_file_count

    for log_path in sorted(log_directory.glob("run_*.json")):
        try:
            run = load_json(log_path)
        except (OSError, json.JSONDecodeError) as error:
            print(f"[skip] {log_path}: {error}")
            continue

        analyzed_file_count += 1
        run_id = str(run.get("run_id", log_path.stem))

        for stage in run.get("stages", []):
            result = stage.get("stage_result")
            if not isinstance(result, dict):
                continue
            if result.get("result") not in {"clear", "game_over"}:
                continue

            stage_id = str(stage.get("stage_id", "unknown"))
            difficulty = int(stage.get("difficulty", 0))
            grouped[(stage_id, difficulty)].append(
                {
                    "run_id": run_id,
                    "stage": stage,
                    "result": result,
                }
            )

    return grouped, analyzed_file_count


def calculate_stage_report(
    stage_id: str,
    difficulty: int,
    records: list[dict[str, Any]],
    targets: dict[str, Any],
) -> dict[str, Any]:
    sample_count = len(records)
    clear_records = [
        record
        for record in records
        if record["result"].get("result") == "clear"
    ]

    total_shots_per_stage = [
        int(record["result"].get("total_shots", 0))
        for record in records
    ]
    shots = [
        shot
        for record in records
        for shot in record["stage"].get("shots", [])
    ]

    clear_rate = len(clear_records) / sample_count if sample_count else 0.0
    median_shots = (
        float(statistics.median(total_shots_per_stage))
        if total_shots_per_stage
        else 0.0
    )
    average_remaining_hp_ratio = (
        statistics.fmean(
            float(record["result"].get("remaining_hp_ratio", 0.0))
            for record in clear_records
        )
        if clear_records
        else 0.0
    )
    no_hit_shot_rate = (
        sum(int(shot.get("collision_effect", 0)) == 0 for shot in shots)
        / len(shots)
        if shots
        else 0.0
    )
    average_collision_effect = (
        statistics.fmean(
            float(shot.get("collision_effect", 0.0))
            for shot in shots
        )
        if shots
        else 0.0
    )

    values = {
        "clear_rate": clear_rate,
        "median_shots": median_shots,
        "average_remaining_hp_ratio": average_remaining_hp_ratio,
        "no_hit_shot_rate": no_hit_shot_rate,
        "average_collision_effect": average_collision_effect,
    }

    metric_scores: dict[str, float] = {}
    weighted_score = 0.0
    total_weight = 0.0
    difficulty_bias = 0.0

    higher_means_easier = {
        "clear_rate": True,
        "median_shots": False,
        "average_remaining_hp_ratio": True,
        "no_hit_shot_rate": False,
        "average_collision_effect": True,
    }

    for metric_name, value in values.items():
        metric_target = targets["metrics"][metric_name]
        weight = float(metric_target["weight"])
        score = band_score(value, metric_target)

        metric_scores[metric_name] = round(score, 2)
        weighted_score += score * weight
        total_weight += weight
        difficulty_bias += (
            signed_deviation(
                value,
                metric_target,
                higher_means_easier[metric_name],
            )
            * weight
        )

    balance_score = (
        weighted_score / total_weight if total_weight > 0.0 else 0.0
    )
    minimum_sample_count = int(targets["minimum_sample_count"])
    adjustment_threshold = float(targets["adjustment_score_threshold"])
    balanced_threshold = float(targets["balanced_score_threshold"])

    if sample_count < minimum_sample_count:
        judgement = "insufficient_data"
        adjustment_required = False
    elif balance_score >= balanced_threshold:
        judgement = "balanced"
        adjustment_required = False
    elif difficulty_bias > 0.05:
        judgement = "too_easy"
        adjustment_required = balance_score < adjustment_threshold
    elif difficulty_bias < -0.05:
        judgement = "too_difficult"
        adjustment_required = balance_score < adjustment_threshold
    else:
        judgement = "mixed_issues"
        adjustment_required = balance_score < adjustment_threshold

    confidence = min(
        1.0,
        sample_count / max(minimum_sample_count, 1),
    )
    adjustment_priority = (
        (100.0 - balance_score) * confidence
        if adjustment_required
        else 0.0
    )

    return {
        "stage_id": stage_id,
        "difficulty": difficulty,
        "sample_count": sample_count,
        "clear_count": len(clear_records),
        "total_shot_count": len(shots),
        "metrics": {
            name: round(value, 4)
            for name, value in values.items()
        },
        "metric_scores": metric_scores,
        "balance_score": round(balance_score, 2),
        "difficulty_bias": round(difficulty_bias, 4),
        "judgement": judgement,
        "adjustment_required": adjustment_required,
        "adjustment_priority": round(adjustment_priority, 2),
    }


def main() -> int:
    args = parse_arguments()

    try:
        targets = load_json(args.targets)
    except (OSError, json.JSONDecodeError) as error:
        print(f"Failed to read target file {args.targets}: {error}")
        return 1

    grouped, analyzed_file_count = collect_stage_records(args.logs)
    stage_reports = [
        calculate_stage_report(stage_id, difficulty, records, targets)
        for (stage_id, difficulty), records in sorted(grouped.items())
    ]

    report = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "analyzed_file_count": analyzed_file_count,
        "evaluated_stage_count": len(stage_reports),
        "targets_file": str(args.targets),
        "stages": stage_reports,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as destination:
        json.dump(report, destination, ensure_ascii=False, indent=2)
        destination.write("\n")

    print(
        f"Wrote {len(stage_reports)} stage evaluations "
        f"from {analyzed_file_count} run logs to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
