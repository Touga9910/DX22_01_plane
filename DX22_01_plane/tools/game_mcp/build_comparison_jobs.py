"""Background process wrapper for fixed build-comparison collection."""

from __future__ import annotations

import csv
import json
import os
import subprocess
import sys
import threading
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from uuid import uuid4


SUPPORTED_BUILDS = ("standard", "heavy", "pierce", "bounce", "anchor")


def _utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return []
    values = []
    for line in lines:
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            values.append(value)
    return values


class BuildComparisonJobManager:
    """Start one collector subprocess and expose non-blocking status/results."""

    def __init__(
        self,
        project_root: Path,
        mcp_url: str,
        python_executable: Path | None = None,
    ) -> None:
        self.project_root = project_root.resolve()
        self.mcp_url = mcp_url
        self.python_executable = Path(python_executable or sys.executable).resolve()
        self.script_path = (
            self.project_root / "tools" / "game_mcp" / "collect_fixed_balance_runs.py"
        )
        self.output_root = self.project_root / "logs" / "balance" / "mcp_comparisons"
        self._lock = threading.RLock()
        self._process: subprocess.Popen[Any] | None = None
        self._output_directory: Path | None = None
        self._request: dict[str, Any] = {}
        self._started_at = ""

    @staticmethod
    def validate_request(
        seeds: list[int],
        build_profiles: list[str],
        player_level: str,
        validation_variant: str,
        valid_runs_per_build: int,
        dynamic_balance: bool,
    ) -> None:
        if not seeds:
            raise ValueError("seeds must contain at least one seed")
        if len(set(seeds)) != len(seeds):
            raise ValueError("seeds must be unique for paired comparison")
        if any(seed < 0 or seed > 0xFFFFFFFF for seed in seeds):
            raise ValueError("each seed must be between 0 and 4294967295")
        if not build_profiles:
            raise ValueError("build_profiles must contain at least one build")
        if len(set(build_profiles)) != len(build_profiles):
            raise ValueError("build_profiles must be unique")
        unsupported = [
            build for build in build_profiles if build not in SUPPORTED_BUILDS
        ]
        if unsupported:
            raise ValueError(f"unsupported build_profiles: {unsupported}")
        if player_level != "intermediate":
            raise ValueError("build comparison currently requires intermediate")
        if validation_variant != "fixed":
            raise ValueError("build comparison requires validation_variant=fixed")
        if dynamic_balance:
            raise ValueError("build comparison requires dynamic_balance=false")
        if valid_runs_per_build != len(seeds):
            raise ValueError(
                "valid_runs_per_build must equal the number of paired seeds"
            )

    def _paths(self) -> dict[str, str]:
        directory = self._output_directory
        if directory is None:
            return {}
        return {
            "output_directory": str(directory),
            "all_runs_jsonl": str(directory / "all_runs.jsonl"),
            "build_summaries_json": str(directory / "build_summaries.json"),
            "seed_build_comparison_csv": str(
                directory / "seed_build_comparison.csv"
            ),
            "errors_and_exclusions_jsonl": str(
                directory / "errors_and_exclusions.jsonl"
            ),
            "final_comparison_report": str(
                directory / "final_comparison_report.md"
            ),
            "status_json": str(directory / "comparison_status.json"),
            "request_json": str(directory / "comparison_request.json"),
            "collector_stdout": str(directory / "collector_stdout.jsonl"),
            "collector_stderr": str(directory / "collector_stderr.log"),
        }

    def start(
        self,
        seeds: list[int],
        build_profiles: list[str],
        player_level: str,
        validation_variant: str,
        valid_runs_per_build: int,
        dynamic_balance: bool,
        resume_output_directory: str | None = None,
    ) -> dict[str, Any]:
        self.validate_request(
            seeds,
            build_profiles,
            player_level,
            validation_variant,
            valid_runs_per_build,
            dynamic_balance,
        )
        with self._lock:
            if self._process is not None and self._process.poll() is None:
                return {
                    "ok": False,
                    "message": "A build comparison is already running.",
                    **self.status(),
                }

            request = {
                "seeds": list(seeds),
                "build_profiles": list(build_profiles),
                "player_level": player_level,
                "validation_variant": validation_variant,
                "valid_runs_per_build": valid_runs_per_build,
                "dynamic_balance": dynamic_balance,
            }
            if resume_output_directory:
                candidate = Path(resume_output_directory)
                if not candidate.is_absolute():
                    candidate = self.output_root / candidate
                candidate = candidate.resolve()
                output_root = self.output_root.resolve()
                if not candidate.is_relative_to(output_root):
                    raise ValueError(
                        "resume_output_directory must be inside "
                        f"{output_root}"
                    )
                if not candidate.is_dir():
                    raise ValueError(
                        f"resume_output_directory does not exist: {candidate}"
                    )
                saved = _load_json(candidate / "comparison_request.json")
                expected_saved = {
                    "seeds": request["seeds"],
                    "build_profiles": request["build_profiles"],
                    "player_level_requested": request["player_level"],
                    "validation_variant": request["validation_variant"],
                    "valid_runs_per_build": request["valid_runs_per_build"],
                    "dynamic_balance": request["dynamic_balance"],
                }
                mismatches = {
                    key: {"expected": value, "saved": saved.get(key)}
                    for key, value in expected_saved.items()
                    if saved.get(key) != value
                }
                if mismatches:
                    raise ValueError(
                        "Resume conditions do not match comparison_request.json: "
                        + json.dumps(mismatches, ensure_ascii=False, sort_keys=True)
                    )
                self._output_directory = candidate
            else:
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                self._output_directory = (
                    self.output_root / f"comparison_{stamp}_{uuid4().hex[:8]}"
                ).resolve()
                self._output_directory.mkdir(parents=True, exist_ok=False)
            self._request = request
            self._request["resume_output_directory"] = (
                str(self._output_directory) if resume_output_directory else None
            )
            self._started_at = _utc_now()

            command = [
                str(self.python_executable),
                str(self.script_path),
                "--comparison-suite",
                "--url",
                self.mcp_url,
                "--profile",
                player_level,
                "--validation-variant",
                validation_variant,
                "--valid-runs-per-build",
                str(valid_runs_per_build),
                "--output-directory",
                str(self._output_directory),
                "--balance-log-directory",
                str(self.project_root / "logs" / "balance"),
            ]
            for seed in seeds:
                command.extend(("--comparison-seed", str(seed)))
            for build in build_profiles:
                command.extend(("--comparison-build", build))

            stdout_path = self._output_directory / "collector_stdout.jsonl"
            stderr_path = self._output_directory / "collector_stderr.log"
            creation_flags = (
                getattr(subprocess, "CREATE_NO_WINDOW", 0)
                if os.name == "nt"
                else 0
            )
            log_mode = "a" if resume_output_directory else "w"
            try:
                with stdout_path.open(log_mode, encoding="utf-8") as stdout_file, (
                    stderr_path.open(log_mode, encoding="utf-8")
                ) as stderr_file:
                    self._process = subprocess.Popen(
                        command,
                        cwd=self.project_root,
                        stdin=subprocess.DEVNULL,
                        stdout=stdout_file,
                        stderr=stderr_file,
                        creationflags=creation_flags,
                        close_fds=True,
                    )
            except Exception:
                self._process = None
                raise

            return {
                "ok": True,
                "message": "Build comparison started in the background.",
                **self.status(),
            }

    def _stderr_tail(self) -> str:
        path = self._paths().get("collector_stderr")
        if not path:
            return ""
        try:
            text = Path(path).read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""
        return text[-8000:]

    def status(self) -> dict[str, Any]:
        with self._lock:
            if self._output_directory is None:
                return {
                    "state": "failed",
                    "running": False,
                    "completed": False,
                    "failed": True,
                    "current_build": "",
                    "current_seed": None,
                    "retrying": False,
                    "retry_count": 0,
                    "retries": 0,
                    "last_error": {},
                    "exception_type": "",
                    "exception_message": "",
                    "error_timestamp": "",
                    "failed_build": "",
                    "failed_seed": None,
                    "scene": None,
                    "battle_state": None,
                    "area_progress": None,
                    "cleared_stage_count": None,
                    "completed_valid_runs": 0,
                    "excluded_runs": 0,
                    "error": "No build comparison has been started.",
                    "output_paths": {},
                }

            status = _load_json(
                self._output_directory / "comparison_status.json"
            )
            return_code = self._process.poll() if self._process is not None else None
            state = str(status.get("state", "running"))
            error = str(status.get("error", ""))
            if return_code is not None:
                if return_code != 0:
                    state = "failed"
                    error = error or self._stderr_tail() or (
                        f"Collector exited with code {return_code}."
                    )
                elif state == "running":
                    state = "completed"

            records = _load_jsonl(self._output_directory / "all_runs.jsonl")
            exclusions = _load_jsonl(
                self._output_directory / "errors_and_exclusions.jsonl"
            )
            current_build = str(status.get("current_build", ""))
            current_seed = status.get("current_seed")
            if not status and self._request:
                current_build = str(self._request["build_profiles"][0])
                current_seed = int(self._request["seeds"][0])
            return {
                "state": state,
                "running": state == "running",
                "completed": state == "completed",
                "failed": state == "failed",
                "current_build": current_build,
                "current_seed": current_seed,
                "retrying": bool(status.get("retrying", False)),
                "retry_count": int(status.get("retry_count", 0)),
                "retries": int(status.get("retries", 0)),
                "last_error": status.get("last_error", {}),
                "exception_type": str(status.get("exception_type", "")),
                "exception_message": str(status.get("exception_message", "")),
                "error_timestamp": str(status.get("error_timestamp", "")),
                "failed_build": str(status.get("failed_build", "")),
                "failed_seed": status.get("failed_seed"),
                "scene": status.get("scene"),
                "battle_state": status.get("battle_state"),
                "area_progress": status.get("area_progress"),
                "cleared_stage_count": status.get("cleared_stage_count"),
                "completed_valid_runs": int(
                    status.get("completed_valid_runs", len(records))
                ),
                "completed_valid_runs_by_build": status.get(
                    "completed_valid_runs_by_build", {}
                ),
                "excluded_runs": int(status.get("excluded_runs", len(exclusions))),
                "error": error,
                "started_at": self._started_at,
                "updated_at": status.get("updated_at", ""),
                "request": dict(self._request),
                "output_paths": self._paths(),
            }

    def result(self) -> dict[str, Any]:
        with self._lock:
            status = self.status()
            if not status["completed"]:
                return {
                    "ok": False,
                    "message": "Build comparison has not completed.",
                    "status": status,
                }
            assert self._output_directory is not None
            summary = _load_json(
                self._output_directory / "build_summaries.json"
            )
            comparison_rows: list[dict[str, str]] = []
            comparison_path = self._output_directory / "seed_build_comparison.csv"
            try:
                with comparison_path.open(
                    "r", encoding="utf-8-sig", newline=""
                ) as source:
                    comparison_rows = list(csv.DictReader(source))
            except OSError:
                pass
            exclusions = _load_jsonl(
                self._output_directory / "errors_and_exclusions.jsonl"
            )
            build_summaries = summary.get("builds_summary", {})
            paired_summary = summary.get("paired_seed_summary", {})
            major_metrics = {
                build: {
                    key: values.get(key)
                    for key in (
                        "sample_count",
                        "win_rate",
                        "final_boss_reach_rate",
                        "boss_defeat_rate_after_reaching",
                        "final_hp_average",
                        "final_hp_median",
                        "shots_average",
                        "shots_median",
                        "build_completion_rate",
                        "target_ball_shot_share_average",
                        "target_build_ball_acquisition_count",
                        "target_build_ball_acquisition_rate",
                        "target_build_ball_acquisition_rate_per_run",
                        "target_build_ball_acquisitions_by_definition",
                        "missing_key_parts_frequency",
                        "average_final_deck",
                        "final_deck_by_seed",
                        "reward_selection_history",
                        "reward_dependency_rate",
                    )
                }
                for build, values in build_summaries.items()
                if isinstance(values, dict)
            }
            return {
                "ok": True,
                "status": status,
                "build_summaries": build_summaries,
                "seed_build_comparison": comparison_rows,
                "errors_and_exclusions": exclusions,
                "saved_paths": self._paths(),
                "major_metrics": {
                    "by_build": major_metrics,
                    "paired_seeds": paired_summary,
                },
            }
