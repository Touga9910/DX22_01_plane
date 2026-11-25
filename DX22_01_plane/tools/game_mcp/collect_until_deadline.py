"""Run the existing MCP collector one run at a time, then analyze offline."""
from __future__ import annotations

import argparse
import asyncio
import csv
import ctypes
import json
import os
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime
from pathlib import Path

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client
from collect_fixed_balance_runs import tool_result

BUILDS = ("standard", "heavy", "pierce", "bounce", "anchor")
PROJECT = Path(__file__).resolve().parents[2]


def save(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    for attempt in range(100):
        try:
            temporary.replace(path)
            break
        except PermissionError:
            if attempt == 99:
                raise
            time.sleep(.1)


def read_lines(path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def write_csv(path, rows):
    if not rows:
        path.write_text("", encoding="utf-8-sig")
        return
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def analyze(output, batches, metadata):
    """Only normally completed, integrity-validated runs enter the denominators."""
    records = []
    exclusions = []
    for batch in batches:
        records.extend(read_lines(Path(batch) / "all_runs.jsonl"))
        exclusions.extend(read_lines(Path(batch) / "errors_and_exclusions.jsonl"))
    stages = []
    runs = []
    for record in records:
        raw = json.loads(Path(record["source_log"]).read_text(encoding="utf-8-sig"))
        build = record["build_profile"]
        run_stages = []
        for stage in raw.get("stages", []):
            result = stage.get("stage_result", {})
            outcome = result.get("result", "unfinished")
            # An unfinished stage is reported, but excluded from win-rate denominators.
            damage = result.get("player_damage_taken")
            if damage is None and "damage_events" in stage:
                damage = sum(event.get("damage", 0) for event in stage["damage_events"])
            row = {
                "run_id": record["run_id"], "run_seed": record["run_seed"],
                "build": build, "stage_index": stage.get("stage_index"),
                "stage_id": stage.get("stage_id", ""),
                "stage_type": stage.get("stage_type", ""), "result": outcome,
                "finished": outcome in {"clear", "game_over"}, "won": outcome == "clear",
                "damage_taken": damage, "shots": result.get("total_shots"),
                "ending_hp": result.get("player_hp"),
            }
            stages.append(row)
            run_stages.append(row)
        runs.append({
            "run_id": record["run_id"], "run_seed": record["run_seed"], "build": build,
            "won": record["final_boss_defeated"], "result": record["run_result"],
            "stages_reached": len(run_stages),
            "stages_cleared": sum(row["won"] for row in run_stages),
            "damage_taken": sum(row["damage_taken"] for row in run_stages if row["damage_taken"] is not None),
            "shots": record["total_shots"], "ending_hp": record["final_hp"],
            "source_log": record["source_log"],
        })
    build_rows = []
    for build in BUILDS:
        samples = [row for row in runs if row["build"] == build]
        wins = sum(row["won"] for row in samples)
        count = len(samples)
        build_rows.append({
            "build": build, "runs": count, "wins": wins, "losses": count-wins,
            "win_rate": wins/count if count else None,
            "mean_damage_taken": sum(row["damage_taken"] for row in samples)/count if count else None,
            "mean_stages_cleared": sum(row["stages_cleared"] for row in samples)/count if count else None,
        })
    def group_stages(keys):
        groups = defaultdict(list)
        for stage in stages:
            groups[tuple(stage[key] for key in keys)].append(stage)
        rows = []
        for group, samples in sorted(groups.items(), key=lambda item: str(item[0])):
            finished = [s for s in samples if s["finished"]]
            damage = [s["damage_taken"] for s in finished if s["damage_taken"] is not None]
            wins = sum(s["won"] for s in finished)
            rows.append({**dict(zip(keys, group)), "reached": len(samples),
                         "finished": len(finished), "wins": wins,
                         "losses": len(finished)-wins,
                         "win_rate": wins/len(finished) if finished else None,
                         "damage_samples": len(damage),
                         "mean_damage_taken": sum(damage)/len(damage) if damage else None})
        return rows
    by_order = group_stages(("stage_index",))
    by_build_order = group_stages(("build", "stage_index"))
    by_id = group_stages(("stage_id", "stage_type"))
    by_build_id = group_stages(("build", "stage_id", "stage_type"))
    for name, rows in (("runs.csv", runs), ("build_win_rates.csv", build_rows),
                       ("run_stages.csv", stages), ("stage_win_rates.csv", by_order),
                       ("build_stage_win_rates.csv", by_build_order),
                       ("stage_id_win_rates.csv", by_id), ("build_stage_id_win_rates.csv", by_build_id)):
        write_csv(output/name, rows)
    seed_builds = defaultdict(set)
    for row in runs:
        seed_builds[row["run_seed"]].add(row["build"])
    complete_seeds = [seed for seed, builds in seed_builds.items() if set(BUILDS) <= builds]
    result = {**metadata, "analysis_finished_at": datetime.now().astimezone().isoformat(),
              "valid_runs": len(runs), "paired_seeds_all_five_builds": complete_seeds,
              "builds": build_rows, "stages": by_order, "build_stages": by_build_order,
              "exclusions_and_retries": exclusions,
              "definitions": {"run_win": "final_boss_defeated",
                              "stage_win_rate": "clear / (clear + game_over), conditional on reaching stage",
                              "damage_taken": "stage_result.player_damage_taken, fallback sum(damage_events.damage)",
                              "axis": "requested build policy; not a claim that build was completed"}}
    save(output/"analysis.json", result)
    lines = [datetime.now().astimezone().strftime('作成日：%Y年%m月%d日'), "ラン自動収集結果", "",
             f"停止予定：{metadata['deadline']}", f"有効ラン数：{len(runs)}",
             f"5軸すべて比較できるseed数：{len(complete_seeds)}", "",
             "軸別勝率（最終ボス撃破数／正常終了ラン数）"]
    for row in build_rows:
        rate = f"{row['win_rate']:.1%}" if row["runs"] else "未取得"
        lines.append(f"{row['build']}: {row['wins']}/{row['runs']} = {rate}")
    lines += ["", "ステージ別勝率（到達した正常終了戦闘が分母）"]
    for row in sorted(by_order, key=lambda row: row["stage_index"] or 0):
        rate = f"{row['win_rate']:.1%}" if row["win_rate"] is not None else "未取得"
        damage = f"{row['mean_damage_taken']:.2f}" if row["mean_damage_taken"] is not None else "未取得"
        lines.append(f"第{row['stage_index']}戦: {row['wins']}/{row['finished']} = {rate}, 平均被ダメージ {damage}")
    lines += ["", "注意：軸は指定したビルド方針。ビルド完成を意味しません。",
              "未到達ステージは敗北に含めません。途中終了や整合性検証失敗は有効ランから除外します。",
              (f"各軸{metadata['target_runs_per_build']}有効ランの固定目標で終了。" if metadata.get('target_runs_per_build') else "時刻による終了のため軸ごとのサンプル数には最大1seed周分の差が生じます。"),
              "詳細はCSVとanalysis.jsonを参照してください。"]
    (output/(datetime.now().astimezone().strftime('%m%d_')+'ラン自動収集分析.txt')).write_text("\n".join(lines)+"\n", encoding="utf-8-sig")
    return result


async def run(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    deadline = datetime.fromisoformat(args.deadline) if args.deadline else None
    if deadline is not None and (deadline.tzinfo is None or deadline <= datetime.now().astimezone()):
        raise ValueError("deadline must be a future timezone-aware timestamp")
    target = getattr(args, "target_runs_per_build", None)
    if target is not None and target <= 0:
        raise ValueError("target_runs_per_build must be positive")
    def counts():
        return {build: sum(pair[1] == build for pair in completed_pairs) for build in BUILDS}
    def expired():
        return ((output/"stop_after_current_run").exists()
                or (deadline is not None and datetime.now().astimezone() >= deadline)
                or (target is not None and all(value >= target for value in counts().values())))
    metadata = {"deadline": deadline.isoformat() if deadline else "until_user_stops", "started_at": datetime.now().astimezone().isoformat(),
                "builds_requested": BUILDS, "player_level": "intermediate", "validation_variant": "fixed",
                "dynamic_balance": False, "endpoint": args.url, "batches": [], "runner_pid": os.getpid()}
    completed_pairs = set()
    metadata["target_runs_per_build"] = target
    if target is not None and deadline is None:
        metadata["deadline"] = f"target_{target}_valid_runs_per_build"
    metadata["required_balance_version"] = getattr(args, "balance_version", None)
    baseline = getattr(args, "compare_with", None)
    if baseline:
        metadata["comparison_baseline"] = str(baseline.resolve())
    configuration_signature = None
    pending_pairs = {}
    resume_from = getattr(args, "resume_from", None)
    if resume_from:
        previous = json.loads(resume_from.read_text(encoding="utf-8"))
        metadata["batches"] = list(previous["batches"])
        metadata["resumed_from"] = str(resume_from.resolve())
        for batch in metadata["batches"]:
            records = read_lines(Path(batch)/"all_runs.jsonl")
            for record in records:
                signature = record["configuration_signature"]
                if configuration_signature is not None and signature != configuration_signature:
                    raise RuntimeError("Configuration changed between resumed collection runs")
                configuration_signature = signature
            completed_pairs.update((int(r["run_seed"]), r["build_profile"]) for r in records)
            if not records:
                request = json.loads((Path(batch)/"comparison_request.json").read_text(encoding="utf-8"))
                if len(request["seeds"]) == len(request["build_profiles"]) == 1:
                    pending_pairs[(request["seeds"][0], request["build_profiles"][0])] = batch
    children = []
    streams = []
    game_process = None
    def status(state, **details):
        save(output/"overnight_status.json", {**metadata, "state": state,
             "completed_runs_by_build": counts(),
             "updated_at": datetime.now().astimezone().isoformat(), **details})
    def launch(command, name, cwd):
        stdout = (output/(name+"_stdout.log")).open("w", encoding="utf-8")
        stderr = (output/(name+"_stderr.log")).open("w", encoding="utf-8")
        streams.extend((stdout, stderr))
        process = subprocess.Popen(command, cwd=cwd, stdout=stdout, stderr=stderr,
                                   stdin=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        children.append(process)
        metadata[name+"_pid"] = process.pid
        return process
    if os.name == "nt":
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000001)
    error = None
    try:
        if args.game:
            game_process = launch([str(args.game.resolve())], "game", PROJECT)
        if args.launch_server:
            restored_build = next((pair[1] for pair in pending_pairs), "standard")
            launch([sys.executable, str(PROJECT/"tools/game_mcp/server.py"), "--port", "8766",
                    "--build-profile", restored_build], "server", PROJECT)
        status("starting")
        await asyncio.sleep(5)
        async with streamable_http_client(args.url) as transport:
            async with ClientSession(transport[0], transport[1]) as session:
                await session.initialize()
                async def call(name, arguments=None):
                    response = await session.call_tool(name, arguments or {})
                    if response.isError:
                        raise RuntimeError(f"{name}: {response.content}")
                    return tool_result(response)
                state = await call("get_game_state")
                if state.get("scene") not in {"title", "result"} and not pending_pairs:
                    raise RuntimeError("Refusing to replace an existing in-progress run")
                seed = args.first_seed
                while not expired():
                    for build in BUILDS:
                        if expired():
                            break
                        if (seed, build) in completed_pairs:
                            continue
                        if target is not None and counts()[build] >= target:
                            continue
                        # Query state before each operation; one collector job contains exactly one run.
                        await call("get_game_state")
                        request = {
                            "seeds": [seed], "build_profiles": [build], "player_level": "intermediate",
                            "validation_variant": "fixed", "valid_runs_per_build": 1, "dynamic_balance": False}
                        if (seed, build) in pending_pairs:
                            request["resume_output_directory"] = pending_pairs[(seed, build)]
                        started = await call("run_build_comparison", request)
                        if not started.get("ok"):
                            raise RuntimeError(f"Could not start collector: {started}")
                        batch = started["output_paths"]["output_directory"]
                        if batch not in metadata["batches"]:
                            metadata["batches"].append(batch)
                        status("collecting", current_build=build, current_seed=seed)
                        while True:
                            await asyncio.sleep(10)
                            if game_process is not None and game_process.poll() is not None:
                                raise RuntimeError(f"Game process exited during collection: {game_process.returncode}")
                            progress = await call("get_build_comparison_status")
                            status("finishing_last_run" if expired() else "collecting",
                                   current_build=build, current_seed=seed, collector=progress)
                            if not progress.get("running"):
                                if not progress.get("completed"):
                                    raise RuntimeError(f"Collector failed: {progress}")
                                if target is not None or metadata["required_balance_version"] or configuration_signature is not None:
                                    accepted = read_lines(Path(batch)/"all_runs.jsonl")
                                    if len(accepted) != 1 or (accepted[0]["run_seed"], accepted[0]["build_profile"]) != (seed, build):
                                        raise RuntimeError("Collector did not save exactly the requested valid run")
                                    record = accepted[0]
                                    raw = json.loads(Path(record["source_log"]).read_text(encoding="utf-8-sig"))
                                    required = metadata["required_balance_version"]
                                    if required and raw.get("balance_version") != required:
                                        raise RuntimeError(f"Unexpected balance version: {raw.get('balance_version')}")
                                    signature = record["configuration_signature"]
                                    if configuration_signature is not None and signature != configuration_signature:
                                        raise RuntimeError("Configuration changed between collection runs")
                                    configuration_signature = signature
                                completed_pairs.add((seed, build))
                                break
                    seed += 1
        status("analyzing")
        result = analyze(output, metadata["batches"], metadata)
        comparison = None
        if baseline:
            from compare_balance_campaigns import compare_campaigns
            comparison = compare_campaigns(baseline, output, target or 10)
        status("completed", valid_runs=result["valid_runs"], analysis=str(output/"analysis.json"),
               comparison_report=comparison)
    except BaseException as exc:
        error = repr(exc)
        status("failed", error=error)
        # Preserve raw results on failure; analysis must not start before the deadline.
        metadata["collection_error"] = error
        if expired():
            analyze(output, metadata["batches"], metadata)
        raise
    finally:
        if os.name == "nt":
            ctypes.windll.kernel32.SetThreadExecutionState(0x80000000)
        for process in children:
            # Leave the game open; the private server has no work after the collector exits.
            if process.pid == metadata.get("server_pid") and process.poll() is None:
                process.terminate()
        for stream in streams:
            stream.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deadline", help="Omit to collect until explicitly stopped")
    parser.add_argument("--resume-from", type=Path, help="Previous overnight_status.json")
    parser.add_argument("--target-runs-per-build", type=int, default=10,
                        help="Accepted runs per build; default 10 (50 total across five builds)")
    parser.add_argument("--balance-version", help="Reject runs from another balance version")
    parser.add_argument("--compare-with", type=Path, help="Compare with this completed campaign after collection")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--url", default="http://127.0.0.1:8766/mcp")
    parser.add_argument("--first-seed", type=int, default=2026111700)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--launch-server", action="store_true")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
