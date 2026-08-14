"""Audit fixed-condition balance logs for reproducible regression runs."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_LOG_DIRECTORY = Path("logs/balance")
DEFAULT_CONFIG_FILE = Path("assets/data/balance_validation.json")
DEFAULT_OUTPUT_FILE = Path("logs/balance/fixed_validation_report.json")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def expected_stage_type(stage_index: int) -> str:
    if stage_index % 10 == 0:
        return "boss"
    if stage_index % 5 == 0:
        return "midBoss"
    return "normal"


def audit_run(
    run: dict[str, Any],
    expected_seeds: set[int],
    fixed_stage_schedule: bool,
    expected_dynamic_balance_forced_off: bool = True,
) -> list[str]:
    failures: list[str] = []
    run_id = str(run.get("run_id", "unknown"))
    context = run.get("run_context", {})
    if not isinstance(context, dict):
        return [f"{run_id}: run_context is missing"]

    randomness = context.get("randomness", {})
    if not isinstance(randomness, dict):
        randomness = {}
    if randomness.get("run_seed") not in expected_seeds:
        failures.append(
            f"{run_id}: run_seed={randomness.get('run_seed')} "
            f"expected one of {sorted(expected_seeds)}"
        )

    validation = context.get("validation", {})
    actual_forced_off = bool(
        validation.get("dynamic_balance_forced_off", False)
        if isinstance(validation, dict)
        else False
    )
    if actual_forced_off != expected_dynamic_balance_forced_off:
        failures.append(
            f"{run_id}: DDA forced_off={actual_forced_off}, "
            f"expected {expected_dynamic_balance_forced_off}"
        )

    for stage in run.get("stages", []):
        if not isinstance(stage, dict):
            continue
        stage_index = int(stage.get("stage_index", 0))
        stage_context = stage.get("stage_context", {})
        if not isinstance(stage_context, dict):
            failures.append(
                f"{run_id}/stage {stage_index}: stage_context is missing"
            )
            continue
        assist = stage_context.get("assist_mode", {})
        assist_enabled = bool(
            assist.get("enabled", True)
            if isinstance(assist, dict)
            else True
        )
        if expected_dynamic_balance_forced_off and assist_enabled:
            failures.append(
                f"{run_id}/stage {stage_index}: assist mode was enabled"
            )
        if fixed_stage_schedule and stage_index > 0:
            actual_type = str(stage.get("stage_type", "unknown"))
            expected_type = expected_stage_type(stage_index)
            if actual_type != expected_type:
                failures.append(
                    f"{run_id}/stage {stage_index}: "
                    f"type={actual_type}, expected {expected_type}"
                )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOG_DIRECTORY)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_FILE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_FILE)
    args = parser.parse_args()

    try:
        config = load_json(args.config)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Failed to read validation config: {error}")
        return 1

    experiment_id = str(config.get("experiment_id", "fixed_baseline"))
    configured_seeds = config.get("random_seeds", [])
    expected_seeds = {
        int(seed)
        for seed in configured_seeds
        if isinstance(seed, int) and seed >= 0
    }
    if not expected_seeds:
        expected_seeds = {int(config.get("random_seed", 20260807))}
    fixed_schedule = bool(config.get("fixed_stage_schedule", True))
    variants = {
        str(variant.get("id")): bool(
            variant.get("disable_dynamic_balance", True)
        )
        for variant in config.get("variants", [])
        if isinstance(variant, dict) and variant.get("id")
    }
    matching_runs: list[dict[str, Any]] = []
    invalid_log_files: list[str] = []

    for path in sorted(args.logs.glob("run_*.json")):
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
        if (
            isinstance(validation, dict)
            and validation.get("enabled", False)
            and validation.get("experiment_id") == experiment_id
        ):
            matching_runs.append(run)

    failures: list[str] = []
    variant_counts: Counter[str] = Counter()
    for run in matching_runs:
        validation = run.get("run_context", {}).get("validation", {})
        variant_id = str(
            validation.get("variant_id", experiment_id)
            if isinstance(validation, dict)
            else experiment_id
        )
        variant_counts[variant_id] += 1
        expected_forced_off = variants.get(
            variant_id,
            bool(config.get("disable_dynamic_balance", True)),
        )
        failures.extend(audit_run(
            run,
            expected_seeds,
            fixed_schedule,
            expected_forced_off,
        ))
    profile_counts = Counter(
        str(run.get("run_context", {}).get("controller_profile", "unknown"))
        for run in matching_runs
    )
    minimum_runs = int(config.get("minimum_runs_per_profile", 30))
    required_profiles = [
        str(profile)
        for profile in config.get("required_controller_profiles", [])
    ]
    insufficient_profiles = {
        profile: profile_counts.get(profile, 0)
        for profile in required_profiles
        if profile_counts.get(profile, 0) < minimum_runs
    }

    if failures:
        status = "failed"
    elif not matching_runs or insufficient_profiles:
        status = "insufficient_data"
    else:
        status = "passed"

    report = {
        "schema_version": 1,
        "generated_at": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "status": status,
        "experiment_id": experiment_id,
        "expected_seeds": sorted(expected_seeds),
        "matching_run_count": len(matching_runs),
        "controller_profile_counts": dict(sorted(profile_counts.items())),
        "variant_counts": dict(sorted(variant_counts.items())),
        "minimum_runs_per_profile": minimum_runs,
        "insufficient_profiles": insufficient_profiles,
        "integrity_failures": failures,
        "invalid_log_files": invalid_log_files,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as destination:
        json.dump(report, destination, ensure_ascii=False, indent=2)
        destination.write("\n")

    print(
        f"Fixed validation: {status}; "
        f"runs={len(matching_runs)}; failures={len(failures)}"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
