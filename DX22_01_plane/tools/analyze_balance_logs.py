"""ラン単位のバランスログを集計し、各ステージを採点する。

ゲームプロジェクトのディレクトリから実行する:
    python tools/analyze_balance_logs.py
"""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from balance_diagnostics import calculate_balance_diagnostics
from balance_playtest_feedback import aggregate_feedback
from balance_statistics import (
    classify_interval,
    mean_interval,
    median_interval,
    rounded_interval,
    wilson_interval,
)
from balance_system_metrics import calculate_system_metrics


DEFAULT_LOG_DIRECTORY = Path("logs/balance")
DEFAULT_TARGET_FILE = Path("assets/data/balance_targets.json")
DEFAULT_OUTPUT_FILE = Path("logs/balance/balance_report.json")
DEFAULT_FEEDBACK_DIRECTORY = Path("logs/balance_feedback")


@dataclass(frozen=True)
class RunFilters:
    """比較可能な1つのバランスコホートを定義する、ラン単位の条件。"""

    controller_types: tuple[str, ...] = ()
    controller_profiles: tuple[str, ...] = ()
    build_profiles: tuple[str, ...] = ()
    experiment_ids: tuple[str, ...] = ()
    validation_variants: tuple[str, ...] = ()
    dynamic_balance_enabled: bool | None = None
    configuration_fingerprints: tuple[str, ...] = ()


LoadedRun = tuple[Path, dict[str, Any], dict[str, Any]]
TERMINAL_RUN_RESULTS = {
    "clear",
    "game_over",
    "validation_complete",
    "victory",
}


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
    parser.add_argument(
        "--feedback",
        type=Path,
        default=DEFAULT_FEEDBACK_DIRECTORY,
        help="Directory containing optional human playtest feedback.",
    )
    parser.add_argument(
        "--controller-type",
        action="append",
        default=[],
        help="Include only this controller type. May be repeated.",
    )
    parser.add_argument(
        "--controller-profile",
        action="append",
        default=[],
        help="Include only this MCP player profile. May be repeated.",
    )
    parser.add_argument(
        "--build-profile",
        action="append",
        default=[],
        help="Include only this MCP build profile. May be repeated.",
    )
    parser.add_argument(
        "--experiment-id",
        action="append",
        default=[],
        help="Include only this validation experiment ID. May be repeated.",
    )
    parser.add_argument(
        "--validation-variant",
        action="append",
        default=[],
        help="Include only this validation variant ID. May be repeated.",
    )
    parser.add_argument(
        "--dynamic-balance",
        choices=("enabled", "disabled"),
        help="Include only runs that started with DDA enabled or disabled.",
    )
    configuration_group = parser.add_mutually_exclusive_group()
    configuration_group.add_argument(
        "--configuration-fingerprint",
        action="append",
        default=[],
        help=(
            "Include only this configuration-suite fingerprint or an "
            "unambiguous prefix. May be repeated."
        ),
    )
    configuration_group.add_argument(
        "--latest-configuration",
        action="store_true",
        help=(
            "Use the configuration fingerprint from the newest readable "
            "run log and exclude other configurations."
        ),
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def merge_metric_targets(
    base_metrics: dict[str, Any],
    metric_overrides: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    """ステージ別またはプロフィール別の部分的な目標上書きを統合する。"""

    merged: dict[str, dict[str, Any]] = {}
    for metric_name, base_target in base_metrics.items():
        if not isinstance(base_target, dict):
            continue
        target = dict(base_target)
        override = metric_overrides.get(metric_name, {})
        if isinstance(override, dict):
            target.update(override)
        merged[metric_name] = target
    return merged


def resolve_stage_metric_targets(
    targets: dict[str, Any],
    stage_type: str,
) -> dict[str, dict[str, Any]]:
    base_metrics = targets.get("metrics", {})
    if not isinstance(base_metrics, dict):
        return {}
    stage_type_targets = targets.get("stage_type_targets", {})
    if not isinstance(stage_type_targets, dict):
        stage_type_targets = {}
    stage_override = stage_type_targets.get(stage_type, {})
    if not isinstance(stage_override, dict):
        stage_override = {}
    metric_overrides = stage_override.get("metrics", {})
    if not isinstance(metric_overrides, dict):
        metric_overrides = {}
    return merge_metric_targets(base_metrics, metric_overrides)


def resolve_run_metric_targets(
    targets: dict[str, Any],
    selected_profile: str,
) -> tuple[str, dict[str, dict[str, Any]], int]:
    run_targets = targets.get("run_targets", {})
    if not isinstance(run_targets, dict):
        return "", {}, int(targets.get("minimum_sample_count", 30))

    default_profile = str(
        run_targets.get("default_profile", "intermediate")
    )
    target_profile = selected_profile or default_profile
    base_metrics = run_targets.get("metrics", {})
    if not isinstance(base_metrics, dict):
        base_metrics = {}

    profile_overrides = run_targets.get("profile_overrides", {})
    if not isinstance(profile_overrides, dict):
        profile_overrides = {}
    profile_override = profile_overrides.get(target_profile, {})
    if not isinstance(profile_override, dict):
        profile_override = {}
    metric_overrides = profile_override.get("metrics", {})
    if not isinstance(metric_overrides, dict):
        metric_overrides = {}

    return (
        target_profile,
        merge_metric_targets(base_metrics, metric_overrides),
        int(
            profile_override.get(
                "minimum_sample_count",
                run_targets.get("minimum_sample_count", 30),
            )
        ),
    )


def configuration_fingerprint(run: dict[str, Any]) -> str:
    """ログに記録した設定一式から、安定したSHA-256指紋を返す。"""

    configuration = run.get("configuration", {})
    if not isinstance(configuration, dict):
        return ""
    files = configuration.get("files", [])
    if not isinstance(files, list):
        return ""

    normalized_files: list[dict[str, Any]] = []
    for entry in files:
        if not isinstance(entry, dict):
            continue
        path = str(entry.get("path", ""))
        if not path:
            continue
        normalized_files.append(
            {
                "path": path.replace("\\", "/").lower(),
                "exists": bool(entry.get("exists", False)),
                "fnv1a64": str(entry.get("fnv1a64", "")).lower(),
            }
        )

    if not normalized_files:
        return ""
    normalized_files.sort(key=lambda entry: entry["path"])
    payload = json.dumps(
        normalized_files,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def extract_run_filter_values(run: dict[str, Any]) -> dict[str, Any]:
    controller = run.get("controller", {})
    if not isinstance(controller, dict):
        controller = {}
    run_context = run.get("run_context", {})
    if not isinstance(run_context, dict):
        run_context = {}
    validation = run_context.get("validation", {})
    if not isinstance(validation, dict):
        validation = {}

    dynamic_balance = run_context.get("dynamic_balance_enabled_at_start")
    if not isinstance(dynamic_balance, bool):
        dynamic_balance = None

    return {
        "controller_type": str(
            controller.get(
                "type",
                run.get("controller_type", "unknown"),
            )
        ),
        "controller_profile": str(
            controller.get(
                "profile",
                run_context.get("controller_profile", ""),
            )
        ),
        "build_profile": str(
            controller.get(
                "build_profile",
                run_context.get("build_profile", ""),
            )
        ),
        "build_profile_settings_hash": str(
            controller.get(
                "build_profile_settings_hash",
                run_context.get("build_profile_settings_hash", ""),
            )
        ),
        "experiment_id": str(validation.get("experiment_id", "")),
        "validation_variant": str(validation.get("variant_id", "")),
        "dynamic_balance_enabled": dynamic_balance,
        "configuration_fingerprint": configuration_fingerprint(run),
    }


def _filter_reasons(
    values: dict[str, Any],
    filters: RunFilters,
) -> list[str]:
    reasons: list[str] = []
    checks = (
        (
            "controller_type",
            values["controller_type"],
            filters.controller_types,
        ),
        (
            "controller_profile",
            values["controller_profile"],
            filters.controller_profiles,
        ),
        (
            "build_profile",
            values["build_profile"],
            filters.build_profiles,
        ),
        (
            "experiment_id",
            values["experiment_id"],
            filters.experiment_ids,
        ),
        (
            "validation_variant",
            values["validation_variant"],
            filters.validation_variants,
        ),
    )
    for name, actual, allowed in checks:
        if allowed and actual not in allowed:
            reasons.append(name)

    expected_dda = filters.dynamic_balance_enabled
    if expected_dda is not None:
        if values["dynamic_balance_enabled"] is not expected_dda:
            reasons.append("dynamic_balance")

    requested_fingerprints = filters.configuration_fingerprints
    if requested_fingerprints:
        actual_fingerprint = values["configuration_fingerprint"]
        if not actual_fingerprint or not any(
            actual_fingerprint.startswith(prefix.lower())
            for prefix in requested_fingerprints
        ):
            reasons.append("configuration_fingerprint")
    return reasons


def _filters_to_json(filters: RunFilters) -> dict[str, Any]:
    return {
        "controller_types": list(filters.controller_types),
        "controller_profiles": list(filters.controller_profiles),
        "build_profiles": list(filters.build_profiles),
        "experiment_ids": list(filters.experiment_ids),
        "validation_variants": list(filters.validation_variants),
        "dynamic_balance_enabled": filters.dynamic_balance_enabled,
        "configuration_fingerprints": list(
            filters.configuration_fingerprints
        ),
    }


def select_run_logs(
    log_directory: Path,
    filters: RunFilters | None = None,
    latest_configuration: bool = False,
) -> tuple[list[LoadedRun], dict[str, Any]]:
    """ログを読み込み、比較可能なコホートを1つ選び、除外内容を報告する。"""

    requested_filters = filters or RunFilters()
    loaded: list[LoadedRun] = []
    invalid_file_count = 0

    log_paths = (
        sorted(log_directory.glob("run_*.json"))
        if log_directory.exists()
        else []
    )
    for log_path in log_paths:
        try:
            run = load_json(log_path)
        except (OSError, json.JSONDecodeError) as error:
            print(f"[skip] {log_path}: {error}")
            invalid_file_count += 1
            continue
        loaded.append((log_path, run, extract_run_filter_values(run)))

    resolved_latest_fingerprint = ""
    resolved_latest_source = ""
    effective_filters = requested_filters
    if latest_configuration:
        for log_path, _, values in reversed(loaded):
            if _filter_reasons(values, requested_filters):
                continue
            fingerprint = str(values["configuration_fingerprint"])
            if fingerprint:
                resolved_latest_fingerprint = fingerprint
                resolved_latest_source = str(log_path)
                break
        effective_filters = RunFilters(
            controller_types=requested_filters.controller_types,
            controller_profiles=requested_filters.controller_profiles,
            build_profiles=requested_filters.build_profiles,
            experiment_ids=requested_filters.experiment_ids,
            validation_variants=requested_filters.validation_variants,
            dynamic_balance_enabled=(
                requested_filters.dynamic_balance_enabled
            ),
            configuration_fingerprints=(resolved_latest_fingerprint,),
        )

    selected: list[LoadedRun] = []
    exclusion_counts: Counter[str] = Counter()
    observed_configurations: Counter[str] = Counter()
    selected_configurations: Counter[str] = Counter()
    selected_build_profiles: Counter[str] = Counter()
    selected_build_hashes: Counter[str] = Counter()
    for item in loaded:
        values = item[2]
        fingerprint = str(values["configuration_fingerprint"])
        observed_configurations[fingerprint or "missing"] += 1
        reasons = _filter_reasons(values, effective_filters)
        if reasons:
            exclusion_counts.update(reasons)
            continue
        selected.append(item)
        selected_configurations[fingerprint or "missing"] += 1
        selected_build_profiles[
            str(values["build_profile"]) or "missing"
        ] += 1
        selected_build_hashes[
            str(values["build_profile_settings_hash"]) or "missing"
        ] += 1

    warning = ""
    if len(selected_configurations) > 1:
        warning = (
            "Selected runs contain multiple configuration cohorts. Use "
            "--latest-configuration or --configuration-fingerprint before "
            "making balance decisions."
        )
    if latest_configuration and not resolved_latest_fingerprint:
        warning = (
            "No readable run contained configuration fingerprints; no run "
            "can match --latest-configuration."
        )

    summary = {
        "discovered_file_count": len(log_paths),
        "readable_file_count": len(loaded),
        "invalid_file_count": invalid_file_count,
        "matched_file_count": len(selected),
        "excluded_file_count": len(loaded) - len(selected),
        "excluded_by_condition": dict(sorted(exclusion_counts.items())),
        "requested_filters": _filters_to_json(requested_filters),
        "effective_filters": _filters_to_json(effective_filters),
        "latest_configuration_requested": latest_configuration,
        "resolved_latest_configuration_fingerprint": (
            resolved_latest_fingerprint
        ),
        "resolved_latest_configuration_source": resolved_latest_source,
        "observed_configuration_fingerprints": dict(
            sorted(observed_configurations.items())
        ),
        "selected_configuration_fingerprints": dict(
            sorted(selected_configurations.items())
        ),
        "selected_build_profiles": dict(
            sorted(selected_build_profiles.items())
        ),
        "selected_build_profile_settings_hashes": dict(
            sorted(selected_build_hashes.items())
        ),
        "warning": warning,
    }
    return selected, summary


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


def resolve_statistical_settings(targets: dict[str, Any]) -> dict[str, Any]:
    settings = targets.get("statistical_decision", {})
    if not isinstance(settings, dict):
        settings = {}
    return {
        "enabled": bool(settings.get("enabled", False)),
        "confidence_level": min(
            0.999,
            max(0.50, float(settings.get("confidence_level", 0.95))),
        ),
        "additional_batch_size": max(
            1,
            int(settings.get("additional_batch_size", 10)),
        ),
        "maximum_sample_count": max(
            1,
            int(settings.get("maximum_sample_count", 100)),
        ),
    }


def _stage_metric_intervals(
    records: list[dict[str, Any]],
    clear_records: list[dict[str, Any]],
    total_shots_per_stage: list[int],
    shots: list[dict[str, Any]],
    confidence_level: float,
) -> dict[str, tuple[tuple[float, float], int]]:
    return {
        "clear_rate": (
            wilson_interval(
                len(clear_records),
                len(records),
                confidence_level,
            ),
            len(records),
        ),
        "median_shots": (
            median_interval(total_shots_per_stage, confidence_level),
            len(total_shots_per_stage),
        ),
        "average_remaining_hp_ratio": (
            mean_interval(
                (
                    float(record["result"].get("remaining_hp_ratio", 0.0))
                    for record in clear_records
                ),
                confidence_level,
                lower_bound=0.0,
                upper_bound=1.0,
            ),
            len(clear_records),
        ),
        "no_hit_shot_rate": (
            wilson_interval(
                sum(int(shot.get("collision_effect", 0)) == 0 for shot in shots),
                len(shots),
                confidence_level,
            ),
            len(shots),
        ),
        "average_collision_effect": (
            mean_interval(
                (
                    float(shot.get("collision_effect", 0.0))
                    for shot in shots
                ),
                confidence_level,
                lower_bound=0.0,
            ),
            len(shots),
        ),
    }


def collect_filtered_stage_records(
    log_directory: Path,
    filters: RunFilters | None = None,
    latest_configuration: bool = False,
) -> tuple[
    dict[tuple[str, int], list[dict[str, Any]]],
    int,
    dict[str, Any],
    list[LoadedRun],
]:
    grouped: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    selected_runs, selection_summary = select_run_logs(
        log_directory,
        filters=filters,
        latest_configuration=latest_configuration,
    )

    for log_path, run, filter_values in selected_runs:
        run_id = str(run.get("run_id", log_path.stem))

        controller_type = str(filter_values["controller_type"])
        run_context = run.get("run_context", {})
        if not isinstance(run_context, dict):
            run_context = {}
        validation = run_context.get("validation", {})
        if not isinstance(validation, dict):
            validation = {}

        for stage in run.get("stages", []):
            if not isinstance(stage, dict):
                continue
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
                        filter_values["controller_profile"]
                    ),
                    "build_profile": str(filter_values["build_profile"]),
                    "validation_enabled": bool(
                        validation.get("enabled", False)
                    ),
                }
            )

    return grouped, len(selected_runs), selection_summary, selected_runs


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

    statistical_settings = resolve_statistical_settings(targets)
    metric_intervals = _stage_metric_intervals(
        records,
        clear_records,
        total_shots_per_stage,
        shots,
        float(statistical_settings["confidence_level"]),
    )

    metric_scores: dict[str, float] = {}
    metric_status: dict[str, dict[str, Any]] = {}
    out_of_range_metrics: list[str] = []
    hard_gate_failures: list[str] = []
    statistically_out_of_range_metrics: list[str] = []
    inconclusive_metrics: list[str] = []
    hard_gate_inconclusive_metrics: list[str] = []
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
    stage_metric_targets = resolve_stage_metric_targets(
        targets,
        stage_type,
    )

    for metric_name, value in values.items():
        metric_target = stage_metric_targets[metric_name]
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
        uses_statistical_gate = bool(
            statistical_settings["enabled"]
            and is_hard_gate
            and metric_target.get("statistical_gate", True)
        )
        in_range = range_status == "within_target"
        interval, interval_sample_count = metric_intervals[metric_name]
        statistical_status = classify_interval(interval, lower, upper)
        metric_status[metric_name] = {
            "status": range_status,
            "in_range": in_range,
            "hard_gate": is_hard_gate,
            "statistical_gate": uses_statistical_gate,
            "target_min": lower,
            "target_max": upper,
            "distance_from_range": round(distance_from_range, 4),
            "confidence_interval": {
                **rounded_interval(interval),
                "confidence_level": statistical_settings[
                    "confidence_level"
                ],
                "sample_count": interval_sample_count,
            },
            "statistical_status": statistical_status,
        }
        if not in_range:
            out_of_range_metrics.append(metric_name)
        if bool(statistical_settings["enabled"]):
            if statistical_status == "inconclusive":
                inconclusive_metrics.append(metric_name)
                if uses_statistical_gate:
                    hard_gate_inconclusive_metrics.append(metric_name)
            elif statistical_status != "within_target":
                statistically_out_of_range_metrics.append(metric_name)
                if uses_statistical_gate:
                    hard_gate_failures.append(metric_name)
        if not uses_statistical_gate and not in_range and is_hard_gate:
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
    elif hard_gate_inconclusive_metrics:
        judgement = "inconclusive"
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
    build_profile_counts = Counter(
        record.get("build_profile", "")
        for record in records
        if record.get("build_profile")
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
        "target_scope": {
            "level": "stage_type",
            "stage_type": stage_type,
            "used_stage_type_override": stage_type in targets.get(
                "stage_type_targets",
                {},
            ),
        },
        "clear_count": len(clear_records),
        "total_shot_count": len(shots),
        "metrics": {
            name: round(value, 4)
            for name, value in values.items()
        },
        "metric_scores": metric_scores,
        "metric_status": metric_status,
        "out_of_range_metrics": out_of_range_metrics,
        "statistically_out_of_range_metrics": (
            statistically_out_of_range_metrics
        ),
        "hard_gate": {
            "passed": (
                not hard_gate_failures
                and not hard_gate_inconclusive_metrics
            ),
            "failed_metrics": hard_gate_failures,
            "inconclusive_metrics": hard_gate_inconclusive_metrics,
        },
        "statistical_decision": {
            "enabled": statistical_settings["enabled"],
            "confidence_level": statistical_settings["confidence_level"],
            "inconclusive_metrics": inconclusive_metrics,
            "additional_samples_recommended": (
                min(
                    statistical_settings["additional_batch_size"],
                    max(
                        0,
                        statistical_settings["maximum_sample_count"]
                        - sample_count,
                    ),
                )
                if statistical_settings["enabled"]
                and hard_gate_inconclusive_metrics
                else 0
            ),
            "maximum_sample_count": statistical_settings[
                "maximum_sample_count"
            ],
        },
        "condition_breakdown": {
            "controller_type": dict(sorted(controller_counts.items())),
            "controller_profile": dict(
                sorted(controller_profile_counts.items())
            ),
            "build_profile": dict(sorted(build_profile_counts.items())),
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


def calculate_run_report(
    selected_runs: list[LoadedRun],
    targets: dict[str, Any],
) -> dict[str, Any]:
    """ラン進行をエンカウントのバランスとは分けて評価する。"""

    selected_profiles = sorted({
        str(values.get("controller_profile", ""))
        for _, _, values in selected_runs
        if values.get("controller_profile")
    })
    selected_build_profiles = sorted({
        str(values.get("build_profile", ""))
        for _, _, values in selected_runs
        if values.get("build_profile")
    })
    selected_profile = (
        selected_profiles[0] if len(selected_profiles) == 1 else ""
    )
    target_profile, metric_targets, minimum_sample_count = (
        resolve_run_metric_targets(
            targets,
            selected_profile,
        )
    )

    result_counts: Counter[str] = Counter()
    terminal_runs: list[dict[str, Any]] = []
    for _, run, _ in selected_runs:
        run_result = run.get("run_result", {})
        if not isinstance(run_result, dict):
            run_result = {}
        result_name = str(run_result.get("result", ""))
        result_counts[result_name or "missing"] += 1
        if result_name in TERMINAL_RUN_RESULTS:
            terminal_runs.append(run)

    reached_progress_values: list[int] = []
    first_boss_reached_count = 0
    first_boss_cleared_count = 0
    second_boss_reached_count = 0
    bosses_cleared_values: list[int] = []
    for run in terminal_runs:
        run_result = run.get("run_result", {})
        if not isinstance(run_result, dict):
            run_result = {}
        stages = [
            stage
            for stage in run.get("stages", [])
            if isinstance(stage, dict)
        ]
        fallback_progress = max(
            (
                int(stage.get("stage_context", {}).get("progress", 0))
                for stage in stages
                if isinstance(stage.get("stage_context", {}), dict)
            ),
            default=0,
        )
        reached_progress_values.append(
            int(
                run_result.get(
                    "reached_stage_index",
                    fallback_progress,
                )
            )
        )

        boss_stages = [
            stage
            for stage in stages
            if str(stage.get("stage_type", "")) == "boss"
        ]
        cleared_boss_count = sum(
            isinstance(stage.get("stage_result"), dict)
            and stage["stage_result"].get("result") == "clear"
            for stage in boss_stages
        )
        bosses_cleared_values.append(cleared_boss_count)
        if boss_stages:
            first_boss_reached_count += 1
        if cleared_boss_count >= 1:
            first_boss_cleared_count += 1
        if len(boss_stages) >= 2:
            second_boss_reached_count += 1

    selected_count = len(selected_runs)
    terminal_count = len(terminal_runs)
    values = {
        "first_boss_reach_rate": (
            first_boss_reached_count / terminal_count
            if terminal_count
            else 0.0
        ),
        "first_boss_clear_rate": (
            first_boss_cleared_count / terminal_count
            if terminal_count
            else 0.0
        ),
        "second_boss_reach_rate": (
            second_boss_reached_count / terminal_count
            if terminal_count
            else 0.0
        ),
        "median_reached_progress": (
            float(statistics.median(reached_progress_values))
            if reached_progress_values
            else 0.0
        ),
        "terminal_run_rate": (
            terminal_count / selected_count if selected_count else 0.0
        ),
    }

    statistical_settings = resolve_statistical_settings(targets)
    confidence_level = float(statistical_settings["confidence_level"])
    metric_intervals: dict[str, tuple[tuple[float, float], int]] = {
        "first_boss_reach_rate": (
            wilson_interval(
                first_boss_reached_count,
                terminal_count,
                confidence_level,
            ),
            terminal_count,
        ),
        "first_boss_clear_rate": (
            wilson_interval(
                first_boss_cleared_count,
                terminal_count,
                confidence_level,
            ),
            terminal_count,
        ),
        "second_boss_reach_rate": (
            wilson_interval(
                second_boss_reached_count,
                terminal_count,
                confidence_level,
            ),
            terminal_count,
        ),
        "median_reached_progress": (
            median_interval(reached_progress_values, confidence_level),
            len(reached_progress_values),
        ),
        "terminal_run_rate": (
            wilson_interval(
                terminal_count,
                selected_count,
                confidence_level,
            ),
            selected_count,
        ),
    }

    metric_scores: dict[str, float] = {}
    metric_status: dict[str, dict[str, Any]] = {}
    out_of_range_metrics: list[str] = []
    hard_gate_failures: list[str] = []
    neutral_failures: list[str] = []
    statistically_out_of_range_metrics: list[str] = []
    inconclusive_metrics: list[str] = []
    hard_gate_inconclusive_metrics: list[str] = []
    weighted_score = 0.0
    total_weight = 0.0
    difficulty_bias = 0.0

    for metric_name, metric_target in metric_targets.items():
        if metric_name not in values:
            continue
        value = values[metric_name]
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
        uses_statistical_gate = bool(
            statistical_settings["enabled"]
            and is_hard_gate
            and metric_target.get("statistical_gate", True)
        )
        direction = str(
            metric_target.get("difficulty_direction", "neutral")
        )
        in_range = range_status == "within_target"
        interval, interval_sample_count = metric_intervals[metric_name]
        statistical_status = classify_interval(interval, lower, upper)
        metric_status[metric_name] = {
            "status": range_status,
            "in_range": in_range,
            "hard_gate": is_hard_gate,
            "statistical_gate": uses_statistical_gate,
            "difficulty_direction": direction,
            "target_min": lower,
            "target_max": upper,
            "distance_from_range": round(distance_from_range, 4),
            "confidence_interval": {
                **rounded_interval(interval),
                "confidence_level": statistical_settings[
                    "confidence_level"
                ],
                "sample_count": interval_sample_count,
            },
            "statistical_status": statistical_status,
        }
        if not in_range:
            out_of_range_metrics.append(metric_name)
        if bool(statistical_settings["enabled"]):
            if statistical_status == "inconclusive":
                inconclusive_metrics.append(metric_name)
                if uses_statistical_gate:
                    hard_gate_inconclusive_metrics.append(metric_name)
            elif statistical_status != "within_target":
                statistically_out_of_range_metrics.append(metric_name)
                if uses_statistical_gate:
                    hard_gate_failures.append(metric_name)
                if direction == "neutral":
                    neutral_failures.append(metric_name)
        if not uses_statistical_gate and not in_range:
            if is_hard_gate:
                hard_gate_failures.append(metric_name)
            if direction == "neutral":
                neutral_failures.append(metric_name)

        metric_scores[metric_name] = round(score, 2)
        weighted_score += score * weight
        total_weight += weight
        if direction != "neutral":
            difficulty_bias += (
                signed_deviation(
                    value,
                    metric_target,
                    direction == "easier",
                )
                * weight
            )

    balance_score = (
        weighted_score / total_weight if total_weight > 0.0 else 0.0
    )
    adjustment_threshold = float(targets["adjustment_score_threshold"])
    balanced_threshold = float(targets["balanced_score_threshold"])
    if terminal_count < minimum_sample_count:
        judgement = "insufficient_data"
        adjustment_required = False
    elif hard_gate_inconclusive_metrics:
        judgement = "inconclusive"
        adjustment_required = False
    elif balance_score >= balanced_threshold and not hard_gate_failures:
        judgement = "balanced"
        adjustment_required = False
    elif neutral_failures and abs(difficulty_bias) <= 0.05:
        judgement = "data_quality_issue"
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
        terminal_count / max(minimum_sample_count, 1),
    )
    adjustment_priority = (
        (100.0 - balance_score) * confidence
        if adjustment_required
        else 0.0
    )
    return {
        "sample_count": selected_count,
        "balance_sample_count": terminal_count,
        "minimum_sample_count": minimum_sample_count,
        "excluded_incomplete_run_count": selected_count - terminal_count,
        "selected_controller_profiles": selected_profiles,
        "selected_build_profiles": selected_build_profiles,
        "target_scope": {
            "level": "run",
            "profile": target_profile,
            "used_default_profile": not selected_profile,
        },
        "result_counts": dict(sorted(result_counts.items())),
        "milestones": {
            "first_boss_reached_count": first_boss_reached_count,
            "first_boss_cleared_count": first_boss_cleared_count,
            "second_boss_reached_count": second_boss_reached_count,
            "average_bosses_cleared": round(
                statistics.fmean(bosses_cleared_values)
                if bosses_cleared_values
                else 0.0,
                4,
            ),
        },
        "metrics": {
            name: round(value, 4)
            for name, value in values.items()
        },
        "metric_scores": metric_scores,
        "metric_status": metric_status,
        "out_of_range_metrics": out_of_range_metrics,
        "statistically_out_of_range_metrics": (
            statistically_out_of_range_metrics
        ),
        "hard_gate": {
            "passed": (
                not hard_gate_failures
                and not hard_gate_inconclusive_metrics
            ),
            "failed_metrics": hard_gate_failures,
            "inconclusive_metrics": hard_gate_inconclusive_metrics,
        },
        "statistical_decision": {
            "enabled": statistical_settings["enabled"],
            "confidence_level": statistical_settings["confidence_level"],
            "inconclusive_metrics": inconclusive_metrics,
            "additional_samples_recommended": (
                min(
                    statistical_settings["additional_batch_size"],
                    max(
                        0,
                        statistical_settings["maximum_sample_count"]
                        - terminal_count,
                    ),
                )
                if statistical_settings["enabled"]
                and hard_gate_inconclusive_metrics
                else 0
            ),
            "maximum_sample_count": statistical_settings[
                "maximum_sample_count"
            ],
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

    dynamic_balance_enabled = None
    if args.dynamic_balance == "enabled":
        dynamic_balance_enabled = True
    elif args.dynamic_balance == "disabled":
        dynamic_balance_enabled = False
    filters = RunFilters(
        controller_types=tuple(args.controller_type),
        controller_profiles=tuple(args.controller_profile),
        build_profiles=tuple(args.build_profile),
        experiment_ids=tuple(args.experiment_id),
        validation_variants=tuple(args.validation_variant),
        dynamic_balance_enabled=dynamic_balance_enabled,
        configuration_fingerprints=tuple(
            fingerprint.lower()
            for fingerprint in args.configuration_fingerprint
        ),
    )
    grouped, analyzed_file_count, run_selection, selected_runs = (
        collect_filtered_stage_records(
            args.logs,
            filters=filters,
            latest_configuration=args.latest_configuration,
        )
    )
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
    run_report = calculate_run_report(selected_runs, targets)
    system_metrics = calculate_system_metrics(selected_runs)
    diagnostics = calculate_balance_diagnostics(selected_runs, targets)
    human_feedback = aggregate_feedback(
        args.feedback,
        {
            str(run.get("run_id", path.stem))
            for path, run, _ in selected_runs
        },
    )

    report = {
        "schema_version": 7,
        "generated_at": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "analyzed_file_count": analyzed_file_count,
        "evaluated_stage_count": len(stage_reports),
        "targets_file": str(args.targets),
        "run_selection": run_selection,
        "run_balance": run_report,
        "system_metrics": system_metrics,
        "diagnostics": diagnostics,
        "human_feedback": human_feedback,
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
    if run_selection["warning"]:
        print(f"[warning] {run_selection['warning']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
