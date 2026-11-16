"""Run bounded sequential trials, preserving identity and stopping on evidence."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
import re
import shutil
import time

from .capture import capture_trial
from .fixtures import CASES, REPO_ROOT
from .io import digest, read_json, require, write_json


def _source_identity() -> dict[str, str]:
    package = Path(__file__).parent
    tools = package.parent
    paths = list(package.glob("*.py")) + [tools / "verify_autoplay.py", tools / "game_mcp/bridge_store.py"]
    return {path.relative_to(tools).as_posix(): digest(path) for path in sorted(paths)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--output-root", type=Path, default=REPO_ROOT / "logs/autoplay_validation")
    parser.add_argument("--trials", type=int, default=2)
    parser.add_argument("--scenario", choices=["controlled", "ordinary"], default="controlled",
                        help="Two queued battles, or unchanged ordinary gameplay data with observation only")
    parser.add_argument("--cases", nargs="+", choices=list(CASES), default=list(CASES))
    parser.add_argument("--conditions", nargs="+", choices=["hidden", "legacy_rehide"], default=["hidden"])
    parser.add_argument("--seeds", nargs="+", type=int, default=[20261116])
    parser.add_argument("--decision-frames", type=int, default=150)
    parser.add_argument("--publish-frames", type=int, default=1)
    parser.add_argument("--render-hz", type=int, choices=[30, 60, 144],
                        help="Request the existing Debug render-rate override; actual FPS is workload-dependent")
    parser.add_argument("--observe-resources", action="store_true", help="Query the owned child handle every 15 seconds; metrics are separate from gameplay")
    parser.add_argument("--native-runs", type=int, default=1)
    parser.add_argument("--case-timeout", type=float)
    parser.add_argument("--reference-root", type=Path, help="Optional pre-refactor fixture directory containing case manifests/comparables")
    parser.add_argument("--stop-before-utc", help="Optional ISO timestamp; reserve enough time for the whole next trial")
    args = parser.parse_args()
    require(re.fullmatch(r"[A-Za-z0-9_-]+", args.label), "Use a safe fresh label")
    require(1 <= args.trials <= 250 and 1 <= args.native_runs <= 50, "Bounded trial/native-run counts")
    if args.scenario == "controlled":
        require(120 <= args.decision_frames <= 180, "Queued-layout scenarios need a 120-180 frame observation window")
    else:
        require(20 <= args.decision_frames <= 600, "Ordinary observation accepts a 20-600 frame decision interval")
    require(1 <= args.publish_frames <= min(60, args.decision_frames // 2),
            "State publication leaves at least two samples per decision interval")
    require(all(0 <= seed <= 0xffffffff for seed in args.seeds), "Seeds are uint32")
    require(args.native_runs == 1 or args.cases == ["maximum_runs"], "Multiple native runs use maximum_runs only")
    per_run_timeout = 180.0 if args.scenario == "controlled" else 1200.0
    timeout = args.case_timeout if args.case_timeout is not None else per_run_timeout * args.native_runs
    require(0 < timeout <= 10800, "Trial timeout is positive and at most three hours")
    args.exe, args.output_root = args.exe.resolve(), args.output_root.resolve()
    require(args.exe.is_file(), "Explicit executable exists")
    deadline = None
    if args.stop_before_utc:
        parsed = datetime.fromisoformat(args.stop_before_utc)
        require(parsed.tzinfo is not None, "Stop timestamp includes its UTC offset")
        deadline = parsed.timestamp()
    references: dict[str, Path] = {}
    if args.reference_root is not None:
        require(args.scenario == "controlled", "Historical two-battle references apply only to the controlled scenario")
        require(args.publish_frames == 1 and args.render_hz in (None, 60),
                "Historical reference uses every-frame publication and the default render rate")
        require(args.native_runs == 1, "Single-run reference is not compared with multi-run sessions")
        for case in args.cases:
            original = read_json(args.reference_root / case / "manifest.json")["preparation"]
            require(all(seed == original["run_seed"] for seed in args.seeds)
                    and args.decision_frames == original["decision_delay_frames"],
                    "Reference seed and decision interval match every requested trial")
            reference = args.reference_root / case / "comparable.json"
            require(reference.is_file(), "Reference comparable exists")
            references[case] = reference
    root = args.output_root / "campaigns" / args.label
    require(not root.exists(), "Use a fresh campaign label")
    root.mkdir(parents=True)
    identity, exe_hash = _source_identity(), digest(args.exe)
    tools = Path(__file__).parent.parent
    for relative, expected_hash in identity.items():
        source = tools / relative
        archived = root / "source" / relative
        archived.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, archived)
        require(digest(archived) == expected_hash, "Archived validation source matches its identity")
    plan = [{"condition": condition, "case": case, "seed": seed}
            for seed in args.seeds for condition in args.conditions for case in args.cases]
    summary = {"label": args.label, "status": "running", "started_at_utc": datetime.now(timezone.utc).isoformat(),
               "exe": str(args.exe), "exe_sha256": exe_hash, "source_sha256": identity,
               "plan": plan, "requested_trials": args.trials, "expected_native_runs_per_trial": args.native_runs,
               "scenario": args.scenario,
               "publish_frames": args.publish_frames, "render_hz_requested": args.render_hz, "observe_resources": args.observe_resources,
               "decision_frames": args.decision_frames, "case_timeout": timeout,
               "stop_before_utc": args.stop_before_utc, "completed_trials": []}
    write_json(root / "summary.json", summary)
    try:
        for index in range(1, args.trials + 1):
            if deadline is not None and time.time() + timeout + 20 >= deadline:
                summary["status"] = "deadline_reached"
                break
            require(shutil.disk_usage(args.output_root).free > 3 * 1024**3, "At least 3GiB free space remains")
            require(digest(args.exe) == exe_hash and _source_identity() == identity, "Executable and validation code stay fixed during campaign")
            specification = plan[(index - 1) % len(plan)]
            case, condition = specification["case"], specification["condition"]
            folder = args.output_root / "trials" / args.label / f"trial_{index:04d}_{condition}_{case}" / case
            require(folder.resolve().is_relative_to(args.output_root), "Trial stays within its output root")
            result = capture_trial(args.exe, folder, **specification, decision_frames=args.decision_frames,
                                   expected_runs=args.native_runs, timeout_seconds=timeout,
                                   reference=references.get(case), scenario=args.scenario,
                                   publish_frames=args.publish_frames, render_hz=args.render_hz, observe_resources=args.observe_resources)
            result.update(trial=index, source_sha256=identity)
            write_json(folder / "result.json", result)
            summary["completed_trials"].append(result)
            write_json(root / "summary.json", summary)
            print({key: result[key] for key in ("trial", "case", "condition", "outcome", "elapsed_seconds")}, flush=True)
            if result["outcome"] != "ordinary_success" or result["evidence"]["close_events"]:
                summary["status"] = "stopped_for_investigation"
                break
        else:
            summary["status"] = "completed"
    except Exception as error:
        summary["status"] = "harness_exception"
        summary["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        summary["ended_at_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(root / "summary.json", summary)
    return 0 if summary["status"] in ("completed", "deadline_reached") else 1
