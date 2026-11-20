"""Request the existing collector's graceful stop during its final target run."""
import argparse
import json
import time
from datetime import datetime
from pathlib import Path

from collect_until_deadline import BUILDS, read_lines, save


def should_stop(counts, current_build, target):
    remaining = {build: max(0, target-counts.get(build, 0)) for build in BUILDS}
    return sum(remaining.values()) == 0 or (
        sum(remaining.values()) == 1 and remaining.get(current_build) == 1
    )


def finalize_target_metadata(directory, target):
    """Keep derived status truthful when the running CLI still has its old quota."""
    for filename in ("overnight_status.json", "analysis.json"):
        path = directory/filename
        if not path.exists():
            continue
        value = json.loads(path.read_text(encoding="utf-8-sig"))
        previous = value.get("target_runs_per_build")
        if previous != target:
            value.setdefault("original_target_runs_per_build", previous)
        value["target_runs_per_build"] = target
        old_deadline = value.get("deadline", "")
        value["deadline"] = f"target_{target}_valid_runs_per_build"
        save(path, value)
        if filename == "analysis.json" and old_deadline:
            for report in directory.glob("*_ラン自動収集分析.txt"):
                text = report.read_text(encoding="utf-8-sig")
                report.write_text(text.replace(old_deadline, value["deadline"]), encoding="utf-8-sig")


def watch(directory, target):
    save(directory/"collection_target.json", {
        "valid_runs_per_build": target, "builds": BUILDS,
        "configured_at": datetime.now().astimezone().isoformat(),
        "stop_policy": "finish final target run, then analyze",
    })
    while True:
        status = json.loads((directory/"overnight_status.json").read_text(encoding="utf-8"))
        counts = dict.fromkeys(BUILDS, 0)
        for batch in status["batches"]:
            for record in read_lines(Path(batch)/"all_runs.jsonl"):
                counts[record["build_profile"]] += 1
        state = status["state"]
        requested = state in {"collecting", "finishing_last_run"} and should_stop(counts, status.get("current_build"), target)
        if requested:
            # The runner checks this between runs, completes the current one,
            # and generates its analysis without starting another run.
            (directory/"stop_after_current_run").touch()
        save(directory/"target_monitor_status.json", {
            "updated_at": datetime.now().astimezone().isoformat(),
            "target_per_build": target, "completed_runs_by_build": counts,
            "stop_requested": (directory/"stop_after_current_run").exists(),
            "collector_state": state,
        })
        if state in {"completed", "failed"}:
            finalize_target_metadata(directory, target)
            return
        time.sleep(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--target", type=int, required=True)
    args = parser.parse_args()
    if args.target <= 0:
        parser.error("target must be positive")
    watch(args.directory.resolve(), args.target)
