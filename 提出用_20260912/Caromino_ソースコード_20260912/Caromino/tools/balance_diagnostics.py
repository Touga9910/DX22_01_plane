"""失敗分類と、フロア単位の難易度曲線を診断する。"""

from __future__ import annotations

import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


LoadedRun = tuple[Path, dict[str, Any], dict[str, Any]]


def _stage_progress(stage: dict[str, Any]) -> int:
    context = stage.get("stage_context", {})
    if isinstance(context, dict) and "progress" in context:
        return int(context.get("progress", 0))
    return int(stage.get("stage_index", 0))


def _stage_result(stage: dict[str, Any]) -> dict[str, Any]:
    result = stage.get("stage_result", {})
    return result if isinstance(result, dict) else {}


def _stage_damage_ratio(stage: dict[str, Any]) -> float:
    result = _stage_result(stage)
    start = stage.get("stage_start", {})
    if not isinstance(start, dict):
        start = {}
    maximum_hp = max(
        1,
        int(result.get("player_max_hp", start.get("player_max_hp", 1))),
    )
    return float(result.get("player_damage_taken", 0)) / maximum_hp


def _median_shot_upper_target(
    stage_type: str,
    targets: dict[str, Any],
) -> float:
    metrics = targets.get("metrics", {})
    if not isinstance(metrics, dict):
        metrics = {}
    target = metrics.get("median_shots", {})
    if not isinstance(target, dict):
        target = {}
    stage_targets = targets.get("stage_type_targets", {})
    if isinstance(stage_targets, dict):
        override = stage_targets.get(stage_type, {})
        if isinstance(override, dict):
            override_metrics = override.get("metrics", {})
            if isinstance(override_metrics, dict):
                override_target = override_metrics.get("median_shots", {})
                if isinstance(override_target, dict):
                    target = {**target, **override_target}
    return float(target.get("max", 0.0))


def _failure_taxonomy(
    selected_runs: list[LoadedRun],
    targets: dict[str, Any],
) -> dict[str, Any]:
    diagnostics = targets.get("diagnostics", {})
    if not isinstance(diagnostics, dict):
        diagnostics = {}
    settings = diagnostics.get("failure_taxonomy", {})
    if not isinstance(settings, dict):
        settings = {}
    burst_ratio = float(settings.get("burst_damage_hp_ratio", 0.35))
    long_battle_multiplier = float(
        settings.get("long_battle_shot_multiplier", 1.5)
    )

    primary_counts: Counter[str] = Counter()
    label_counts: Counter[str] = Counter()
    source_counts: Counter[str] = Counter()
    progress_counts: Counter[str] = Counter()
    stage_type_counts: Counter[str] = Counter()
    examples: list[dict[str, Any]] = []

    for path, run, _ in selected_runs:
        run_result = run.get("run_result", {})
        if not isinstance(run_result, dict):
            continue
        if str(run_result.get("result", "")) != "game_over":
            continue
        failed_stages = [
            stage
            for stage in run.get("stages", [])
            if isinstance(stage, dict)
            and _stage_result(stage).get("result") == "game_over"
        ]
        if not failed_stages:
            primary_counts["unknown"] += 1
            label_counts["unknown"] += 1
            continue
        stage = failed_stages[-1]
        result = _stage_result(stage)
        stage_type = str(stage.get("stage_type", "unknown"))
        progress = _stage_progress(stage)
        start = stage.get("stage_start", {})
        if not isinstance(start, dict):
            start = {}
        maximum_hp = max(
            1,
            int(result.get("player_max_hp", start.get("player_max_hp", 1))),
        )
        damages = [
            event
            for event in stage.get("damage_events", [])
            if isinstance(event, dict)
        ]
        source_damage: Counter[str] = Counter()
        maximum_single_damage = 0
        for event in damages:
            source = str(event.get("source", "unknown"))
            damage = max(0, int(event.get("damage", 0)))
            source_damage[source] += damage
            source_counts[source] += damage
            maximum_single_damage = max(maximum_single_damage, damage)

        labels: list[str] = []
        if source_damage.get("pocket", 0) > 0:
            labels.append("player_pocket")
        if maximum_single_damage / maximum_hp >= burst_ratio:
            labels.append("burst_damage")
        total_shots = int(result.get("total_shots", 0))
        shot_upper = _median_shot_upper_target(stage_type, targets)
        if shot_upper > 0 and total_shots >= shot_upper * long_battle_multiplier:
            labels.append("offense_shortfall")
        if source_damage and "player_pocket" not in labels:
            labels.append("combat_attrition")
        if not labels:
            labels.append("unknown")

        primary_order = (
            "player_pocket",
            "burst_damage",
            "offense_shortfall",
            "combat_attrition",
            "unknown",
        )
        primary = next(label for label in primary_order if label in labels)
        primary_counts[primary] += 1
        label_counts.update(labels)
        progress_counts[str(progress)] += 1
        stage_type_counts[stage_type] += 1
        if len(examples) < 20:
            examples.append(
                {
                    "run_id": str(run.get("run_id", path.stem)),
                    "progress": progress,
                    "stage_type": stage_type,
                    "primary_reason": primary,
                    "labels": labels,
                    "total_shots": total_shots,
                    "maximum_single_damage_ratio": round(
                        maximum_single_damage / maximum_hp,
                        4,
                    ),
                }
            )

    return {
        "game_over_count": sum(primary_counts.values()),
        "primary_reason_counts": dict(sorted(primary_counts.items())),
        "overlapping_label_counts": dict(sorted(label_counts.items())),
        "damage_by_source": dict(sorted(source_counts.items())),
        "game_over_by_progress": dict(
            sorted(progress_counts.items(), key=lambda item: int(item[0]))
        ),
        "game_over_by_stage_type": dict(sorted(stage_type_counts.items())),
        "examples": examples,
        "classification_notes": [
            "Reasons are telemetry-derived indicators, not proof of player intent.",
            "A failed run may have multiple overlapping labels.",
        ],
    }


def _difficulty_curve(
    selected_runs: list[LoadedRun],
    targets: dict[str, Any],
) -> dict[str, Any]:
    diagnostics = targets.get("diagnostics", {})
    if not isinstance(diagnostics, dict):
        diagnostics = {}
    settings = diagnostics.get("difficulty_spikes", {})
    if not isinstance(settings, dict):
        settings = {}
    minimum_samples = max(1, int(settings.get("minimum_samples_per_floor", 5)))
    failure_delta_limit = float(settings.get("failure_rate_delta", 0.15))
    damage_delta_limit = float(settings.get("damage_ratio_delta", 0.20))
    shot_delta_limit = float(settings.get("median_shots_delta", 3.0))
    hp_drop_limit = float(settings.get("remaining_hp_ratio_drop", 0.20))

    grouped: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for _, run, _ in selected_runs:
        for stage in run.get("stages", []):
            if not isinstance(stage, dict):
                continue
            result = _stage_result(stage)
            if result.get("result") not in {"clear", "game_over"}:
                continue
            grouped[_stage_progress(stage)].append(stage)

    floors: list[dict[str, Any]] = []
    for progress, stages in sorted(grouped.items()):
        results = [_stage_result(stage) for stage in stages]
        remaining = [
            float(result.get("remaining_hp_ratio", 0.0))
            for result in results
        ]
        shots = [float(result.get("total_shots", 0)) for result in results]
        damage_ratios = [_stage_damage_ratio(stage) for stage in stages]
        stage_types = Counter(str(stage.get("stage_type", "unknown")) for stage in stages)
        floors.append(
            {
                "progress": progress,
                "stage_types": dict(sorted(stage_types.items())),
                "sample_count": len(stages),
                "failure_rate": round(
                    sum(result.get("result") == "game_over" for result in results)
                    / len(results),
                    4,
                ),
                "average_damage_ratio": round(statistics.fmean(damage_ratios), 4),
                "median_shots": round(float(statistics.median(shots)), 4),
                "average_remaining_hp_ratio": round(statistics.fmean(remaining), 4),
            }
        )

    spikes: list[dict[str, Any]] = []
    for previous, current in zip(floors, floors[1:]):
        if (
            int(previous["sample_count"]) < minimum_samples
            or int(current["sample_count"]) < minimum_samples
        ):
            continue
        deltas = {
            "failure_rate": round(current["failure_rate"] - previous["failure_rate"], 4),
            "average_damage_ratio": round(
                current["average_damage_ratio"] - previous["average_damage_ratio"],
                4,
            ),
            "median_shots": round(current["median_shots"] - previous["median_shots"], 4),
            "average_remaining_hp_ratio": round(
                current["average_remaining_hp_ratio"]
                - previous["average_remaining_hp_ratio"],
                4,
            ),
        }
        reasons: list[str] = []
        if deltas["failure_rate"] >= failure_delta_limit:
            reasons.append("failure_rate_jump")
        if deltas["average_damage_ratio"] >= damage_delta_limit:
            reasons.append("damage_jump")
        if deltas["median_shots"] >= shot_delta_limit:
            reasons.append("shot_count_jump")
        if deltas["average_remaining_hp_ratio"] <= -hp_drop_limit:
            reasons.append("remaining_hp_drop")
        if reasons:
            spikes.append(
                {
                    "from_progress": previous["progress"],
                    "to_progress": current["progress"],
                    "reasons": reasons,
                    "deltas": deltas,
                }
            )

    return {
        "minimum_samples_per_floor": minimum_samples,
        "floors": floors,
        "detected_spikes": spikes,
    }


def _dda_usage(selected_runs: list[LoadedRun]) -> dict[str, Any]:
    evaluation_count = 0
    changed_count = 0
    level_changes: Counter[str] = Counter()
    result_counts: Counter[str] = Counter()
    for _, run, _ in selected_runs:
        for event in run.get("events", []):
            if not isinstance(event, dict):
                continue
            if event.get("event_type") != "dynamic_balance_evaluation":
                continue
            details = event.get("details", {})
            if not isinstance(details, dict):
                details = {}
            evaluation_count += 1
            change = int(details.get("level_change", 0))
            if change:
                changed_count += 1
            level_changes[str(change)] += 1
            result_counts[str(details.get("result", "unknown"))] += 1
    return {
        "evaluation_count": evaluation_count,
        "level_change_count": changed_count,
        "level_change_rate": round(
            changed_count / evaluation_count if evaluation_count else 0.0,
            4,
        ),
        "level_changes": dict(sorted(level_changes.items())),
        "results": dict(sorted(result_counts.items())),
    }


def calculate_balance_diagnostics(
    selected_runs: list[LoadedRun],
    targets: dict[str, Any],
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "failure_taxonomy": _failure_taxonomy(selected_runs, targets),
        "difficulty_curve": _difficulty_curve(selected_runs, targets),
        "dda_usage": _dda_usage(selected_runs),
    }
