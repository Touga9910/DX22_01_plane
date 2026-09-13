"""バランス調整ランに紐づく人間のプレイテスト意見を記録・集計する。"""

from __future__ import annotations

import argparse
import json
import statistics
import uuid
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_DIRECTORY = Path("logs/balance_feedback")
RATING_FIELDS = (
    "decision_clarity",
    "control_feel",
    "result_trust",
    "enjoyment",
)


def utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def validate_rating(value: int) -> int:
    if not 1 <= value <= 5:
        raise ValueError("ratings must be between 1 and 5")
    return value


def write_feedback(
    output_directory: Path,
    *,
    run_id: str,
    player_profile: str,
    ratings: dict[str, int],
    issue_tags: list[str],
    note: str,
) -> Path:
    validated = {
        field: validate_rating(int(ratings[field]))
        for field in RATING_FIELDS
    }
    feedback_id = f"feedback_{uuid.uuid4().hex}"
    payload = {
        "schema_version": 1,
        "feedback_id": feedback_id,
        "recorded_at": utc_now(),
        "run_id": run_id,
        "player_profile": player_profile,
        "ratings": validated,
        "issue_tags": sorted({tag.strip() for tag in issue_tags if tag.strip()}),
        "note": note,
    }
    output_directory.mkdir(parents=True, exist_ok=True)
    path = output_directory / f"{feedback_id}.json"
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return path


def aggregate_feedback(
    directory: Path,
    allowed_run_ids: set[str] | None = None,
) -> dict[str, Any]:
    rating_values: dict[str, list[int]] = {
        field: [] for field in RATING_FIELDS
    }
    issue_counts: Counter[str] = Counter()
    profile_counts: Counter[str] = Counter()
    linked_run_ids: set[str] = set()
    invalid_files: list[str] = []
    feedback_count = 0
    excluded_feedback_count = 0
    if directory.exists():
        for path in sorted(directory.glob("feedback_*.json")):
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(payload, dict):
                    raise ValueError("root must be an object")
                ratings = payload.get("ratings", {})
                if not isinstance(ratings, dict):
                    raise ValueError("ratings must be an object")
                run_id = str(payload.get("run_id", ""))
                if allowed_run_ids is not None and run_id not in allowed_run_ids:
                    excluded_feedback_count += 1
                    continue
                for field in RATING_FIELDS:
                    rating_values[field].append(
                        validate_rating(int(ratings[field]))
                    )
                feedback_count += 1
                if run_id:
                    linked_run_ids.add(run_id)
                profile_counts[str(payload.get("player_profile", "unknown"))] += 1
                tags = payload.get("issue_tags", [])
                if isinstance(tags, list):
                    issue_counts.update(str(tag) for tag in tags)
            except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
                invalid_files.append(str(path))
    return {
        "schema_version": 1,
        "feedback_count": feedback_count,
        "excluded_feedback_count": excluded_feedback_count,
        "linked_run_count": len(linked_run_ids),
        "average_ratings": {
            field: round(statistics.fmean(values), 2) if values else 0.0
            for field, values in rating_values.items()
        },
        "player_profiles": dict(sorted(profile_counts.items())),
        "issue_tags": dict(sorted(issue_counts.items())),
        "invalid_files": invalid_files,
        "note": (
            "Human ratings complement automated telemetry and are not "
            "included in automatic parameter changes."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Record one human balance-playtest response."
    )
    parser.add_argument("--output-directory", type=Path, default=DEFAULT_DIRECTORY)
    parser.add_argument("--run-id", required=True)
    parser.add_argument(
        "--player-profile",
        choices=("beginner", "intermediate", "advanced"),
        required=True,
    )
    for field in RATING_FIELDS:
        parser.add_argument(
            f"--{field.replace('_', '-')}",
            type=int,
            required=True,
        )
    parser.add_argument("--issue-tag", action="append", default=[])
    parser.add_argument("--note", default="")
    args = parser.parse_args()
    try:
        path = write_feedback(
            args.output_directory,
            run_id=args.run_id,
            player_profile=args.player_profile,
            ratings={field: getattr(args, field) for field in RATING_FIELDS},
            issue_tags=args.issue_tag,
            note=args.note,
        )
    except ValueError as error:
        print(f"Failed to record feedback: {error}")
        return 1
    print(f"Wrote playtest feedback to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
