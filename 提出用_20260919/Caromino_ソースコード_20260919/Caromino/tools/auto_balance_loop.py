"""安全策付きの収集・分析・調整反復を無人で実行する。

ランのプレイは収集器とローカルMCPサーバーが引き続き担当する。このモジュールは、
現在の設定コホートが十分な大きさになったかを判定し、決定的な条件を通過した後だけ
AIを呼び出す。検証済みの計画を適用した後、新しい設定指紋について標本数0から
収集を始める。変更方向の反転や同一設定の再訪を検出した場合はループを停止する。
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import queue
import shutil
import statistics
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from adjust_balance_from_logs import (
    DEFAULT_BACKUP_DIRECTORY,
    DEFAULT_ENEMY_FILE,
    DEFAULT_LOG_DIRECTORY,
    DEFAULT_POLICY_FILE,
    DEFAULT_TARGET_FILE,
    apply_plan,
    build_adjustment_plan,
    file_sha256,
    load_json,
    utc_now,
    write_json,
)
from analyze_balance_logs import (
    RunFilters,
    TERMINAL_RUN_RESULTS,
    calculate_run_report,
    configuration_fingerprint,
    select_run_logs,
)
from generate_balance_plan_with_ai import DEFAULT_AI_CONFIG_FILE


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOOP_CONFIG_FILE = Path("assets/data/auto_balance_loop.json")
DEFAULT_STATE_FILE = Path("logs/balance/auto_balance/state.json")
DEFAULT_PLAN_DIRECTORY = Path("logs/balance/auto_balance/plans")
DEFAULT_LOCK_FILE = Path("logs/balance/auto_balance/loop.lock")
DEFAULT_COLLECTOR = Path("tools/game_mcp/collect_fixed_balance_runs.py")
DEFAULT_AI_GENERATOR = Path("tools/generate_balance_plan_with_ai.py")
CONFIGURATION_PATHS = (
    "assets/data/stage_01.json",
    "assets/data/enemy_data.json",
    "assets/data/player_status.json",
    "assets/data/player_deck.json",
    "assets/data/dynamic_balance.json",
    "assets/data/difficulty_profiles.json",
    "assets/data/balance_validation.json",
    "assets/data/encounter_balance.json",
    "assets/data/pocket_rules.json",
    "assets/data/balance_autoplay.json",
    "assets/data/balance_targets.json",
    "tools/game_mcp/player_profiles.json",
    "tools/game_mcp/build_profiles.json",
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Collect current-cohort runs and apply guarded AI balance changes "
            "until balanced or a safety stop is reached."
        )
    )
    parser.add_argument("--config", type=Path, default=DEFAULT_LOOP_CONFIG_FILE)
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE_FILE)
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOG_DIRECTORY)
    parser.add_argument("--targets", type=Path, default=DEFAULT_TARGET_FILE)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY_FILE)
    parser.add_argument("--enemies", type=Path, default=DEFAULT_ENEMY_FILE)
    parser.add_argument("--ai-config", type=Path, default=DEFAULT_AI_CONFIG_FILE)
    parser.add_argument(
        "--backup-directory",
        type=Path,
        default=DEFAULT_BACKUP_DIRECTORY,
    )
    parser.add_argument(
        "--plan-directory",
        type=Path,
        default=DEFAULT_PLAN_DIRECTORY,
    )
    parser.add_argument(
        "--lock-file",
        type=Path,
        default=DEFAULT_LOCK_FILE,
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Evaluate readiness without collecting, calling AI, or writing state.",
    )
    parser.add_argument(
        "--mock-response",
        type=Path,
        help=argparse.SUPPRESS,
    )
    return parser.parse_args()


def normalize_argument_paths(args: argparse.Namespace) -> None:
    for name in (
        "config",
        "state",
        "logs",
        "targets",
        "policy",
        "enemies",
        "ai_config",
        "backup_directory",
        "plan_directory",
        "lock_file",
        "mock_response",
    ):
        value = getattr(args, name, None)
        if value is not None and not value.is_absolute():
            setattr(args, name, PROJECT_ROOT / value)


def fnv1a64_file(path: Path) -> tuple[bool, int, str]:
    if not path.is_file():
        return False, 0, ""
    value = 14695981039346656037
    size = 0
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            size += len(block)
            for byte in block:
                value ^= byte
                value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return True, size, f"{value:016x}"


def current_configuration_snapshot(project_root: Path) -> dict[str, Any]:
    files: list[dict[str, Any]] = []
    for relative_path in CONFIGURATION_PATHS:
        exists, size, fingerprint = fnv1a64_file(
            project_root / Path(relative_path)
        )
        entry: dict[str, Any] = {
            "path": relative_path,
            "exists": exists,
        }
        if exists:
            entry["size_bytes"] = size
            entry["fnv1a64"] = fingerprint
        files.append(entry)
    return {"files": files}


def current_configuration_fingerprint(project_root: Path) -> str:
    return configuration_fingerprint(
        {"configuration": current_configuration_snapshot(project_root)}
    )


def validate_loop_config(config: dict[str, Any]) -> None:
    if int(config.get("schema_version", 0)) != 1:
        raise ValueError("Unsupported auto balance config schema_version")
    if config.get("controller_profile") not in {
        "beginner",
        "intermediate",
        "advanced",
    }:
        raise ValueError("Invalid controller_profile")
    positive_fields = (
        "collection_batch_runs",
        "maximum_steps_per_run",
        "maximum_adjustment_iterations",
        "maximum_non_improving_iterations",
    )
    for field in positive_fields:
        if int(config.get(field, 0)) <= 0:
            raise ValueError(f"{field} must be positive")
    if int(config.get("maximum_collection_batches_per_fingerprint", 12)) <= 0:
        raise ValueError(
            "maximum_collection_batches_per_fingerprint must be positive"
        )
    quality = config.get("sample_quality", {})
    if not isinstance(quality, dict):
        raise ValueError("sample_quality must be an object")
    if int(quality.get("minimum_unique_run_seeds", 1)) <= 0:
        raise ValueError(
            "sample_quality.minimum_unique_run_seeds must be positive"
        )
    for field in (
        "minimum_terminal_run_rate",
        "maximum_single_seed_share",
        "maximum_duration_outlier_rate",
    ):
        value = float(quality.get(field, 0.0 if field.startswith("minimum") else 1.0))
        if not 0.0 <= value <= 1.0:
            raise ValueError(f"sample_quality.{field} must be between 0 and 1")
    if float(quality.get("maximum_single_seed_share", 1.0)) <= 0.0:
        raise ValueError(
            "sample_quality.maximum_single_seed_share must be greater than 0"
        )
    paired = config.get("paired_validation", {})
    if not isinstance(paired, dict):
        raise ValueError("paired_validation must be an object")
    seeds = paired.get("seeds", [])
    if bool(paired.get("enabled", True)):
        if not isinstance(seeds, list) or not seeds:
            raise ValueError("paired_validation.seeds must not be empty")
        if any(
            not isinstance(seed, int) or not 0 <= seed <= 0xFFFFFFFF
            for seed in seeds
        ):
            raise ValueError("paired_validation.seeds contains an invalid seed")
    holdout = config.get("holdout_validation", {})
    if not isinstance(holdout, dict):
        raise ValueError("holdout_validation must be an object")
    holdout_seeds = holdout.get("seeds", [])
    if bool(holdout.get("enabled", False)):
        if not isinstance(holdout_seeds, list) or not holdout_seeds:
            raise ValueError("holdout_validation.seeds must not be empty")
        if any(
            not isinstance(seed, int) or not 0 <= seed <= 0xFFFFFFFF
            for seed in holdout_seeds
        ):
            raise ValueError(
                "holdout_validation.seeds contains an invalid seed"
            )
        duplicated_seeds = set(seeds).intersection(holdout_seeds)
        if duplicated_seeds:
            raise ValueError(
                "holdout_validation.seeds must be disjoint from "
                "paired_validation.seeds"
            )
    regression = config.get("regression_validation", {})
    if not isinstance(regression, dict):
        raise ValueError("regression_validation must be an object")
    cases = regression.get("cases", [])
    if bool(regression.get("enabled", True)) and not isinstance(cases, list):
        raise ValueError("regression_validation.cases must be an array")
    if bool(regression.get("enabled", True)):
        if int(regression.get("seed_count", 1)) <= 0:
            raise ValueError("regression_validation.seed_count must be positive")
        for index, case in enumerate(cases):
            if not isinstance(case, dict):
                raise ValueError(
                    f"regression_validation.cases[{index}] must be an object"
                )
            if case.get("controller_profile") not in {
                "beginner",
                "intermediate",
                "advanced",
            }:
                raise ValueError(
                    "regression_validation.cases"
                    f"[{index}].controller_profile is invalid"
                )
            if not str(case.get("build_profile", "standard")).strip():
                raise ValueError(
                    "regression_validation.cases"
                    f"[{index}].build_profile must not be empty"
                )
            case_seeds = case.get("seeds")
            if case_seeds is not None and (
                not isinstance(case_seeds, list)
                or not case_seeds
                or any(
                    not isinstance(seed, int) or not 0 <= seed <= 0xFFFFFFFF
                    for seed in case_seeds
                )
            ):
                raise ValueError(
                    f"regression_validation.cases[{index}].seeds is invalid"
                )
    budgets = config.get("budgets", {})
    if not isinstance(budgets, dict):
        raise ValueError("budgets must be an object")
    for name in (
        "maximum_wall_clock_seconds",
        "maximum_completed_runs",
        "maximum_ai_calls",
        "maximum_total_tokens",
    ):
        if float(budgets.get(name, 1)) <= 0:
            raise ValueError(f"budgets.{name} must be positive")
    maximum_cost = budgets.get("maximum_estimated_cost_usd")
    if maximum_cost is not None and float(maximum_cost) <= 0:
        raise ValueError(
            "budgets.maximum_estimated_cost_usd must be positive"
        )
    pricing = budgets.get("pricing_usd_per_million_tokens", {})
    if not isinstance(pricing, dict):
        raise ValueError(
            "budgets.pricing_usd_per_million_tokens must be an object"
        )
    for name in ("input", "cached_input", "output"):
        if float(pricing.get(name, 0.0)) < 0:
            raise ValueError(
                "budgets.pricing_usd_per_million_tokens"
                f".{name} must not be negative"
            )


def new_state() -> dict[str, Any]:
    now = utc_now()
    return {
        "schema_version": 2,
        "status": "running",
        "started_at": now,
        "updated_at": now,
        "adjustment_iteration": 0,
        "initial_configuration_fingerprint": "",
        "current_configuration_fingerprint": "",
        "last_scored_configuration_fingerprint": "",
        "non_improving_iterations": 0,
        "collection_batches": [],
        "history": [],
        "pending_apply": {},
        "budget_usage": {
            "completed_runs": 0,
            "ai_calls": 0,
            "input_tokens": 0,
            "cached_input_tokens": 0,
            "output_tokens": 0,
            "total_tokens": 0,
            "estimated_cost_usd": 0.0,
        },
        "stop_reason": "",
        "stop_details": {},
    }


def save_state(path: Path, state: dict[str, Any]) -> None:
    state["updated_at"] = utc_now()
    write_json(path, state)


def _budget_usage(state: dict[str, Any]) -> dict[str, Any]:
    usage = state.setdefault("budget_usage", {})
    defaults = {
        "completed_runs": 0,
        "ai_calls": 0,
        "input_tokens": 0,
        "cached_input_tokens": 0,
        "output_tokens": 0,
        "total_tokens": 0,
        "estimated_cost_usd": 0.0,
    }
    for name, default in defaults.items():
        usage.setdefault(name, default)
    return usage


def budget_failure(
    state: dict[str, Any],
    config: dict[str, Any],
    *,
    additional_runs: int = 0,
    additional_ai_calls: int = 0,
) -> dict[str, Any] | None:
    budgets = config.get("budgets", {})
    if not isinstance(budgets, dict):
        return {"type": "invalid_budget_configuration"}
    usage = _budget_usage(state)
    started_text = str(state.get("started_at", utc_now()))
    started = datetime.fromisoformat(started_text.replace("Z", "+00:00"))
    elapsed_seconds = max(
        0.0,
        (datetime.now(timezone.utc) - started).total_seconds(),
    )
    checks = (
        (
            "wall_clock_seconds",
            elapsed_seconds,
            float(budgets.get("maximum_wall_clock_seconds", 86400)),
        ),
        (
            "completed_runs",
            int(usage["completed_runs"]) + additional_runs,
            int(budgets.get("maximum_completed_runs", 200)),
        ),
        (
            "ai_calls",
            int(usage["ai_calls"]) + additional_ai_calls,
            int(budgets.get("maximum_ai_calls", 10)),
        ),
        (
            "total_tokens",
            int(usage["total_tokens"]),
            int(budgets.get("maximum_total_tokens", 100000)),
        ),
    )
    for name, actual, maximum in checks:
        reached_limit = (
            actual >= maximum
            if name in {"wall_clock_seconds", "total_tokens"}
            else actual > maximum
        )
        if reached_limit:
            return {
                "type": name,
                "actual": round(actual, 4),
                "maximum": maximum,
                "usage": dict(usage),
            }

    maximum_cost = budgets.get("maximum_estimated_cost_usd")
    if maximum_cost is not None and float(usage["estimated_cost_usd"]) >= float(
        maximum_cost
    ):
        return {
            "type": "estimated_cost_usd",
            "actual": float(usage["estimated_cost_usd"]),
            "maximum": float(maximum_cost),
            "usage": dict(usage),
        }
    return None


def record_collection_usage(
    state: dict[str, Any],
    summary: dict[str, Any],
) -> None:
    usage = _budget_usage(state)
    usage["completed_runs"] = int(usage["completed_runs"]) + int(
        summary.get("completed_runs", 0)
    )


def record_ai_usage(
    state: dict[str, Any],
    plan: dict[str, Any],
    config: dict[str, Any],
) -> None:
    usage = _budget_usage(state)
    usage["ai_calls"] = int(usage["ai_calls"]) + 1
    response_usage = plan.get("ai", {}).get("usage", {})
    if not isinstance(response_usage, dict):
        response_usage = {}
    input_tokens = int(response_usage.get("input_tokens", 0) or 0)
    output_tokens = int(response_usage.get("output_tokens", 0) or 0)
    input_details = response_usage.get("input_tokens_details", {})
    if not isinstance(input_details, dict):
        input_details = {}
    cached_input_tokens = int(input_details.get("cached_tokens", 0) or 0)
    total_tokens = int(
        response_usage.get(
            "total_tokens",
            input_tokens + output_tokens,
        )
        or 0
    )
    usage["input_tokens"] = int(usage["input_tokens"]) + input_tokens
    usage["cached_input_tokens"] = int(
        usage["cached_input_tokens"]
    ) + cached_input_tokens
    usage["output_tokens"] = int(usage["output_tokens"]) + output_tokens
    usage["total_tokens"] = int(usage["total_tokens"]) + total_tokens
    pricing = config.get("budgets", {}).get(
        "pricing_usd_per_million_tokens",
        {},
    )
    if isinstance(pricing, dict):
        uncached_input_tokens = max(0, input_tokens - cached_input_tokens)
        cost = (
            uncached_input_tokens * float(pricing.get("input", 0.0))
            + cached_input_tokens
            * float(pricing.get("cached_input", pricing.get("input", 0.0)))
            + output_tokens * float(pricing.get("output", 0.0))
        ) / 1_000_000.0
        usage["estimated_cost_usd"] = round(
            float(usage["estimated_cost_usd"]) + cost,
            8,
        )


def stop_state(
    path: Path,
    state: dict[str, Any],
    reason: str,
    details: dict[str, Any] | None = None,
    *,
    completed: bool = False,
) -> None:
    state["status"] = "completed" if completed else "stopped"
    state["stop_reason"] = reason
    state["stop_details"] = details or {}
    save_state(path, state)
    print(
        json.dumps(
            {
                "event": "auto_balance_completed" if completed else (
                    "auto_balance_stopped"
                ),
                "status": state["status"],
                "reason": reason,
                "details": state["stop_details"],
                "state_file": str(path),
            },
            ensure_ascii=False,
        ),
        flush=True,
    )


class LockUnavailableError(RuntimeError):
    """別の自動バランスプロセスがループロックを所有している場合に送出する。"""


class AutoBalanceLock:
    """プロセス終了時に自動解放される、プロセス間の非ブロッキングロック。"""

    def __init__(self, path: Path) -> None:
        self.path = path
        self._file: Any = None

    def __enter__(self) -> AutoBalanceLock:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("a+b")
        self._file.seek(0, os.SEEK_END)
        if self._file.tell() == 0:
            self._file.write(b"0")
            self._file.flush()
        self._file.seek(0)
        try:
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self._file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(
                    self._file.fileno(),
                    fcntl.LOCK_EX | fcntl.LOCK_NB,
                )
        except (OSError, BlockingIOError) as error:
            self._file.close()
            self._file = None
            raise LockUnavailableError(
                f"Another auto balance loop owns {self.path}"
            ) from error

        metadata = json.dumps(
            {
                "pid": os.getpid(),
                "acquired_at": utc_now(),
            },
            ensure_ascii=True,
        ).encode("utf-8")
        self._file.seek(1)
        self._file.truncate()
        self._file.write(metadata)
        self._file.flush()
        return self

    def __exit__(self, *_: Any) -> None:
        if self._file is None:
            return
        try:
            self._file.seek(0)
            if os.name == "nt":
                import msvcrt

                msvcrt.locking(self._file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(self._file.fileno(), fcntl.LOCK_UN)
        finally:
            self._file.close()
            self._file = None


def create_configuration_backup(
    project_root: Path,
    backup_directory: Path,
    fingerprint: str,
    iteration: int,
) -> Path:
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S_%f")
    destination = backup_directory / "auto_balance" / (
        f"iteration_{iteration:02d}_{fingerprint[:12]}_{timestamp}"
    )
    manifest_files: list[dict[str, Any]] = []
    for relative_name in CONFIGURATION_PATHS:
        relative_path = Path(relative_name)
        source = project_root / relative_path
        exists = source.is_file()
        entry: dict[str, Any] = {
            "path": relative_name,
            "exists": exists,
        }
        if exists:
            target = destination / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            entry["sha256"] = file_sha256(source)
        manifest_files.append(entry)
    write_json(
        destination / "manifest.json",
        {
            "schema_version": 1,
            "created_at": utc_now(),
            "configuration_fingerprint": fingerprint,
            "files": manifest_files,
        },
    )
    return destination


def restore_configuration_backup(
    project_root: Path,
    backup_directory: Path,
) -> str:
    manifest = load_json(backup_directory / "manifest.json")
    entries = manifest.get("files", [])
    if not isinstance(entries, list):
        raise RuntimeError("Configuration backup manifest is invalid")

    allowed = set(CONFIGURATION_PATHS)
    for entry in entries:
        if not isinstance(entry, dict):
            raise RuntimeError("Configuration backup entry is invalid")
        relative_name = str(entry.get("path", ""))
        if relative_name not in allowed:
            raise RuntimeError(
                f"Configuration backup contains an unexpected path: "
                f"{relative_name}"
            )
        target = project_root / Path(relative_name)
        if bool(entry.get("exists", False)):
            source = backup_directory / Path(relative_name)
            if not source.is_file():
                raise RuntimeError(
                    f"Configuration backup file is missing: {relative_name}"
                )
            expected_hash = str(entry.get("sha256", ""))
            if expected_hash and file_sha256(source) != expected_hash:
                raise RuntimeError(
                    f"Configuration backup hash mismatch: {relative_name}"
                )
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_suffix(target.suffix + ".restore.tmp")
            shutil.copy2(source, temporary)
            temporary.replace(target)
        elif target.exists():
            target.unlink()

    restored = current_configuration_fingerprint(project_root)
    expected = str(manifest.get("configuration_fingerprint", ""))
    if restored != expected:
        raise RuntimeError(
            "Restored configuration fingerprint does not match its backup"
        )
    return restored


def recover_interrupted_apply(
    project_root: Path,
    state_path: Path,
    state: dict[str, Any],
) -> bool:
    pending = state.get("pending_apply", {})
    if not isinstance(pending, dict) or not pending:
        return False
    backup_name = str(
        pending.get("configuration_backup_directory", "")
    )
    backup = Path(backup_name) if backup_name else None
    if backup is None or not backup.is_dir():
        raise RuntimeError(
            "Interrupted apply has no usable configuration backup"
        )
    restored = restore_configuration_backup(project_root, backup)
    state["current_configuration_fingerprint"] = restored
    state["pending_apply"] = {}
    stop_state(
        state_path,
        state,
        "interrupted_apply_rolled_back",
        {
            "restored_configuration_fingerprint": restored,
            "configuration_backup_directory": str(backup),
        },
    )
    return True


def _run_seed(run: dict[str, Any]) -> str:
    context = run.get("run_context", {})
    if not isinstance(context, dict):
        return ""
    randomness = context.get("randomness", {})
    if not isinstance(randomness, dict):
        return ""
    value = randomness.get("run_seed")
    return "" if value is None else str(value)


def _duration_outlier_count(durations: list[float]) -> int:
    if len(durations) < 8:
        return 0
    quartiles = statistics.quantiles(durations, n=4, method="inclusive")
    lower_quartile, upper_quartile = quartiles[0], quartiles[2]
    interquartile_range = upper_quartile - lower_quartile
    if interquartile_range <= 0.0:
        return 0
    lower_bound = lower_quartile - 3.0 * interquartile_range
    upper_bound = upper_quartile + 3.0 * interquartile_range
    return sum(
        value < lower_bound or value > upper_bound
        for value in durations
    )


def evaluate_sample_quality(
    selected_runs: list[tuple[Path, dict[str, Any], dict[str, Any]]],
    run_report: dict[str, Any],
    settings: dict[str, Any],
) -> dict[str, Any]:
    seeds = [_run_seed(run) for _, run, _ in selected_runs]
    seeds = [seed for seed in seeds if seed]
    seed_counts: dict[str, int] = {}
    for seed in seeds:
        seed_counts[seed] = seed_counts.get(seed, 0) + 1

    total_count = len(selected_runs)
    terminal_count = int(run_report.get("balance_sample_count", 0))
    terminal_rate = terminal_count / total_count if total_count else 0.0
    unique_seed_count = len(seed_counts)
    maximum_seed_count = max(seed_counts.values(), default=0)
    maximum_seed_share = (
        maximum_seed_count / len(seeds) if seeds else 1.0
    )
    durations = []
    for _, run, _ in selected_runs:
        result = run.get("run_result", {})
        if not isinstance(result, dict):
            continue
        duration = result.get("duration_ms")
        if isinstance(duration, (int, float)) and duration >= 0:
            durations.append(float(duration))
    outlier_count = _duration_outlier_count(durations)
    outlier_rate = outlier_count / len(durations) if durations else 0.0

    minimum_unique_seeds = max(
        1,
        int(settings.get("minimum_unique_run_seeds", 1)),
    )
    minimum_terminal_rate = float(
        settings.get("minimum_terminal_run_rate", 0.0)
    )
    maximum_seed_share_limit = float(
        settings.get("maximum_single_seed_share", 1.0)
    )
    maximum_outlier_rate = float(
        settings.get("maximum_duration_outlier_rate", 1.0)
    )
    issues: list[dict[str, Any]] = []
    additional_runs = 0
    if unique_seed_count < minimum_unique_seeds:
        missing = minimum_unique_seeds - unique_seed_count
        additional_runs = max(additional_runs, missing)
        issues.append(
            {
                "type": "insufficient_seed_diversity",
                "actual": unique_seed_count,
                "required": minimum_unique_seeds,
            }
        )
    if terminal_rate < minimum_terminal_rate:
        denominator = max(1e-9, 1.0 - minimum_terminal_rate)
        missing = max(
            1,
            math.ceil(
                (minimum_terminal_rate * total_count - terminal_count)
                / denominator
            ),
        )
        additional_runs = max(additional_runs, missing)
        issues.append(
            {
                "type": "low_terminal_run_rate",
                "actual": round(terminal_rate, 4),
                "required": minimum_terminal_rate,
            }
        )
    if maximum_seed_share > maximum_seed_share_limit:
        missing = max(
            1,
            math.ceil(
                maximum_seed_count / max(maximum_seed_share_limit, 1e-9)
                - len(seeds)
            ),
        )
        additional_runs = max(additional_runs, missing)
        issues.append(
            {
                "type": "single_seed_dominance",
                "actual": round(maximum_seed_share, 4),
                "maximum": maximum_seed_share_limit,
            }
        )
    if outlier_rate > maximum_outlier_rate:
        missing = max(
            1,
            math.ceil(
                outlier_count / max(maximum_outlier_rate, 1e-9)
                - len(durations)
            ),
        )
        additional_runs = max(additional_runs, missing)
        issues.append(
            {
                "type": "duration_outlier_rate",
                "actual": round(outlier_rate, 4),
                "maximum": maximum_outlier_rate,
            }
        )
    return {
        "passed": not issues,
        "issues": issues,
        "additional_runs_needed": additional_runs,
        "metrics": {
            "selected_run_count": total_count,
            "terminal_run_count": terminal_count,
            "terminal_run_rate": round(terminal_rate, 4),
            "unique_run_seed_count": unique_seed_count,
            "missing_run_seed_count": total_count - len(seeds),
            "maximum_single_seed_share": round(maximum_seed_share, 4),
            "duration_sample_count": len(durations),
            "duration_outlier_count": outlier_count,
            "duration_outlier_rate": round(outlier_rate, 4),
        },
    }


def evaluate_current_cohort(
    log_directory: Path,
    targets: dict[str, Any],
    policy: dict[str, Any],
    enemy_file: Path,
    enemy_data: dict[str, Any],
    fingerprint: str,
    controller_profile: str,
    build_profile: str,
    sample_quality_settings: dict[str, Any] | None = None,
    validation_variant: str = "",
) -> dict[str, Any]:
    filters = RunFilters(
        controller_types=("mcp",),
        controller_profiles=(controller_profile,),
        build_profiles=(build_profile,),
        validation_variants=(validation_variant,) if validation_variant else (),
        configuration_fingerprints=(fingerprint,),
    )
    selected_runs, run_selection = select_run_logs(
        log_directory,
        filters=filters,
    )
    run_report = calculate_run_report(selected_runs, targets)
    sample_quality = evaluate_sample_quality(
        selected_runs,
        run_report,
        sample_quality_settings or {},
    )
    guardrail_plan = build_adjustment_plan(
        log_directory,
        targets,
        policy,
        enemy_file,
        enemy_data,
        configuration_fingerprint=fingerprint,
        latest_configuration=False,
        controller_profile=controller_profile,
        build_profile=build_profile,
    )
    return {
        "configuration_fingerprint": fingerprint,
        "run_selection": run_selection,
        "run_balance": run_report,
        "sample_quality": sample_quality,
        "guardrail_plan": guardrail_plan,
    }


def assess_readiness(evaluation: dict[str, Any]) -> dict[str, Any]:
    run_report = evaluation["run_balance"]
    sample_count = int(run_report["balance_sample_count"])
    minimum_count = int(run_report["minimum_sample_count"])
    if sample_count < minimum_count:
        return {
            "status": "collect",
            "missing_runs": minimum_count - sample_count,
            "reason": "insufficient_current_configuration_runs",
        }

    quality = evaluation.get("sample_quality", {"passed": True})
    if not bool(quality.get("passed", True)):
        return {
            "status": "collect",
            "missing_runs": max(
                1,
                int(quality.get("additional_runs_needed", 1)),
            ),
            "reason": "sample_quality_requirements_not_met",
            "quality_issues": quality.get("issues", []),
        }

    run_statistics = run_report.get("statistical_decision", {})
    run_additional = int(
        run_statistics.get("additional_samples_recommended", 0)
        if isinstance(run_statistics, dict)
        else 0
    )
    stage_additional = max(
        (
            int(
                report.get("statistical_decision", {}).get(
                    "additional_samples_recommended",
                    0,
                )
            )
            for report in evaluation["guardrail_plan"].get(
                "stage_evaluations",
                [],
            )
            if isinstance(report, dict)
            and isinstance(report.get("statistical_decision", {}), dict)
        ),
        default=0,
    )
    if run_report.get("judgement") == "inconclusive" or stage_additional:
        additional = max(run_additional, stage_additional)
        if additional > 0:
            return {
                "status": "collect",
                "missing_runs": additional,
                "reason": "statistical_decision_inconclusive",
            }
        return {
            "status": "stop",
            "reason": "statistical_uncertainty_sample_limit_reached",
            "missing_runs": 0,
        }

    if run_report["judgement"] == "data_quality_issue":
        return {
            "status": "stop",
            "reason": "data_quality_issue",
            "missing_runs": 0,
        }

    changes = evaluation["guardrail_plan"]["changes"]
    stage_adjustment_required = any(
        report.get("adjustment_required", False)
        for report in evaluation["guardrail_plan"]["stage_evaluations"]
    )
    if run_report["judgement"] == "balanced" and not stage_adjustment_required:
        return {"status": "balanced", "reason": "targets_satisfied"}
    if not changes:
        return {
            "status": "stop",
            "reason": "no_actionable_guardrail_change",
            "missing_runs": 0,
        }
    return {"status": "ready", "reason": "sufficient_data"}


def validation_cases(config: dict[str, Any]) -> list[dict[str, Any]]:
    paired = config.get("paired_validation", {})
    if not bool(paired.get("enabled", True)):
        return []
    primary_seeds = [int(seed) for seed in paired.get("seeds", [])]
    variant = str(paired.get("validation_variant", ""))
    cases = [
        {
            "id": "primary",
            "kind": "primary",
            "controller_profile": str(config["controller_profile"]),
            "build_profile": str(config["build_profile"]),
            "run_seeds": primary_seeds,
            "validation_variant": variant,
            "minimum_score_delta": float(
                paired.get("minimum_balance_score_improvement", 0.0)
            ),
            "minimum_terminal_run_rate": float(
                paired.get("minimum_terminal_run_rate", 1.0)
            ),
        }
    ]
    holdout = config.get("holdout_validation", {})
    if bool(holdout.get("enabled", False)):
        cases.append(
            {
                "id": str(holdout.get("id", "holdout")),
                "kind": "holdout",
                "controller_profile": str(
                    holdout.get(
                        "controller_profile",
                        config["controller_profile"],
                    )
                ),
                "build_profile": str(
                    holdout.get("build_profile", config["build_profile"])
                ),
                "run_seeds": [
                    int(seed) for seed in holdout.get("seeds", [])
                ],
                "validation_variant": str(
                    holdout.get("validation_variant", variant)
                ),
                "minimum_score_delta": float(
                    holdout.get("minimum_balance_score_delta", 0.0)
                ),
                "minimum_terminal_run_rate": float(
                    holdout.get("minimum_terminal_run_rate", 1.0)
                ),
            }
        )
    regression = config.get("regression_validation", {})
    if not bool(regression.get("enabled", True)):
        return cases
    default_seed_count = max(
        1,
        int(regression.get("seed_count", min(2, len(primary_seeds)))),
    )
    for index, raw_case in enumerate(regression.get("cases", [])):
        if not isinstance(raw_case, dict):
            continue
        case_seeds = raw_case.get("seeds", primary_seeds[:default_seed_count])
        cases.append(
            {
                "id": str(raw_case.get("id", f"regression_{index + 1}")),
                "kind": "regression",
                "controller_profile": str(raw_case["controller_profile"]),
                "build_profile": str(raw_case.get("build_profile", "standard")),
                "run_seeds": [int(seed) for seed in case_seeds],
                "validation_variant": str(
                    raw_case.get("validation_variant", variant)
                ),
                "minimum_score_delta": -float(
                    raw_case.get(
                        "maximum_balance_score_regression",
                        regression.get(
                            "maximum_balance_score_regression",
                            5.0,
                        ),
                    )
                ),
                "minimum_terminal_run_rate": float(
                    raw_case.get(
                        "minimum_terminal_run_rate",
                        regression.get("minimum_terminal_run_rate", 1.0),
                    )
                ),
            }
        )
    return cases


def _terminal_run_outcome(run: dict[str, Any]) -> dict[str, Any]:
    result = run.get("run_result", {})
    if not isinstance(result, dict):
        result = {}
    return {
        "run_id": str(run.get("run_id", "")),
        "result": str(result.get("result", "")),
        "reached_stage_index": int(result.get("reached_stage_index", 0)),
        "cleared_stage_count": int(result.get("cleared_stage_count", 0)),
        "remaining_hp_ratio": float(result.get("remaining_hp_ratio", 0.0)),
    }


def build_validation_snapshot(
    log_directory: Path,
    fingerprint: str,
    case: dict[str, Any],
    targets: dict[str, Any],
) -> dict[str, Any]:
    selected, _ = select_run_logs(
        log_directory,
        filters=RunFilters(
            controller_types=("mcp",),
            controller_profiles=(str(case["controller_profile"]),),
            build_profiles=(str(case["build_profile"]),),
            validation_variants=(str(case["validation_variant"]),)
            if case.get("validation_variant")
            else (),
            configuration_fingerprints=(fingerprint,),
        ),
    )
    requested_seeds = [str(seed) for seed in case["run_seeds"]]
    by_seed: dict[str, tuple[Path, dict[str, Any], dict[str, Any]]] = {}
    for loaded in selected:
        run = loaded[1]
        result = run.get("run_result", {})
        if not isinstance(result, dict) or str(result.get("result", "")) not in (
            TERMINAL_RUN_RESULTS
        ):
            continue
        seed = _run_seed(run)
        if seed in requested_seeds:
            by_seed[seed] = loaded
    chosen = [by_seed[seed] for seed in requested_seeds if seed in by_seed]
    report = calculate_run_report(chosen, targets)
    return {
        "case": dict(case),
        "configuration_fingerprint": fingerprint,
        "missing_seeds": [
            int(seed) for seed in requested_seeds if seed not in by_seed
        ],
        "outcomes": {
            seed: _terminal_run_outcome(by_seed[seed][1])
            for seed in requested_seeds
            if seed in by_seed
        },
        "run_balance": report,
    }


def compare_validation_snapshots(
    baseline: dict[str, Any],
    candidate: dict[str, Any],
) -> dict[str, Any]:
    case = baseline["case"]
    before_score = float(baseline["run_balance"]["balance_score"])
    after_score = float(candidate["run_balance"]["balance_score"])
    score_delta = after_score - before_score
    terminal_rate = float(
        candidate["run_balance"].get("metrics", {}).get(
            "terminal_run_rate",
            0.0,
        )
    )
    minimum_delta = float(case["minimum_score_delta"])
    minimum_terminal_rate = float(case["minimum_terminal_run_rate"])
    seed_deltas: dict[str, dict[str, Any]] = {}
    for seed, before in baseline["outcomes"].items():
        after = candidate["outcomes"].get(seed)
        if after is None:
            continue
        seed_deltas[seed] = {
            "reached_stage_index": (
                int(after["reached_stage_index"])
                - int(before["reached_stage_index"])
            ),
            "cleared_stage_count": (
                int(after["cleared_stage_count"])
                - int(before["cleared_stage_count"])
            ),
            "remaining_hp_ratio": round(
                float(after["remaining_hp_ratio"])
                - float(before["remaining_hp_ratio"]),
                4,
            ),
        }
    passed = (
        not candidate["missing_seeds"]
        and score_delta >= minimum_delta
        and terminal_rate >= minimum_terminal_rate
    )
    return {
        "case_id": str(case["id"]),
        "kind": str(case["kind"]),
        "passed": passed,
        "balance_score_before": before_score,
        "balance_score_after": after_score,
        "balance_score_delta": round(score_delta, 4),
        "minimum_score_delta": minimum_delta,
        "terminal_run_rate": terminal_rate,
        "minimum_terminal_run_rate": minimum_terminal_rate,
        "missing_seeds": candidate["missing_seeds"],
        "seed_deltas": seed_deltas,
    }


def run_streaming_command(
    command: list[str],
    cwd: Path,
    timeout_seconds: float | None = None,
) -> list[str]:
    process = subprocess.Popen(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    output: list[str] = []
    assert process.stdout is not None
    output_queue: queue.Queue[str | None] = queue.Queue()

    def read_output() -> None:
        assert process.stdout is not None
        for raw_line in process.stdout:
            output_queue.put(raw_line.rstrip("\r\n"))
        output_queue.put(None)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    started = time.monotonic()
    while True:
        if (
            timeout_seconds is not None
            and time.monotonic() - started >= timeout_seconds
        ):
            process.kill()
            process.wait()
            raise RuntimeError(
                f"Command timed out after {timeout_seconds:.0f} seconds: "
                f"{command[1]}"
            )
        try:
            line = output_queue.get(timeout=0.5)
        except queue.Empty:
            if process.poll() is not None and not reader.is_alive():
                break
            continue
        if line is None:
            break
        output.append(line)
        print(line, flush=True)
    return_code = process.wait()
    if return_code != 0:
        raise RuntimeError(
            f"Command failed with exit code {return_code}: {command[1]}"
        )
    return output


def collect_runs(
    project_root: Path,
    config: dict[str, Any],
    requested_runs: int,
    *,
    controller_profile: str | None = None,
    build_profile: str | None = None,
    run_seeds: list[int] | None = None,
    validation_variant: str = "",
) -> dict[str, Any]:
    command = [
        sys.executable,
        str(project_root / DEFAULT_COLLECTOR),
        "--url",
        str(config["mcp_url"]),
        "--profile",
        controller_profile or str(config["controller_profile"]),
        "--build-profile",
        build_profile or str(config["build_profile"]),
        "--runs",
        str(requested_runs),
        "--maximum-steps",
        str(int(config["maximum_steps_per_run"])),
    ]
    for seed in run_seeds or []:
        command.extend(["--run-seed", str(seed)])
    if validation_variant:
        command.extend(["--validation-variant", validation_variant])
    output = run_streaming_command(
        command,
        project_root,
        float(config.get("maximum_collector_command_seconds", 7200)),
    )
    summary: dict[str, Any] | None = None
    for line in reversed(output):
        try:
            candidate = json.loads(line)
        except json.JSONDecodeError:
            continue
        if candidate.get("event") == "collection_complete":
            summary = candidate
            break
    if summary is None:
        raise RuntimeError("Collector did not publish collection_complete")
    completed = int(summary.get("completed_runs", 0))
    if completed < requested_runs:
        raise RuntimeError(
            f"Collector completed {completed}/{requested_runs} requested runs"
        )
    return summary


def generate_ai_proposal(
    project_root: Path,
    args: argparse.Namespace,
    config: dict[str, Any],
    fingerprint: str,
    output_path: Path,
) -> dict[str, Any]:
    command = [
        sys.executable,
        str(project_root / DEFAULT_AI_GENERATOR),
        "--logs",
        str(args.logs),
        "--targets",
        str(args.targets),
        "--policy",
        str(args.policy),
        "--enemies",
        str(args.enemies),
        "--ai-config",
        str(args.ai_config),
        "--output",
        str(output_path),
        "--configuration-fingerprint",
        fingerprint,
        "--controller-profile",
        str(config["controller_profile"]),
        "--build-profile",
        str(config["build_profile"]),
    ]
    if args.mock_response is not None:
        command.extend(["--mock-response", str(args.mock_response)])
    run_streaming_command(
        command,
        project_root,
        float(config.get("maximum_ai_command_seconds", 300)),
    )
    plan = load_json(output_path)
    selected_fingerprint = str(
        plan.get("source", {}).get("configuration_fingerprint", "")
    )
    if selected_fingerprint != fingerprint:
        raise RuntimeError("AI plan used a different configuration cohort")
    return plan


def change_key(change: dict[str, Any]) -> tuple[str, str, str]:
    return (
        str(change.get("target_type", "enemy")),
        str(change["target_id"]),
        str(change["field"]),
    )


def change_direction(change: dict[str, Any]) -> int:
    step = int(change["after"]) - int(change["before"])
    return 1 if step > 0 else -1 if step < 0 else 0


def detect_change_oscillation(
    proposed_changes: list[dict[str, Any]],
    history: list[dict[str, Any]],
    settings: dict[str, Any],
) -> dict[str, Any] | None:
    lookback = max(1, int(settings.get("reversal_lookback_iterations", 3)))
    recent_history = history[-lookback:]
    maximum_same_direction = max(
        1,
        int(settings.get("maximum_same_direction_changes", 3)),
    )
    for proposed in proposed_changes:
        key = change_key(proposed)
        direction = change_direction(proposed)
        if direction == 0:
            continue
        recent_matches: list[dict[str, Any]] = []
        all_matches: list[dict[str, Any]] = []
        for entry in history:
            for previous in entry.get("changes", []):
                if change_key(previous) == key:
                    all_matches.append(previous)
        for entry in recent_history:
            for previous in entry.get("changes", []):
                if change_key(previous) == key:
                    recent_matches.append(previous)
        if settings.get("stop_on_direction_reversal", True) and any(
            change_direction(previous) == -direction
            for previous in recent_matches
        ):
            return {
                "type": "direction_reversal",
                "target_type": key[0],
                "target_id": key[1],
                "field": key[2],
                "proposed_step": int(proposed["after"])
                - int(proposed["before"]),
            }
        same_direction_count = sum(
            change_direction(previous) == direction
            for previous in all_matches
        )
        if same_direction_count >= maximum_same_direction:
            return {
                "type": "repeated_same_direction_limit",
                "target_type": key[0],
                "target_id": key[1],
                "field": key[2],
                "previous_change_count": same_direction_count,
            }
    return None


def projected_enemy_status_sha256(
    plan: dict[str, Any],
    enemy_data: dict[str, Any],
) -> str:
    projected = copy.deepcopy(enemy_data)
    enemy_by_id = {
        str(enemy.get("id", "")): enemy
        for enemy in projected.get("enemies", [])
    }
    for change in plan.get("changes", []):
        enemy = enemy_by_id[str(change["target_id"])]
        field = str(change["field"]).removeprefix("status.")
        if int(enemy["status"][field]) != int(change["before"]):
            raise RuntimeError("Enemy status changed before projection")
        enemy["status"][field] = int(change["after"])
    payload = json.dumps(
        projected,
        ensure_ascii=False,
        indent=2,
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def seen_enemy_status_hashes(state: dict[str, Any]) -> set[str]:
    result: set[str] = set()
    for entry in state.get("history", []):
        for name in (
            "enemy_status_sha256_before",
            "enemy_status_sha256_after",
        ):
            value = str(entry.get(name, ""))
            if value:
                result.add(value)
    return result


def update_improvement_guard(
    state: dict[str, Any],
    fingerprint: str,
    score: float,
    config: dict[str, Any],
) -> dict[str, Any] | None:
    if state.get("last_scored_configuration_fingerprint") == fingerprint:
        return None
    history = state.get("history", [])
    if history:
        previous_score = float(
            history[-1].get("evaluation_before", {}).get(
                "run_balance_score",
                0.0,
            )
        )
        minimum_improvement = float(
            config.get("minimum_score_improvement", 0.5)
        )
        if score < previous_score + minimum_improvement:
            state["non_improving_iterations"] = int(
                state.get("non_improving_iterations", 0)
            ) + 1
        else:
            state["non_improving_iterations"] = 0
    state["last_scored_configuration_fingerprint"] = fingerprint
    maximum = int(config.get("maximum_non_improving_iterations", 2))
    if int(state.get("non_improving_iterations", 0)) >= maximum:
        return {
            "previous_score": float(
                history[-1]["evaluation_before"]["run_balance_score"]
            ),
            "current_score": score,
            "consecutive_non_improving_iterations": int(
                state["non_improving_iterations"]
            ),
        }
    return None


def run_loop(args: argparse.Namespace) -> int:
    project_root = PROJECT_ROOT
    config = load_json(args.config)
    validate_loop_config(config)
    if not bool(config.get("enabled", False)):
        print("Auto balance loop is disabled in its config.")
        return 2

    targets = load_json(args.targets)
    policy = load_json(args.policy)
    enemy_data = load_json(args.enemies)
    fingerprint = current_configuration_fingerprint(project_root)
    evaluation = evaluate_current_cohort(
        args.logs,
        targets,
        policy,
        args.enemies,
        enemy_data,
        fingerprint,
        str(config["controller_profile"]),
        str(config["build_profile"]),
        config.get("sample_quality", {}),
        str(config.get("paired_validation", {}).get(
            "validation_variant",
            "",
        )),
    )
    readiness = assess_readiness(evaluation)
    if args.dry_run:
        print(
            json.dumps(
                {
                    "configuration_fingerprint": fingerprint,
                    "run_balance": evaluation["run_balance"],
                    "sample_quality": evaluation["sample_quality"],
                    "run_selection": evaluation["run_selection"],
                    "readiness": readiness,
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        return 0

    state = load_json(args.state) if args.state.exists() else new_state()
    if recover_interrupted_apply(project_root, args.state, state):
        return 2
    if state.get("status") in {"stopped", "completed"}:
        print(
            f"Auto balance state is {state['status']}: "
            f"{state.get('stop_reason', '')}"
        )
        return 2
    if not state.get("initial_configuration_fingerprint"):
        state["initial_configuration_fingerprint"] = fingerprint
    save_state(args.state, state)

    while True:
        fingerprint = current_configuration_fingerprint(project_root)
        state["current_configuration_fingerprint"] = fingerprint
        history = state.get("history", [])
        if history:
            latest_history = history[-1]
            expected_fingerprint = str(
                latest_history.get(
                    "configuration_fingerprint_after",
                    "",
                )
            )
            if (
                expected_fingerprint
                and not latest_history.get("rolled_back_at")
                and fingerprint != expected_fingerprint
            ):
                stop_state(
                    args.state,
                    state,
                    "external_configuration_change_detected",
                    {
                        "expected_configuration_fingerprint": (
                            expected_fingerprint
                        ),
                        "actual_configuration_fingerprint": fingerprint,
                    },
                )
                return 2
        exhausted = budget_failure(state, config)
        if exhausted is not None:
            stop_state(
                args.state,
                state,
                "budget_exhausted",
                exhausted,
            )
            return 2
        targets = load_json(args.targets)
        policy = load_json(args.policy)
        enemy_data = load_json(args.enemies)
        if history:
            post_validation = history[-1].get(
                "post_apply_validation",
                {},
            )
            if (
                isinstance(post_validation, dict)
                and post_validation.get("status") == "pending"
            ):
                collected_validation_run = False
                candidate_snapshots: list[dict[str, Any]] = []
                for baseline in post_validation.get("baselines", []):
                    case = baseline["case"]
                    candidate = build_validation_snapshot(
                        args.logs,
                        fingerprint,
                        case,
                        targets,
                    )
                    if candidate["missing_seeds"]:
                        missing_seeds = [
                            int(seed) for seed in candidate["missing_seeds"]
                        ]
                        exhausted = budget_failure(
                            state,
                            config,
                            additional_runs=len(missing_seeds),
                        )
                        if exhausted is not None:
                            stop_state(
                                args.state,
                                state,
                                "budget_exhausted",
                                {
                                    **exhausted,
                                    "validation_case": case["id"],
                                },
                            )
                            return 2
                        summary = collect_runs(
                            project_root,
                            config,
                            len(missing_seeds),
                            controller_profile=str(
                                case["controller_profile"]
                            ),
                            build_profile=str(case["build_profile"]),
                            run_seeds=missing_seeds,
                            validation_variant=str(
                                case["validation_variant"]
                            ),
                        )
                        record_collection_usage(state, summary)
                        state["collection_batches"].append(
                            {
                                "collected_at": utc_now(),
                                "configuration_fingerprint": fingerprint,
                                "requested_runs": len(missing_seeds),
                                "completed_runs": int(
                                    summary["completed_runs"]
                                ),
                                "run_seeds": missing_seeds,
                                "controller_profile": str(
                                    case["controller_profile"]
                                ),
                                "build_profile": str(
                                    case["build_profile"]
                                ),
                                "validation_variant": str(
                                    case["validation_variant"]
                                ),
                                "purpose": "post_apply_validation",
                                "validation_case": str(case["id"]),
                            }
                        )
                        save_state(args.state, state)
                        collected_validation_run = True
                        break
                    candidate_snapshots.append(candidate)
                if collected_validation_run:
                    continue

                comparisons = [
                    compare_validation_snapshots(baseline, candidate)
                    for baseline, candidate in zip(
                        post_validation.get("baselines", []),
                        candidate_snapshots,
                    )
                ]
                failed = [
                    comparison
                    for comparison in comparisons
                    if not comparison["passed"]
                ]
                post_validation["completed_at"] = utc_now()
                post_validation["comparisons"] = comparisons
                if failed:
                    backup_directory = Path(
                        str(history[-1]["configuration_backup_directory"])
                    )
                    restored_fingerprint = restore_configuration_backup(
                        project_root,
                        backup_directory,
                    )
                    history[-1]["rolled_back_at"] = utc_now()
                    history[-1]["rollback_reason"] = (
                        "post_apply_validation_failed"
                    )
                    history[-1]["restored_configuration_fingerprint"] = (
                        restored_fingerprint
                    )
                    post_validation["status"] = "failed_rolled_back"
                    state["current_configuration_fingerprint"] = (
                        restored_fingerprint
                    )
                    stop_state(
                        args.state,
                        state,
                        "post_apply_validation_failed_rolled_back",
                        {
                            "failed_cases": failed,
                            "all_comparisons": comparisons,
                            "restored_configuration_fingerprint": (
                                restored_fingerprint
                            ),
                        },
                    )
                    return 2
                post_validation["status"] = "passed"
                save_state(args.state, state)
                print(
                    json.dumps(
                        {
                            "event": "post_apply_validation_passed",
                            "configuration_fingerprint": fingerprint,
                            "comparisons": comparisons,
                        },
                        ensure_ascii=False,
                    ),
                    flush=True,
                )
        evaluation = evaluate_current_cohort(
            args.logs,
            targets,
            policy,
            args.enemies,
            enemy_data,
            fingerprint,
            str(config["controller_profile"]),
            str(config["build_profile"]),
            config.get("sample_quality", {}),
            str(config.get("paired_validation", {}).get(
                "validation_variant",
                "",
            )),
        )
        readiness = assess_readiness(evaluation)
        run_report = evaluation["run_balance"]
        print(
            f"[AutoBalance] config={fingerprint[:12]} "
            f"runs={run_report['balance_sample_count']}/"
            f"{run_report['minimum_sample_count']} "
            f"status={readiness['status']}"
        )

        if readiness["status"] == "collect":
            batch_count = sum(
                str(batch.get("configuration_fingerprint", ""))
                == fingerprint
                for batch in state.get("collection_batches", [])
                if isinstance(batch, dict)
            )
            maximum_batches = int(
                config.get(
                    "maximum_collection_batches_per_fingerprint",
                    12,
                )
            )
            if batch_count >= maximum_batches:
                stop_state(
                    args.state,
                    state,
                    "sample_collection_limit_reached",
                    {
                        "configuration_fingerprint": fingerprint,
                        "collection_batch_count": batch_count,
                        "maximum_collection_batches": maximum_batches,
                        "readiness": readiness,
                        "sample_quality": evaluation["sample_quality"],
                        "run_balance": run_report,
                    },
                )
                return 2
            requested = min(
                int(config["collection_batch_runs"]),
                int(readiness["missing_runs"]),
            )
            exhausted = budget_failure(
                state,
                config,
                additional_runs=requested,
            )
            if exhausted is not None:
                stop_state(
                    args.state,
                    state,
                    "budget_exhausted",
                    exhausted,
                )
                return 2
            paired_settings = config.get("paired_validation", {})
            configured_seeds = [
                int(seed) for seed in paired_settings.get("seeds", [])
            ]
            existing_count = int(
                evaluation["run_selection"].get("matched_file_count", 0)
            )
            requested_seeds = (
                [
                    configured_seeds[
                        (existing_count + index) % len(configured_seeds)
                    ]
                    for index in range(requested)
                ]
                if configured_seeds
                else []
            )
            summary = collect_runs(
                project_root,
                config,
                requested,
                run_seeds=requested_seeds,
                validation_variant=str(
                    paired_settings.get("validation_variant", "")
                ),
            )
            record_collection_usage(state, summary)
            state["collection_batches"].append(
                {
                    "collected_at": utc_now(),
                    "configuration_fingerprint": fingerprint,
                    "requested_runs": requested,
                    "completed_runs": int(summary["completed_runs"]),
                    "run_seeds": requested_seeds,
                    "validation_variant": str(
                        paired_settings.get("validation_variant", "")
                    ),
                    "purpose": "primary_collection",
                }
            )
            save_state(args.state, state)
            continue

        if readiness["status"] == "balanced":
            stop_state(
                args.state,
                state,
                "balanced",
                {
                    "run_balance": run_report,
                    "sample_quality": evaluation["sample_quality"],
                },
                completed=True,
            )
            return 0

        if readiness["status"] == "stop":
            stop_state(
                args.state,
                state,
                str(readiness["reason"]),
                {
                    "run_balance": run_report,
                    "sample_quality": evaluation["sample_quality"],
                },
            )
            return 2

        improvement_failure = update_improvement_guard(
            state,
            fingerprint,
            float(run_report["balance_score"]),
            config,
        )
        if improvement_failure is not None:
            latest = state.get("history", [])[-1]
            backup_name = str(
                latest.get("configuration_backup_directory", "")
            )
            backup_directory = Path(backup_name) if backup_name else None
            if backup_directory is None or not backup_directory.is_dir():
                raise RuntimeError(
                    "No configuration backup is available for rollback"
                )
            restored_fingerprint = restore_configuration_backup(
                project_root,
                backup_directory,
            )
            latest["rolled_back_at"] = utc_now()
            latest["rollback_reason"] = "no_score_improvement"
            latest["restored_configuration_fingerprint"] = (
                restored_fingerprint
            )
            state["current_configuration_fingerprint"] = (
                restored_fingerprint
            )
            stop_state(
                args.state,
                state,
                "no_score_improvement_rolled_back",
                {
                    **improvement_failure,
                    "restored_configuration_fingerprint": (
                        restored_fingerprint
                    ),
                    "configuration_backup_directory": str(
                        backup_directory
                    ),
                },
            )
            return 2
        save_state(args.state, state)

        if int(state["adjustment_iteration"]) >= int(
            config["maximum_adjustment_iterations"]
        ):
            stop_state(
                args.state,
                state,
                "maximum_adjustment_iterations",
                {
                    "configuration_fingerprint": fingerprint,
                    "run_balance": run_report,
                    "sample_quality": evaluation["sample_quality"],
                },
            )
            return 2

        validation_baselines: list[dict[str, Any]] = []
        collected_baseline_run = False
        for case in validation_cases(config):
            case_seeds = [int(seed) for seed in case["run_seeds"]]
            fresh_baseline_collected = any(
                isinstance(batch, dict)
                and batch.get("purpose") == "pre_apply_validation_baseline"
                and str(batch.get("configuration_fingerprint", ""))
                == fingerprint
                and str(batch.get("validation_case", ""))
                == str(case["id"])
                and [int(seed) for seed in batch.get("run_seeds", [])]
                == case_seeds
                for batch in state.get("collection_batches", [])
            )
            if not fresh_baseline_collected:
                exhausted = budget_failure(
                    state,
                    config,
                    additional_runs=len(case_seeds),
                )
                if exhausted is not None:
                    stop_state(
                        args.state,
                        state,
                        "budget_exhausted",
                        {
                            **exhausted,
                            "validation_case": case["id"],
                        },
                    )
                    return 2
                summary = collect_runs(
                    project_root,
                    config,
                    len(case_seeds),
                    controller_profile=str(case["controller_profile"]),
                    build_profile=str(case["build_profile"]),
                    run_seeds=case_seeds,
                    validation_variant=str(case["validation_variant"]),
                )
                record_collection_usage(state, summary)
                state["collection_batches"].append(
                    {
                        "collected_at": utc_now(),
                        "configuration_fingerprint": fingerprint,
                        "requested_runs": len(case_seeds),
                        "completed_runs": int(summary["completed_runs"]),
                        "run_seeds": case_seeds,
                        "controller_profile": str(
                            case["controller_profile"]
                        ),
                        "build_profile": str(case["build_profile"]),
                        "validation_variant": str(
                            case["validation_variant"]
                        ),
                        "purpose": "pre_apply_validation_baseline",
                        "validation_case": str(case["id"]),
                    }
                )
                save_state(args.state, state)
                collected_baseline_run = True
                break
            baseline = build_validation_snapshot(
                args.logs,
                fingerprint,
                case,
                targets,
            )
            if baseline["missing_seeds"]:
                missing_seeds = [
                    int(seed) for seed in baseline["missing_seeds"]
                ]
                exhausted = budget_failure(
                    state,
                    config,
                    additional_runs=len(missing_seeds),
                )
                if exhausted is not None:
                    stop_state(
                        args.state,
                        state,
                        "budget_exhausted",
                        {
                            **exhausted,
                            "validation_case": case["id"],
                        },
                    )
                    return 2
                summary = collect_runs(
                    project_root,
                    config,
                    len(missing_seeds),
                    controller_profile=str(case["controller_profile"]),
                    build_profile=str(case["build_profile"]),
                    run_seeds=missing_seeds,
                    validation_variant=str(case["validation_variant"]),
                )
                record_collection_usage(state, summary)
                state["collection_batches"].append(
                    {
                        "collected_at": utc_now(),
                        "configuration_fingerprint": fingerprint,
                        "requested_runs": len(missing_seeds),
                        "completed_runs": int(summary["completed_runs"]),
                        "run_seeds": missing_seeds,
                        "controller_profile": str(
                            case["controller_profile"]
                        ),
                        "build_profile": str(case["build_profile"]),
                        "validation_variant": str(
                            case["validation_variant"]
                        ),
                        "purpose": "pre_apply_validation_baseline",
                        "validation_case": str(case["id"]),
                    }
                )
                save_state(args.state, state)
                collected_baseline_run = True
                break
            validation_baselines.append(baseline)
        if collected_baseline_run:
            continue

        exhausted = budget_failure(
            state,
            config,
            additional_ai_calls=1,
        )
        if exhausted is not None:
            stop_state(
                args.state,
                state,
                "budget_exhausted",
                exhausted,
            )
            return 2

        iteration = int(state["adjustment_iteration"]) + 1
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        plan_path = args.plan_directory / (
            f"iteration_{iteration:02d}_{fingerprint[:12]}_{timestamp}.json"
        )
        plan = generate_ai_proposal(
            project_root,
            args,
            config,
            fingerprint,
            plan_path,
        )
        record_ai_usage(state, plan, config)
        save_state(args.state, state)
        exhausted = budget_failure(state, config)
        if exhausted is not None:
            stop_state(
                args.state,
                state,
                "budget_exhausted_after_ai_response",
                {
                    **exhausted,
                    "plan_file": str(plan_path),
                },
            )
            return 2
        changes = plan.get("changes", [])
        if not changes:
            stop_state(
                args.state,
                state,
                "ai_declined_changes",
                {"plan_file": str(plan_path)},
            )
            return 2

        oscillation = detect_change_oscillation(
            changes,
            state.get("history", []),
            config.get("oscillation", {}),
        )
        if oscillation is not None:
            stop_state(
                args.state,
                state,
                "oscillation_detected",
                {"plan_file": str(plan_path), **oscillation},
            )
            return 2

        fingerprint_before_apply = current_configuration_fingerprint(
            project_root
        )
        if fingerprint_before_apply != fingerprint:
            stop_state(
                args.state,
                state,
                "configuration_changed_before_apply",
                {
                    "evaluated_configuration_fingerprint": fingerprint,
                    "actual_configuration_fingerprint": (
                        fingerprint_before_apply
                    ),
                    "plan_file": str(plan_path),
                },
            )
            return 2

        projected_hash = projected_enemy_status_sha256(plan, enemy_data)
        if (
            config.get("oscillation", {}).get(
                "stop_on_repeated_enemy_configuration",
                True,
            )
            and projected_hash in seen_enemy_status_hashes(state)
        ):
            stop_state(
                args.state,
                state,
                "repeated_enemy_configuration",
                {
                    "plan_file": str(plan_path),
                    "projected_enemy_status_sha256": projected_hash,
                },
            )
            return 2

        configuration_backup_directory = create_configuration_backup(
            project_root,
            args.backup_directory,
            fingerprint,
            iteration,
        )
        state["pending_apply"] = {
            "iteration": iteration,
            "configuration_fingerprint_before": fingerprint,
            "configuration_backup_directory": str(
                configuration_backup_directory
            ),
            "plan_file": str(plan_path),
        }
        save_state(args.state, state)
        enemy_hash_before = file_sha256(args.enemies)
        try:
            backup_path = apply_plan(
                plan,
                args.enemies,
                enemy_data,
                args.backup_directory,
            )
            if backup_path is None:
                state["pending_apply"] = {}
                stop_state(
                    args.state,
                    state,
                    "validated_plan_had_no_changes",
                    {"plan_file": str(plan_path)},
                )
                return 2

            plan["mode"] = "auto_ai_applied"
            plan["applied_at"] = utc_now()
            plan["backup_file"] = str(backup_path)
            plan["configuration_backup_directory"] = str(
                configuration_backup_directory
            )
            write_json(plan_path, plan)
            new_fingerprint = current_configuration_fingerprint(project_root)
            enemy_hash_after = file_sha256(args.enemies)
            if new_fingerprint == fingerprint:
                raise RuntimeError(
                    "Applied change did not create a new fingerprint"
                )
        except Exception as error:
            restored_fingerprint = restore_configuration_backup(
                project_root,
                configuration_backup_directory,
            )
            state["pending_apply"] = {}
            state["current_configuration_fingerprint"] = (
                restored_fingerprint
            )
            save_state(args.state, state)
            raise RuntimeError(
                "Apply failed and the previous configuration was restored: "
                f"{error}"
            ) from error
        state["history"].append(
            {
                "iteration": iteration,
                "applied_at": utc_now(),
                "configuration_fingerprint_before": fingerprint,
                "configuration_fingerprint_after": new_fingerprint,
                "enemy_status_sha256_before": enemy_hash_before,
                "enemy_status_sha256_after": enemy_hash_after,
                "evaluation_before": {
                    "run_balance_score": float(run_report["balance_score"]),
                    "run_judgement": str(run_report["judgement"]),
                    "balance_sample_count": int(
                        run_report["balance_sample_count"]
                    ),
                },
                "plan_file": str(plan_path),
                "backup_file": str(backup_path),
                "configuration_backup_directory": str(
                    configuration_backup_directory
                ),
                "post_apply_validation": {
                    "status": (
                        "pending" if validation_baselines else "disabled"
                    ),
                    "baselines": validation_baselines,
                },
                "changes": changes,
            }
        )
        state["pending_apply"] = {}
        state["adjustment_iteration"] = iteration
        state["current_configuration_fingerprint"] = new_fingerprint
        save_state(args.state, state)
        print(
            f"[AutoBalance] applied iteration {iteration}; "
            f"new config={new_fingerprint[:12]}. Collecting a fresh cohort."
        )


def main() -> int:
    args = parse_arguments()
    normalize_argument_paths(args)
    try:
        if args.dry_run:
            return run_loop(args)
        with AutoBalanceLock(args.lock_file):
            return run_loop(args)
    except LockUnavailableError as error:
        print(
            json.dumps(
                {
                    "event": "auto_balance_lock_unavailable",
                    "reason": str(error),
                    "lock_file": str(args.lock_file),
                },
                ensure_ascii=False,
            )
        )
        return 3
    except (
        OSError,
        json.JSONDecodeError,
        KeyError,
        TypeError,
        ValueError,
        RuntimeError,
    ) as error:
        if not args.dry_run:
            state = load_json(args.state) if args.state.exists() else new_state()
            stop_state(
                args.state,
                state,
                "error",
                {"message": str(error)},
            )
        print(f"Auto balance loop failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
