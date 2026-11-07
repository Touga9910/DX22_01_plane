"""Caromino のバランス版と管理対象設定のハッシュを管理する。

プロジェクトディレクトリから実行する:
    python tools/balance_version.py status
    python tools/balance_version.py bump --target enemy_hp \
        --before "normal: 100" --after "normal: 110" \
        --reason "終盤の生存時間を延ばす" \
        --expected-impact "平均戦闘ターン数が増える"
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


DEFAULT_REGISTRY = Path("assets/data/balance_version.json")
VERSION_PATTERN = re.compile(r"^B(\d{4,})$")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def calculate_config_hash(
    project_root: Path,
    managed_files: list[str],
) -> tuple[str, list[str]]:
    """パスとファイル内容から改行差も検知する安定した SHA-256 を返す。"""

    digest = hashlib.sha256()
    missing: list[str] = []
    for relative_path in sorted(set(managed_files)):
        normalized = relative_path.replace("\\", "/")
        path = project_root / Path(normalized)
        digest.update(normalized.encode("utf-8"))
        digest.update(b"\0")
        if not path.is_file():
            missing.append(normalized)
            digest.update(b"<missing>")
        else:
            digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest(), missing


def current_git_commit(project_root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=project_root,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return result.stdout.strip() or "unknown"


def utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def next_version(version: str) -> str:
    match = VERSION_PATTERN.fullmatch(version)
    if not match:
        raise ValueError(f"Unsupported balance version: {version}")
    width = len(match.group(1))
    return f"B{int(match.group(1)) + 1:0{width}d}"


def resolve_history_path(
    project_root: Path,
    registry: dict[str, Any],
) -> Path:
    value = registry.get(
        "history_file",
        "assets/data/balance_history.json",
    )
    return project_root / Path(str(value))


def validate_registry(
    project_root: Path,
    registry: dict[str, Any],
) -> tuple[str, list[str], list[str]]:
    managed = registry.get("managed_files", [])
    if not isinstance(managed, list) or not all(
        isinstance(path, str) for path in managed
    ):
        raise ValueError("managed_files must be a string array")
    actual_hash, missing = calculate_config_hash(project_root, managed)
    errors: list[str] = []
    version = str(registry.get("current_version", ""))
    if not VERSION_PATTERN.fullmatch(version):
        errors.append(f"invalid current_version: {version or '<missing>'}")
    expected_hash = str(registry.get("current_config_hash", ""))
    if actual_hash != expected_hash:
        errors.append(
            "managed balance settings changed without a version bump "
            f"(registered={expected_hash or '<missing>'}, actual={actual_hash})"
        )
    if missing:
        errors.append("managed files are missing: " + ", ".join(missing))
    return actual_hash, missing, errors


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def command_status(args: argparse.Namespace) -> int:
    project_root = args.project_root.resolve()
    registry_path = project_root / args.registry
    try:
        registry = load_json(registry_path)
        actual_hash, _, errors = validate_registry(project_root, registry)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"[error] {error}")
        return 2

    print(f"balance_version={registry.get('current_version', '')}")
    print(f"balance_config_hash={actual_hash}")
    print(f"managed_file_count={len(registry.get('managed_files', []))}")
    if errors:
        for error in errors:
            print(f"[error] {error}")
        return 1
    print("[ok] managed balance settings match the registered version")
    return 0


def command_bump(args: argparse.Namespace) -> int:
    project_root = args.project_root.resolve()
    registry_path = project_root / args.registry
    try:
        registry = load_json(registry_path)
        old_version = str(registry["current_version"])
        new_version = next_version(old_version)
        managed = registry["managed_files"]
        new_hash, missing = calculate_config_hash(project_root, managed)
    except (KeyError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"[error] {error}")
        return 2
    if missing:
        print("[error] managed files are missing: " + ", ".join(missing))
        return 2

    changed_at = utc_now()
    history_path = resolve_history_path(project_root, registry)
    try:
        history = load_json(history_path)
    except FileNotFoundError:
        history = {"schema_version": 1, "entries": []}
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"[error] {error}")
        return 2
    entries = history.setdefault("entries", [])
    if not isinstance(entries, list):
        print("[error] balance history entries must be an array")
        return 2

    entry = {
        "version": new_version,
        "changed_at": changed_at,
        "target": args.target,
        "before": args.before,
        "after": args.after,
        "reason": args.reason,
        "expected_impact": args.expected_impact,
        "actual_result": None,
        "balance_config_hash": new_hash,
        "git_commit": current_git_commit(project_root),
    }
    entries.append(entry)
    registry["current_version"] = new_version
    registry["current_config_hash"] = new_hash
    registry["registered_at"] = changed_at
    registry["registered_git_commit"] = entry["git_commit"]

    write_json(history_path, history)
    write_json(registry_path, registry)
    print(f"Bumped {old_version} -> {new_version}")
    print(f"balance_config_hash={new_hash}")
    return 0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate or bump the Caromino balance version."
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="Game project directory containing assets/ and tools/.",
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=DEFAULT_REGISTRY,
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("status")
    bump = subparsers.add_parser("bump")
    bump.add_argument("--target", required=True)
    bump.add_argument("--before", required=True)
    bump.add_argument("--after", required=True)
    bump.add_argument("--reason", required=True)
    bump.add_argument("--expected-impact", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if args.command == "status":
        return command_status(args)
    if args.command == "bump":
        return command_bump(args)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
