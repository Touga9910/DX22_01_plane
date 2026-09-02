"""敵ステータスに対する保守的なバランス変更を作成し、必要に応じて適用する。

ゲームプロジェクトのディレクトリから実行する:
    python tools/adjust_balance_from_logs.py
    python tools/adjust_balance_from_logs.py --apply

既定のコマンドは提案の書き出しだけを行う。``--apply``を指定すると、
分析後に敵JSONが変更されていないことを検証し、バックアップを作成してから
提案された変更を適用する。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from analyze_balance_logs import (
    RunFilters,
    apply_boss_revaluation_gate,
    calculate_stage_report,
    collect_filtered_stage_records,
    load_json,
    resolve_stage_metric_targets,
)


DEFAULT_LOG_DIRECTORY = Path("logs/balance")
DEFAULT_TARGET_FILE = Path("assets/data/balance_targets.json")
DEFAULT_POLICY_FILE = Path("assets/data/balance_adjustment_policy.json")
DEFAULT_ENEMY_FILE = Path("assets/data/enemy_data.json")
DEFAULT_PLAN_FILE = Path("logs/balance/balance_adjustment_plan.json")
DEFAULT_BACKUP_DIRECTORY = Path("logs/balance/backups")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Read balance run logs and propose conservative enemy status changes."
        )
    )
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOG_DIRECTORY)
    parser.add_argument("--targets", type=Path, default=DEFAULT_TARGET_FILE)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY_FILE)
    parser.add_argument("--enemies", type=Path, default=DEFAULT_ENEMY_FILE)
    parser.add_argument("--output", type=Path, default=DEFAULT_PLAN_FILE)
    parser.add_argument(
        "--backup-directory",
        type=Path,
        default=DEFAULT_BACKUP_DIRECTORY,
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply the generated plan after making a timestamped backup.",
    )
    configuration_group = parser.add_mutually_exclusive_group()
    configuration_group.add_argument(
        "--latest-configuration",
        action="store_true",
        help=(
            "Use only the newest configuration cohort. This is the default "
            "when no configuration fingerprint is specified."
        ),
    )
    configuration_group.add_argument(
        "--configuration-fingerprint",
        help=(
            "Use only one configuration-suite fingerprint or an "
            "unambiguous prefix."
        ),
    )
    parser.add_argument(
        "--controller-profile",
        choices=("beginner", "intermediate", "advanced"),
        help="Include only this MCP player profile.",
    )
    parser.add_argument(
        "--build-profile",
        help="Include only this MCP build profile.",
    )
    return parser.parse_args()


def utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def positive_distance(
    value: float,
    boundary: float,
    tolerance: float,
    value_is_too_high: bool,
) -> float:
    distance = value - boundary if value_is_too_high else boundary - value
    return max(0.0, distance) / max(tolerance, 0.000001)


def choose_adjustment_field(
    stage_report: dict[str, Any],
    targets: dict[str, Any],
) -> tuple[str | None, float]:
    metrics = stage_report["metrics"]
    judgement = stage_report["judgement"]
    metric_targets = resolve_stage_metric_targets(
        targets,
        str(stage_report.get("stage_type", "unknown")),
    )

    if judgement == "too_easy":
        clear_pressure = positive_distance(
            metrics["clear_rate"],
            metric_targets["clear_rate"]["max"],
            metric_targets["clear_rate"]["tolerance"],
            True,
        )
        remaining_hp_pressure = positive_distance(
            metrics["average_remaining_hp_ratio"],
            metric_targets["average_remaining_hp_ratio"]["max"],
            metric_targets["average_remaining_hp_ratio"]["tolerance"],
            True,
        )
        shot_pressure = positive_distance(
            metrics["median_shots"],
            metric_targets["median_shots"]["min"],
            metric_targets["median_shots"]["tolerance"],
            False,
        )
        collision_pressure = positive_distance(
            metrics["average_collision_effect"],
            metric_targets["average_collision_effect"]["max"],
            metric_targets["average_collision_effect"]["tolerance"],
            True,
        )
    elif judgement == "too_difficult":
        clear_pressure = positive_distance(
            metrics["clear_rate"],
            metric_targets["clear_rate"]["min"],
            metric_targets["clear_rate"]["tolerance"],
            False,
        )
        remaining_hp_pressure = positive_distance(
            metrics["average_remaining_hp_ratio"],
            metric_targets["average_remaining_hp_ratio"]["min"],
            metric_targets["average_remaining_hp_ratio"]["tolerance"],
            False,
        )
        shot_pressure = positive_distance(
            metrics["median_shots"],
            metric_targets["median_shots"]["max"],
            metric_targets["median_shots"]["tolerance"],
            True,
        )
        collision_pressure = 0.0
    else:
        return None, 0.0

    # 攻撃力は主にプレイヤーの残HPを、最大HPは主にステージの長さを左右する。
    # どちらもクリア率へ影響するため、クリア率は両方の判断材料に含める。
    attack_pressure = remaining_hp_pressure + clear_pressure * 0.5
    max_hp_pressure = (
        shot_pressure +
        collision_pressure * 0.25 +
        clear_pressure * 0.5
    )

    if attack_pressure <= 0.0 and max_hp_pressure <= 0.0:
        # 問題が照準またはノーヒット率だけの場合は、戦闘ステータスを変更しない。
        return None, 0.0
    if attack_pressure >= max_hp_pressure:
        return "attack", attack_pressure
    return "maxHp", max_hp_pressure


def enemy_counts_for_records(
    records: list[dict[str, Any]],
) -> dict[str, int]:
    counts: dict[str, int] = defaultdict(int)
    for record in records:
        seen_in_stage: dict[str, int] = defaultdict(int)
        enemies = (
            record["stage"]
            .get("stage_start", {})
            .get("enemies", [])
        )
        for enemy in enemies:
            enemy_id = str(enemy.get("id", ""))
            if enemy_id:
                seen_in_stage[enemy_id] += 1

        for enemy_id, count in seen_in_stage.items():
            counts[enemy_id] += count
    return dict(counts)


def build_adjustment_plan(
    log_directory: Path,
    targets: dict[str, Any],
    policy: dict[str, Any],
    enemy_file: Path,
    enemy_data: dict[str, Any],
    *,
    configuration_fingerprint: str | None = None,
    latest_configuration: bool = True,
    controller_profile: str | None = None,
    build_profile: str | None = None,
) -> dict[str, Any]:
    normalized_fingerprint = (
        configuration_fingerprint.strip().lower()
        if configuration_fingerprint
        else ""
    )
    filters = RunFilters(
        controller_types=("mcp",)
        if controller_profile or build_profile
        else (),
        controller_profiles=(controller_profile,)
        if controller_profile
        else (),
        build_profiles=(build_profile,) if build_profile else (),
        configuration_fingerprints=(normalized_fingerprint,)
        if normalized_fingerprint
        else (),
    )
    grouped, analyzed_file_count, run_selection, selected_runs = (
        collect_filtered_stage_records(
            log_directory,
            filters=filters,
            latest_configuration=(
                latest_configuration and not normalized_fingerprint
            ),
        )
    )
    selected_configurations = run_selection[
        "selected_configuration_fingerprints"
    ]
    if len(selected_configurations) > 1:
        raise ValueError(
            "Configuration fingerprint matched multiple cohorts; use a "
            "longer, unambiguous fingerprint."
        )
    stage_reports = {
        key: calculate_stage_report(
            key[0],
            key[1],
            records,
            targets,
        )
        for key, records in grouped.items()
    }
    boss_revaluation = apply_boss_revaluation_gate(
        list(stage_reports.values()),
        grouped,
        analyzed_file_count,
        targets,
    )

    votes: dict[str, dict[str, list[dict[str, Any]]]] = defaultdict(
        lambda: defaultdict(list)
    )
    ignored_stage_count = 0

    for key, report in sorted(stage_reports.items()):
        if not report["adjustment_required"]:
            ignored_stage_count += 1
            continue

        field, pressure = choose_adjustment_field(report, targets)
        if field is None:
            ignored_stage_count += 1
            continue

        records = grouped[key]
        enemy_counts = enemy_counts_for_records(records)
        total_enemy_count = sum(enemy_counts.values())
        if total_enemy_count <= 0:
            ignored_stage_count += 1
            continue

        direction = 1 if report["judgement"] == "too_easy" else -1
        for enemy_id, enemy_count in enemy_counts.items():
            enemy_share = enemy_count / total_enemy_count
            weight = (
                float(report["adjustment_priority"])
                * max(pressure, 0.1)
                * enemy_share
            )
            votes[enemy_id][field].append(
                {
                    "stage_id": report["stage_id"],
                    "difficulty": report["difficulty"],
                    "sample_count": report["sample_count"],
                    "judgement": report["judgement"],
                    "balance_score": report["balance_score"],
                    "direction": direction,
                    "weight": weight,
                }
            )

    enemy_by_id = {
        str(enemy.get("id", "")): enemy
        for enemy in enemy_data.get("enemies", [])
        if enemy.get("id")
    }
    minimum_confidence = float(policy["minimum_enemy_confidence"])
    max_step = max(1, int(policy["maximum_step_per_iteration"]))
    allowed_fields = set(policy["adjustable_fields"])
    changes: list[dict[str, Any]] = []
    skipped: list[dict[str, Any]] = []

    for enemy_id, field_votes in sorted(votes.items()):
        enemy = enemy_by_id.get(enemy_id)
        if enemy is None:
            skipped.append(
                {"enemy_id": enemy_id, "reason": "enemy_not_found"}
            )
            continue

        candidates: list[tuple[float, str, float, float, list[dict[str, Any]]]] = []
        for field, evidence in field_votes.items():
            if field not in allowed_fields:
                continue
            signed_weight = sum(
                item["direction"] * item["weight"] for item in evidence
            )
            total_weight = sum(abs(item["weight"]) for item in evidence)
            confidence = (
                abs(signed_weight) / total_weight if total_weight > 0.0 else 0.0
            )
            candidates.append(
                (
                    abs(signed_weight),
                    field,
                    signed_weight,
                    confidence,
                    evidence,
                )
            )

        if not candidates:
            continue

        _, field, signed_weight, confidence, evidence = max(candidates)
        if confidence < minimum_confidence or signed_weight == 0.0:
            skipped.append(
                {
                    "enemy_id": enemy_id,
                    "reason": "conflicting_evidence",
                    "confidence": round(confidence, 4),
                }
            )
            continue

        status = enemy.get("status", {})
        if field not in status:
            skipped.append(
                {
                    "enemy_id": enemy_id,
                    "reason": f"missing_status_field:{field}",
                }
            )
            continue

        direction = 1 if signed_weight > 0.0 else -1
        field_limits = policy["limits"][field]
        before = int(status[field])
        after = max(
            int(field_limits["min"]),
            min(
                int(field_limits["max"]),
                before + direction * max_step,
            ),
        )
        if after == before:
            skipped.append(
                {
                    "enemy_id": enemy_id,
                    "reason": f"limit_reached:{field}",
                }
            )
            continue

        changes.append(
            {
                "target_type": "enemy",
                "target_id": enemy_id,
                "field": f"status.{field}",
                "before": before,
                "after": after,
                "step": after - before,
                "direction": "harder" if direction > 0 else "easier",
                "confidence": round(confidence, 4),
                "evidence": [
                    {
                        key: (
                            round(value, 4)
                            if isinstance(value, float)
                            else value
                        )
                        for key, value in item.items()
                    }
                    for item in evidence
                ],
            }
        )

    return {
        "schema_version": 2,
        "generated_at": utc_now(),
        "mode": "proposal",
        "source": {
            "log_directory": str(log_directory),
            "analyzed_file_count": analyzed_file_count,
            "evaluated_stage_count": len(stage_reports),
            "ignored_stage_count": ignored_stage_count,
            "enemy_status_file": str(enemy_file),
            "enemy_status_sha256": file_sha256(enemy_file),
            "configuration_fingerprint": next(
                iter(selected_configurations),
                "",
            ),
            "selected_log_files": [
                str(log_path) for log_path, _, _ in selected_runs
            ],
            "run_selection": run_selection,
            "boss_revaluation": boss_revaluation,
        },
        "safety": {
            "minimum_stage_sample_count": int(
                targets["minimum_sample_count"]
            ),
            "minimum_enemy_confidence": minimum_confidence,
            "maximum_step_per_iteration": max_step,
            "requires_explicit_apply": True,
            "requires_single_configuration": True,
        },
        "summary": {
            "proposed_change_count": len(changes),
            "skipped_target_count": len(skipped),
        },
        "changes": changes,
        "skipped": skipped,
        "stage_evaluations": [
            stage_reports[key] for key in sorted(stage_reports)
        ],
    }


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = path.with_suffix(path.suffix + ".tmp")
    with temporary_path.open("w", encoding="utf-8") as destination:
        json.dump(value, destination, ensure_ascii=False, indent=2)
        destination.write("\n")
    temporary_path.replace(path)


def apply_plan(
    plan: dict[str, Any],
    enemy_file: Path,
    enemy_data: dict[str, Any],
    backup_directory: Path,
) -> Path | None:
    expected_hash = plan["source"]["enemy_status_sha256"]
    if file_sha256(enemy_file) != expected_hash:
        raise RuntimeError(
            "Enemy status JSON changed after analysis; regenerate the plan."
        )

    changes = plan["changes"]
    if not changes:
        return None

    enemy_by_id = {
        str(enemy.get("id", "")): enemy
        for enemy in enemy_data.get("enemies", [])
        if enemy.get("id")
    }
    for change in changes:
        enemy = enemy_by_id.get(change["target_id"])
        if enemy is None:
            raise RuntimeError(
                f"Enemy disappeared before apply: {change['target_id']}"
            )
        field = str(change["field"]).removeprefix("status.")
        current_value = int(enemy["status"][field])
        if current_value != int(change["before"]):
            raise RuntimeError(
                f"Status changed before apply: {change['target_id']} {field}"
            )
        enemy["status"][field] = int(change["after"])

    backup_directory.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_path = backup_directory / (
        f"{enemy_file.stem}_{timestamp}{enemy_file.suffix}"
    )
    shutil.copy2(enemy_file, backup_path)
    write_json(enemy_file, enemy_data)
    return backup_path


def main() -> int:
    args = parse_arguments()

    try:
        targets = load_json(args.targets)
        policy = load_json(args.policy)
        enemy_data = load_json(args.enemies)
        plan = build_adjustment_plan(
            args.logs,
            targets,
            policy,
            args.enemies,
            enemy_data,
            configuration_fingerprint=args.configuration_fingerprint,
            latest_configuration=(
                args.latest_configuration
                or args.configuration_fingerprint is None
            ),
            controller_profile=args.controller_profile,
            build_profile=args.build_profile,
        )
        write_json(args.output, plan)
    except (
        OSError,
        json.JSONDecodeError,
        KeyError,
        TypeError,
        ValueError,
    ) as error:
        print(f"Failed to generate adjustment plan: {error}")
        return 1

    print(
        f"Wrote {plan['summary']['proposed_change_count']} proposed changes "
        f"from {plan['source']['analyzed_file_count']} run logs "
        f"to {args.output}"
    )

    if not args.apply:
        print("Proposal only. Run again with --apply to change enemy status.")
        return 0

    try:
        backup_path = apply_plan(
            plan,
            args.enemies,
            enemy_data,
            args.backup_directory,
        )
    except (OSError, RuntimeError, KeyError, TypeError) as error:
        print(f"Failed to apply adjustment plan: {error}")
        return 1

    if backup_path is None:
        print("No changes were applied because the evidence was insufficient.")
        return 0

    plan["mode"] = "applied"
    plan["applied_at"] = utc_now()
    plan["backup_file"] = str(backup_path)
    write_json(args.output, plan)
    print(f"Applied changes. Backup: {backup_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
