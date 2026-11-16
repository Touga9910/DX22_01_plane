"""Capture one isolated scenario, then classify its exit from durable evidence."""
from __future__ import annotations

import gzip
import os
from pathlib import Path
import time
from typing import Any

from .fixtures import CASES, prepare_fixture
from .io import digest, read_json, require, write_json
from .lifecycle import classify_result, read_native
from .normalization import normalize_run
from .ordinary import prepare_ordinary_fixture, run_ordinary, validate_ordinary_logs
from .process_session import Session
from .scenarios import ScenarioDriver, ScenarioOptions, validate_run_logs


def capture_trial(exe: Path, folder: Path, *, case: str, condition: str, seed: int,
                  decision_frames: int, expected_runs: int = 1, timeout_seconds: float = 180,
                  reference: Path | None = None, scenario: str = "controlled",
                  publish_frames: int = 1, render_hz: int | None = None, observe_resources: bool = False) -> dict[str, Any]:
    require(case in CASES, "Known autoplay case")
    require(scenario in ("controlled", "ordinary"), "Known scenario profile")
    require(scenario == "controlled" or reference is None, "Ordinary data is not compared with a two-battle reference")
    prepare = prepare_fixture if scenario == "controlled" else prepare_ordinary_fixture
    preparation = prepare(folder, CASES[case], seed, decision_frames, expected_runs, publish_frames=publish_frames)
    session = Session(folder, condition, observe_resources=observe_resources)
    child_environment = dict(os.environ)
    child_environment.pop("DX22_TEST_RENDER_HZ", None)
    if render_hz is not None:
        child_environment["DX22_TEST_RENDER_HZ"] = str(render_hz)
    manifest: dict[str, Any] = {
        "case": case, "ok": False, "working_directory": str(folder.resolve()),
        "exe": str(exe.resolve()), "exe_sha256": digest(exe), "preparation": preparation,
        "commands": [], "milestones": [],
        "observation_settings": {"state_publish_interval_frames": publish_frames, "observe_resources": observe_resources,
                                 "DX22_TEST_RENDER_HZ_requested": child_environment.get("DX22_TEST_RENDER_HZ")},
        "fixture_file_sha256": {
            path.relative_to(folder).as_posix(): digest(path)
            for path in sorted(folder.rglob("*")) if path.is_file()
        },
    }
    driver = None
    started = time.monotonic()
    with (folder / "process.log").open("wb") as output, gzip.open(
            folder / "states.jsonl.gz", "wt", encoding="utf-8", compresslevel=1) as trace:
        try:
            child = session.launch([str(exe.resolve())], output, env=child_environment)
            manifest["process_id"] = child.pid
            options = ScenarioOptions(case, seed, expected_runs, timeout_seconds)
            driver = ScenarioDriver(folder, child, session.observe, trace, options)
            manifest["terminal"] = driver.run() if scenario == "controlled" else run_ordinary(driver)
            manifest["ok"] = True
        except Exception as error:
            manifest["error"] = f"{type(error).__name__}: {error}"
        finally:
            # Cleanup always precedes additional artifact writes, including when
            # an observation or scenario assertion failed.
            manifest["cleanup"] = session.cleanup()
            manifest["resources"] = session.resources_summary()
    manifest["elapsed_seconds"] = round(time.monotonic() - started, 3)
    if driver is not None:
        manifest["commands"], manifest["milestones"] = driver.commands, driver.milestones
        if driver.latest_state:
            write_json(folder / "last_observed_state.json", driver.latest_state)
    runs = []
    try:
        for path in sorted((folder / "logs/balance").glob("run_*.json")):
            run = read_json(path)
            runs.append(run)
            manifest.setdefault("run_logs", []).append({
                "path": path.relative_to(folder).as_posix(), "sha256": digest(path),
                "controller_type": run.get("controller_type"), "run_result": run.get("run_result"),
                "stage_ids": [stage.get("stage_id") for stage in run.get("stages", [])],
                "shot_count": sum(len(stage.get("shots", [])) for stage in run.get("stages", [])),
            })
        validate = validate_run_logs if scenario == "controlled" else validate_ordinary_logs
        validate(runs, seed, expected_runs)
        manifest["coverage_checks_passed"] = True
    except (AssertionError, KeyError, IndexError, OSError, ValueError) as error:
        manifest["ok"] = False
        manifest["coverage_error"] = f"{type(error).__name__}: {error}"
    comparable = {
        "case": case, "ok": manifest["ok"], "terminal": manifest.get("terminal"),
        "runs": [normalize_run(run) for run in runs],
        "commands": [{"action": command["action"], "arguments": command["arguments"],
                      "ok": command["result"].get("ok"), "message": command["result"].get("message")}
                     for command in manifest["commands"]],
    }
    reference_equal = read_json(reference) == comparable if reference is not None else None
    native, native_error = [], None
    try:
        native = read_native(folder, final=True)
    except (OSError, ValueError) as error:
        native_error = f"{type(error).__name__}: {error}"
    classification = classify_result({
        "process_id": manifest.get("process_id"), "manifest": manifest,
        "native": native, "events": session.events, "condition": condition,
        "visible_window_samples": session.visible_samples,
        "exit_code": manifest["cleanup"].get("exit_code"),
        "cleanup_nonce": {"wparam": session.wparam, "lparam": session.lparam},
        "native_parse_error": native_error,
        "trace_errors": session.trace_errors + manifest["cleanup"].get("errors", []),
        "reference_comparable_equal": reference_equal,
    })
    result = {
        **classification, "resources": manifest["resources"], "case": case, "condition": condition, "seed": seed, "scenario": scenario,
        "decision_frames": decision_frames, "expected_runs": expected_runs,
        "case_timeout": timeout_seconds, "exe_sha256": manifest["exe_sha256"],
        "publish_frames": publish_frames, "render_hz_requested": manifest["observation_settings"]["DX22_TEST_RENDER_HZ_requested"],
        "process_id": manifest.get("process_id"), "elapsed_seconds": manifest["elapsed_seconds"],
        "cleanup_nonce": {"wparam": session.wparam, "lparam": session.lparam},
        "native_quit": classification["evidence"]["native_quit"],
        "reference_comparable_equal": reference_equal,
        "error": manifest.get("error"), "coverage_error": manifest.get("coverage_error"),
        "manifest": str((folder / "manifest.json").resolve()),
        "request_events": str((folder / "request_events.jsonl").resolve()),
        "lifecycle": str((folder / "logs/application_lifecycle.jsonl").resolve()),
    }
    write_json(folder / "manifest.json", manifest)
    write_json(folder / "comparable.json", comparable)
    write_json(folder / "window_observations.json", session.windows)
    write_json(folder / "result.json", result)
    return result
