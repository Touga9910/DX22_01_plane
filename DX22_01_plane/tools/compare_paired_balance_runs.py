from __future__ import annotations

import argparse
import json
import statistics
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_LOG_DIRECTORY = Path("logs/balance")
DEFAULT_CONFIG_FILE = Path("assets/data/balance_validation.json")
DEFAULT_OUTPUT_FILE = Path("logs/balance/paired_dda_report.json")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def _variant_definitions(
    config: dict[str, Any],
) -> list[dict[str, Any]]:
    variants = config.get("variants", [])
    valid = [
        variant
        for variant in variants
        if isinstance(variant, dict)
        and isinstance(variant.get("id"), str)
        and variant["id"]
    ]
    if valid:
        return valid
    return [{
        "id": str(config.get("experiment_id", "fixed_baseline")),
        "disable_dynamic_balance": bool(
            config.get("disable_dynamic_balance", True)
        ),
    }]


def _run_summary(run: dict[str, Any]) -> dict[str, float]:
    result = run.get("run_result", {})
    if not isinstance(result, dict):
        result = {}
    stages = [
        stage
        for stage in run.get("stages", [])
        if isinstance(stage, dict)
    ]
    total_shots = sum(
        int(stage.get("stage_result", {}).get("total_shots", 0))
        for stage in stages
        if isinstance(stage.get("stage_result"), dict)
    )
    return {
        "reached_progress": float(result.get("reached_stage_index", 0)),
        "cleared_stages": float(result.get("cleared_stage_count", 0)),
        "total_shots": float(total_shots),
        "remaining_hp_ratio": float(
            result.get("remaining_hp_ratio", 0.0)
        ),
        "boss_reached": float(
            any(stage.get("stage_type") == "boss" for stage in stages)
        ),
    }


def build_report(
    log_directory: Path,
    config: dict[str, Any],
) -> dict[str, Any]:
    experiment_id = str(
        config.get("experiment_id", "paired_dda_5x2")
    )
    variants = _variant_definitions(config)
    variant_ids = [str(variant["id"]) for variant in variants]
    expected_seeds = {
        int(seed)
        for seed in config.get("random_seeds", [])
        if isinstance(seed, int) and seed >= 0
    }
    minimum_runs = int(config.get("minimum_runs_per_variant", 5))
    minimum_pairs = int(config.get("minimum_paired_seeds", 5))
    by_variant: dict[str, dict[int, dict[str, Any]]] = {
        variant_id: {} for variant_id in variant_ids
    }
    integrity_failures: list[str] = []
    invalid_log_files: list[str] = []

    for path in sorted(log_directory.glob("run_*.json")):
        try:
            run = load_json(path)
        except (OSError, ValueError, json.JSONDecodeError):
            invalid_log_files.append(str(path))
            continue
        context = run.get("run_context", {})
        validation = (
            context.get("validation", {})
            if isinstance(context, dict)
            else {}
        )
        if not isinstance(validation, dict):
            continue
        if (
            not validation.get("enabled", False)
            or validation.get("experiment_id") != experiment_id
        ):
            continue
        variant_id = str(validation.get("variant_id", ""))
        if variant_id not in by_variant:
            integrity_failures.append(
                f"{path.name}: unknown variant_id={variant_id}"
            )
            continue
        randomness = context.get("randomness", {})
        seed = (
            randomness.get("run_seed")
            if isinstance(randomness, dict)
            else None
        )
        if not isinstance(seed, int) or seed not in expected_seeds:
            integrity_failures.append(
                f"{path.name}: unexpected run_seed={seed}"
            )
            continue
        if seed in by_variant[variant_id]:
            integrity_failures.append(
                f"{path.name}: duplicate {variant_id}/seed={seed}"
            )
            continue
        expected_forced_off = bool(next(
            variant.get("disable_dynamic_balance", True)
            for variant in variants
            if variant["id"] == variant_id
        ))
        actual_forced_off = bool(
            validation.get("dynamic_balance_forced_off", False)
        )
        if actual_forced_off != expected_forced_off:
            integrity_failures.append(
                f"{path.name}: {variant_id} forced_off="
                f"{actual_forced_off}, expected {expected_forced_off}"
            )
        by_variant[variant_id][seed] = run

    paired_seeds = sorted(
        set.intersection(*(
            set(by_variant[variant_id])
            for variant_id in variant_ids
        ))
        if variant_ids
        else set()
    )
    summaries: dict[str, dict[str, Any]] = {}
    for variant_id in variant_ids:
        runs = list(by_variant[variant_id].values())
        values = [_run_summary(run) for run in runs]
        summaries[variant_id] = {
            "run_count": len(runs),
            "seeds": sorted(by_variant[variant_id]),
            "averages": {
                metric: round(statistics.fmean(
                    value[metric] for value in values
                ), 4)
                for metric in (
                    "reached_progress",
                    "cleared_stages",
                    "total_shots",
                    "remaining_hp_ratio",
                    "boss_reached",
                )
            } if values else {},
        }

    comparison: dict[str, Any] = {}
    if len(variant_ids) == 2 and paired_seeds:
        first_id, second_id = variant_ids
        paired_deltas: dict[str, list[float]] = {
            metric: []
            for metric in (
                "reached_progress",
                "cleared_stages",
                "total_shots",
                "remaining_hp_ratio",
                "boss_reached",
            )
        }
        for seed in paired_seeds:
            first = _run_summary(by_variant[first_id][seed])
            second = _run_summary(by_variant[second_id][seed])
            for metric in paired_deltas:
                paired_deltas[metric].append(
                    second[metric] - first[metric]
                )
        comparison = {
            "baseline_variant": first_id,
            "comparison_variant": second_id,
            "delta_comparison_minus_baseline": {
                metric: round(statistics.fmean(deltas), 4)
                for metric, deltas in paired_deltas.items()
            },
        }

    insufficient_variants = {
        variant_id: len(by_variant[variant_id])
        for variant_id in variant_ids
        if len(by_variant[variant_id]) < minimum_runs
    }
    if integrity_failures:
        status = "failed"
    elif insufficient_variants or len(paired_seeds) < minimum_pairs:
        status = "insufficient_data"
    else:
        status = "passed"

    return {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "status": status,
        "experiment_id": experiment_id,
        "expected_seeds": sorted(expected_seeds),
        "variant_order": variant_ids,
        "minimum_runs_per_variant": minimum_runs,
        "minimum_paired_seeds": minimum_pairs,
        "paired_seeds": paired_seeds,
        "variants": summaries,
        "comparison": comparison,
        "insufficient_variants": insufficient_variants,
        "integrity_failures": integrity_failures,
        "invalid_log_files": invalid_log_files,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare paired DDA-off and DDA-on balance runs."
    )
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOG_DIRECTORY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_FILE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_FILE)
    args = parser.parse_args()
    try:
        config = load_json(args.config)
        report = build_report(args.logs, config)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Failed to compare paired runs: {error}")
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as destination:
        json.dump(report, destination, ensure_ascii=False, indent=2)
        destination.write("\n")
    print(
        f"Paired DDA comparison: {report['status']}; "
        f"pairs={len(report['paired_seeds'])}"
    )
    return 1 if report["status"] == "failed" else 0


if __name__ == "__main__":
    raise SystemExit(main())
