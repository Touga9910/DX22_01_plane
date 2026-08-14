"""Aggregate per-run balance logs and score each stage.

Run from the game project directory:
    python tools/analyze_balance_logs.py
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import Counter, defaultdict
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

        controller_type = str(run.get("controller_type", "unknown"))
        run_context = run.get("run_context", {})
        if not isinstance(run_context, dict):
            run_context = {}

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
                    "controller_type": controller_type,
                    "controller_profile": str(
                        run_context.get("controller_profile", "")
                    ),
                    "validation_enabled": bool(
                        run_context.get("validation", {}).get(
                            "enabled",
                            False,
                        )
                    ),
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
    stage_type = str(
        records[0]["stage"].get("stage_type", "unknown")
        if records
        else "unknown"
    )
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
    metric_status: dict[str, dict[str, Any]] = {}
    out_of_range_metrics: list[str] = []
    hard_gate_failures: list[str] = []
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

        lower = float(metric_target["min"])
        upper = float(metric_target["max"])
        if value < lower:
            range_status = "below_target"
            distance_from_range = lower - value
        elif value > upper:
            range_status = "above_target"
            distance_from_range = value - upper
        else:
            range_status = "within_target"
            distance_from_range = 0.0

        is_hard_gate = bool(metric_target.get("hard_gate", False))
        in_range = range_status == "within_target"
        metric_status[metric_name] = {
            "status": range_status,
            "in_range": in_range,
            "hard_gate": is_hard_gate,
            "target_min": lower,
            "target_max": upper,
            "distance_from_range": round(distance_from_range, 4),
        }
        if not in_range:
            out_of_range_metrics.append(metric_name)
            if is_hard_gate:
                hard_gate_failures.append(metric_name)

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
    minimum_by_stage_type = targets.get(
        "minimum_sample_count_by_stage_type",
        {},
    )
    minimum_sample_count = int(
        minimum_by_stage_type.get(
            stage_type,
            targets["minimum_sample_count"],
        )
        if isinstance(minimum_by_stage_type, dict)
        else targets["minimum_sample_count"]
    )
    adjustment_threshold = float(targets["adjustment_score_threshold"])
    balanced_threshold = float(targets["balanced_score_threshold"])

    if sample_count < minimum_sample_count:
        judgement = "insufficient_data"
        adjustment_required = False
    elif balance_score >= balanced_threshold and not hard_gate_failures:
        judgement = "balanced"
        adjustment_required = False
    elif difficulty_bias > 0.05:
        judgement = "too_easy"
        adjustment_required = (
            balance_score < adjustment_threshold
            or bool(hard_gate_failures)
        )
    elif difficulty_bias < -0.05:
        judgement = "too_difficult"
        adjustment_required = (
            balance_score < adjustment_threshold
            or bool(hard_gate_failures)
        )
    else:
        judgement = "mixed_issues"
        adjustment_required = (
            balance_score < adjustment_threshold
            or bool(hard_gate_failures)
        )

    confidence = min(
        1.0,
        sample_count / max(minimum_sample_count, 1),
    )
    adjustment_priority = (
        (100.0 - balance_score) * confidence
        if adjustment_required
        else 0.0
    )

    controller_counts = Counter(
        record["controller_type"] for record in records
    )
    controller_profile_counts = Counter(
        record["controller_profile"]
        for record in records
        if record["controller_profile"]
    )
    baseline_profile_counts: Counter[str] = Counter()
    assist_level_counts: Counter[str] = Counter()
    for record in records:
        stage_context = record["stage"].get("stage_context", {})
        if not isinstance(stage_context, dict):
            continue
        baseline = stage_context.get("baseline_difficulty", {})
        if isinstance(baseline, dict):
            baseline_profile_counts[str(
                baseline.get("profile", "unknown")
            )] += 1
        assist = stage_context.get("assist_mode", {})
        if isinstance(assist, dict):
            assist_key = (
                f"enabled:{bool(assist.get('enabled', False))}"
                f"/level:{int(assist.get('applied_level', 0))}"
            )
            assist_level_counts[assist_key] += 1

    return {
        "stage_id": stage_id,
        "stage_type": stage_type,
        "difficulty": difficulty,
        "sample_count": sample_count,
        "minimum_sample_count": minimum_sample_count,
        "clear_count": len(clear_records),
        "total_shot_count": len(shots),
        "metrics": {
            name: round(value, 4)
            for name, value in values.items()
        },
        "metric_scores": metric_scores,
        "metric_status": metric_status,
        "out_of_range_metrics": out_of_range_metrics,
        "hard_gate": {
            "passed": not hard_gate_failures,
            "failed_metrics": hard_gate_failures,
        },
        "condition_breakdown": {
            "controller_type": dict(sorted(controller_counts.items())),
            "controller_profile": dict(
                sorted(controller_profile_counts.items())
            ),
            "baseline_profile": dict(
                sorted(baseline_profile_counts.items())
            ),
            "assist_mode": dict(sorted(assist_level_counts.items())),
            "validation_sample_count": sum(
                bool(record["validation_enabled"])
                for record in records
            ),
        },
        "balance_score": round(balance_score, 2),
        "difficulty_bias": round(difficulty_bias, 4),
        "judgement": judgement,
        "adjustment_required": adjustment_required,
        "adjustment_priority": round(adjustment_priority, 2),
    }


def apply_boss_revaluation_gate(
    stage_reports: list[dict[str, Any]],
    grouped: dict[tuple[str, int], list[dict[str, Any]]],
    analyzed_file_count: int,
    targets: dict[str, Any],
) -> dict[str, Any]:
    settings = targets.get("boss_revaluation", {})
    if not isinstance(settings, dict):
        settings = {}
    minimum_reached_runs = max(
        1,
        int(settings.get("minimum_reached_runs", 10)),
    )
    boss_run_ids = {
        str(record["run_id"])
        for records in grouped.values()
        for record in records
        if record["stage"].get("stage_type") == "boss"
    }
    reached_run_count = len(boss_run_ids)
    eligible = reached_run_count >= minimum_reached_runs
    reach_rate = (
        reached_run_count / analyzed_file_count
        if analyzed_file_count
        else 0.0
    )
    for report in stage_reports:
        if report.get("stage_type") != "boss":
            continue
        report["boss_strength_evaluation"] = {
            "eligible": eligible,
            "reached_run_count": reached_run_count,
            "minimum_reached_runs": minimum_reached_runs,
        }
        if not eligible:
            report["judgement"] = "insufficient_boss_reach"
            report["adjustment_required"] = False
            report["adjustment_priority"] = 0.0
    return {
        "eligible": eligible,
        "reached_run_count": reached_run_count,
        "minimum_reached_runs": minimum_reached_runs,
        "reach_rate": round(reach_rate, 4),
        "strength_adjustment_locked": not eligible,
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
    boss_revaluation = apply_boss_revaluation_gate(
        stage_reports,
        grouped,
        analyzed_file_count,
        targets,
    )

    report = {
        "schema_version": 3,
        "generated_at": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "analyzed_file_count": analyzed_file_count,
        "evaluated_stage_count": len(stage_reports),
        "targets_file": str(args.targets),
        "boss_revaluation": boss_revaluation,
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
