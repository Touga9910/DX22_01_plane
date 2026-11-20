"""Isolated current-data shot/performance captures; never use the user's saves.

UI mode keeps one owned game open for Computer Use. This script sends only
documented file-bridge commands, and tags shutdown through the existing Session.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time

from autoplay_validation.io import digest, read_json, write_json
from autoplay_validation.process_session import Session
import capture_fixed_timestep as bridge
from capture_boss_armor import compare

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parent


def prepare(folder: Path, build: str) -> None:
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copytree(PROJECT / "assets", folder / "assets")
    shutil.copytree(PROJECT / "shader", folder / "shader")
    for path in PROJECT.glob("*.cso"):
        shutil.copy2(path, folder / path.name)
    for name in ("balance_autoplay", "balance_validation", "dynamic_balance", "auto_balance_loop"):
        path = folder / f"assets/data/{name}.json"
        if path.exists():
            config = read_json(path)
            config["enabled"] = False
            write_json(path, config)
    write_json(folder / "assets/data/game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": True,
        "bridge_directory": "runtime/game_mcp",
        "state_publish_interval_frames": 6, "command_poll_interval_frames": 1,
    })
    write_json(folder / "assets/data/player_deck.json", {"deck": [f"player_{build}"] * 5})
    write_json(folder / "saves/settings.json", {
        "bgm_volume": 0, "se_volume": 0, "fullscreen": False,
        "resolution_index": 0, "camera_shake_enabled": False,
        "frame_time_measurement_enabled": True,
    })


@contextmanager
def game(exe: Path, folder: Path, build: str, interactive: bool = False):
    prepare(folder, build)
    session = Session(folder, "legacy_rehide" if interactive else "hidden")
    report = {"exe": str(exe), "exe_sha256": digest(exe), "build": build}
    try:
        with (folder / "process.log").open("wb") as output:
            process = session.launch([str(exe)], output,
                env=dict(os.environ, DX22_TEST_PREDICTION="1"))
            report["process_id"] = process.pid
            state = folder / "runtime/game_mcp/game_state.json"
            bridge.wait_for(lambda: bridge.read(state).get("scene") == "title", process)
            yield process, state, report
    finally:
        report["cleanup"] = session.cleanup()
        write_json(folder / "identity.json", report)
    assert report["cleanup"]["exit_code"] == 0, report["cleanup"]


def ready(path: Path, process):
    return bridge.wait_for(lambda: s if "fire_shot" in
        (s := bridge.read(path)).get("available_actions", []) else None, process, 60)


def capture(args, folder: Path, build: str, count: int):
    with game(args.exe, folder, build) as (process, path, report):
        bridge.command(folder, process, "start_new_run", run_seed=20261120)
        positions = [(x, z) for z in (14, -18) for x in (-45, -27, -9, 9, 27, 45)]
        bridge.command(folder, process, "set_next_stage_layout", layout_id=f"portfolio_{count}",
            stage_type="normal", difficulty=1, par=5,
            enemies=[{"enemy_id": "enemy_strong", "x": x, "z": z} for x, z in positions[:count]])
        bridge.command(folder, process, "choose_destination", route_index=0)
        initial = ready(path, process)
        write_json(folder / "initial.json", initial)
        assert len(initial["enemies"]) == count
        if args.mode == "performance":
            initial = bridge.wait_for(lambda: s if (s := bridge.read(path)).get(
                "aiming_performance", {}).get("sample_count", 0) >= 180 else None, process, 60)
            report["aiming"] = initial["aiming_performance"]
            write_json(folder / "aiming.json", initial)
        target = initial["enemies"][0]["position"]
        origin = initial["player"]["position"]
        bridge.command(folder, process, "fire_shot", direction_x=target["x"]-origin["x"],
            direction_z=target["z"]-origin["z"], power=8)
        final = bridge.wait_for(lambda: s if (s := bridge.read(path)).get("game_state")
            in ("aiming_direction", "clear_reward", "game_over") else None, process, 90)
        write_json(folder / "final.json", final)
        report["frame_timing"] = final.get("frame_timing")
        report["physics_clock"] = final["physics_clock"]
        assert final["physics_clock"]["substep_limit_count"] == 0
        expected = folder / "runtime/shot_prediction_expected.json"
        if expected.exists():
            shutil.copy2(expected, folder / "expected.json")
            shutil.copy2(folder / "runtime/shot_prediction_actual.json", folder / "actual.json")
            report["prediction"] = compare(folder)
        else:
            assert args.mode == "performance", "Shot agreement requires a Debug executable"
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--mode", choices=["shots", "performance", "ui"], default="shots")
    parser.add_argument("--builds", nargs="+", choices=["standard", "heavy", "pierce", "bounce", "anchor"],
        default=["standard", "heavy", "pierce", "bounce", "anchor"])
    parser.add_argument("--counts", nargs="+", type=int, choices=[3, 6, 10, 12], default=[3])
    args = parser.parse_args()
    assert re.fullmatch(r"[A-Za-z0-9_-]+", args.label)
    args.exe = args.exe.resolve(strict=True)
    output = ROOT / "logs/portfolio_20261120" / args.label
    assert not output.exists(), "Use a fresh label"
    output.mkdir(parents=True)
    if args.mode == "ui":
        with game(args.exe, output / "ui", "standard", interactive=True) as (process, path, report):
            print(json.dumps({"ready": True, "pid": process.pid, "folder": str(output / "ui")}), flush=True)
            for line in sys.stdin:
                request = json.loads(line)
                if request.get("action") == "close": break
                if request.get("action") == "state":
                    state = bridge.read(path)
                    write_json(output / request.get("name", "ui_state.json"), state)
                    print(json.dumps({k: state.get(k) for k in ("scene", "game_state", "player", "available_actions", "save")}), flush=True)
                else:
                    state = bridge.read(path)
                    assert request["action"] in state["available_actions"], request
                    bridge.command(output / "ui", process, request.pop("action"), **request)
                    print("bridge command completed", flush=True)
        return
    results = []
    try:
        for build in args.builds:
            for count in args.counts:
                result = capture(args, output / f"{build}_{count}", build, count)
                results.append(result)
                write_json(output / "summary.json", results)
                print(json.dumps({"build": build, "enemies": count, "prediction": result.get("prediction"),
                    "aiming": result.get("aiming"), "cleanup": result["cleanup"]["exit_code"]}), flush=True)
    finally:
        write_json(output / "summary.json", results)


if __name__ == "__main__":
    main()
