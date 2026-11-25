"""Verify shop entry/save/resume with a Debug game and --write-shop-fixtures output.

Uses private current-asset copies and the file bridge; performs no GUI input.
Example: python tools/verify_audit_shop.py --exe <Debug.exe> --fixture <shop_entry_fixture.json>
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools/game_mcp"))
from bridge_store import GameBridgeError, GameBridgeStore


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def wait_for(store: GameBridgeStore, process: subprocess.Popen, predicate, timeout=45) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Test game exited: pid={process.pid}, exit={process.returncode}")
        try:
            state = store.read_state()
            if predicate(state):
                return state
        except GameBridgeError:
            pass
        time.sleep(0.05)
    raise TimeoutError("Game did not reach the expected state; use a Debug executable with DX22_TEST_RESUME_RUN support")


@contextmanager
def running_game(exe: Path, work: Path, phase: str, report: dict):
    bridge = work / "runtime" / phase  # A fresh bridge prevents stale restart state.
    write(work / "assets/data/game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": True,
        "bridge_directory": f"runtime/{phase}", "state_publish_interval_frames": 1,
        "command_poll_interval_frames": 1})
    env = dict(os.environ, DX22_TEST_HIDDEN_WINDOW="1", DX22_TEST_RESUME_RUN="1")
    with (work.parent / f"{phase}_process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(exe)], cwd=work, env=env,
            stdin=subprocess.DEVNULL, stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        child = {"phase": phase, "pid": process.pid,
            "bridge_config_sha256": digest(work / "assets/data/game_mcp_bridge.json")}
        report["children"].append(child)
        try:
            yield GameBridgeStore(bridge), process
        finally:
            # Only stop/reap this Popen child, including assertion/timeout failures.
            if process.poll() is None:
                process.terminate()
            try:
                child["exit"] = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                child["exit"] = process.wait(timeout=10)


def offers(state: dict) -> tuple[list[int], list[int]]:
    return ([item["catalog_index"] for item in state["shop_ball_offers"]],
        [item["index"] for item in state["relics"] if item["shop_offered"]])


def verify(args) -> Path:
    exe, fixture = args.exe.resolve(), args.fixture.resolve()
    if not exe.is_file() or not fixture.is_file():
        raise ValueError("--exe and --fixture must name existing files")
    folder = PROJECT.parent / "logs/audit_shop" / args.label
    folder.mkdir(parents=True, exist_ok=False)
    work = folder / "work"
    work.mkdir()
    report = {"started_at_utc": datetime.now(timezone.utc).isoformat(),
        "exe": str(exe), "exe_sha256": digest(exe), "fixture": str(fixture),
        "fixture_sha256": digest(fixture), "script_sha256": digest(Path(__file__)),
        "children": [], "passed": False}
    try:
        shutil.copytree(PROJECT / "assets", work / "assets")
        shutil.copytree(PROJECT / "shader", work / "shader")
        for cso in PROJECT.glob("*.cso"):
            shutil.copy2(cso, work / cso.name)
        (work / "saves").mkdir()
        shutil.copy2(fixture, work / "saves/run_save.json")
        for name in ("balance_validation", "balance_autoplay", "dynamic_balance"):
            path = work / f"assets/data/{name}.json"
            config = read(path)
            config["enabled"] = False
            write(path, config)
        write(work / "saves/settings.json", {"bgm_volume": 0, "se_volume": 0,
            "fullscreen": False, "resolution_index": 0})
        report["data_sha256"] = {p.name: digest(p) for p in sorted((work / "assets/data").glob("*.json"))
            if p.name != "game_mcp_bridge.json"}
        with running_game(exe, work, "entry", report) as (store, process):
            selected = wait_for(store, process, lambda s: s["scene"] == "stage_select"
                and "choose_destination" in s["available_actions"])
            write(folder / "select_state.json", selected)
            route = next(r for r in selected["route_options"] if r["destination"] == "shop")
            result = store.submit_command("choose_destination", {"route_index": route["route_index"]})
            write(folder / "entry_command.json", result)
            assert result.get("ok"), result
            state = wait_for(store, process, lambda s: s["scene"] == "shop")
            entry = read(work / "saves/run_save.json")
            entry_save_time = (work / "saves/run_save.json").stat().st_mtime_ns
            write(folder / "entry_state.json", state)
            write(folder / "entry_save.json", entry)
            before = entry["payload"]
            assert before["resume_scene"] == "shop"
            saved = before["scene_state"]
            assert offers(state) == (saved["shop_ball_offers"], sorted(saved["shop_relic_offers"]))
            report["entry_offers_match"] = True
        with running_game(exe, work, "resume", report) as (store, process):
            state = wait_for(store, process, lambda s: s["scene"] == "shop")
            resumed = read(work / "saves/run_save.json")
            assert (work / "saves/run_save.json").stat().st_mtime_ns != entry_save_time, "Resume hook did not rewrite the save"
            write(folder / "resume_state.json", state)
            write(folder / "resume_save.json", resumed)
            after = resumed["payload"]
            assert after["scene_state"] == before["scene_state"]
            assert offers(state) == offers(read(folder / "entry_state.json"))
            assert after["deck"]["random_state"] == before["deck"]["random_state"]
            assert after["random"]["relic_state"] == before["random"]["relic_state"]
            report.update(shop_offers_preserved=True, deck_rng_preserved=True,
                relic_rng_preserved=True, resume_save_rewritten=True)
        report["passed"] = True
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        report["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        write(folder / "summary.json", report)
    return folder


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path, help="Debug game executable")
    parser.add_argument("--fixture", required=True, type=Path, help="test_run_save_runtime --write-shop-fixtures output")
    parser.add_argument("--label", default=datetime.now().strftime("shop_%Y%m%d_%H%M%S_%f"))
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        parser.error("--label must contain only ASCII letters, digits, hyphens, or underscores")
    print(f"PASS: {verify(args) / 'summary.json'}")


if __name__ == "__main__":
    main()
