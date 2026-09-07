"""Replay isolated one-shot fixtures through the game's existing file bridge.

Never uses the live MCP directory or a user's saves. Only closes processes spawned
here, by posting WM_QUIT to their own main window thread (normal game shutdown).
The baseline executable/data must be archived before changing game code.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT / "tools/runtime_tests/fixed_timestep"
BASELINE = ROOT / "baseline"
CASES = [
    ("standard_chain", "standard", [(20, 8), (30, 12), (-35, -18)], (20, 8), 8),
    ("heavy_chain", "heavy", [(20, 8), (30, 12), (-35, -18)], (20, 8), 8),
    ("pierce_line", "pierce", [(20, 8), (30, 12), (-35, -18)], (20, 8), 8),
    ("bounce_wall", "bounce", [(28, 20), (45, -15), (-35, -18)], (1, 0.3), 8),
    ("anchor_contact", "anchor", [(20, 8), (30, 12), (-35, -18)], (20, 8), 8),
    ("standard_pocket", "standard", [(28, 20), (45, -15), (-35, -18)], (0, 1), 8),
]


def read(path):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return {}


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".writing")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    os.replace(temp, path)


def wait_for(predicate, process, timeout=45):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if process.poll() is not None:
            raise RuntimeError(f"Test game exited: {process.returncode}")
        result = predicate()
        if result:
            return result
        time.sleep(0.02)
    raise TimeoutError("Test game did not reach the expected state")


def command(folder, process, action, **arguments):
    bridge = folder / "runtime/game_mcp"
    command_id = uuid.uuid4().hex
    write(bridge / "pending_command.json", {
        "command_id": command_id, "action": action, "arguments": arguments})
    result = wait_for(lambda: (r if (r := read(bridge / "last_result.json")).get(
        "command_id") == command_id else None), process)
    if not result.get("ok"):
        raise AssertionError(result)
    return read(bridge / "game_state.json")


def window_for(process):
    found = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def visit(hwnd, _):
        pid = wintypes.DWORD()
        thread = ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(128)
        ctypes.windll.user32.GetClassNameW(hwnd, name, 128)
        if pid.value == process.pid and name.value == "DX22_01_plane_WindowClass":
            found.append((hwnd, thread))
        return True

    ctypes.windll.user32.EnumWindows(visit, 0)
    return found[0] if found else None


def prepare(label, case, ball):
    folder = ROOT / label / case
    if folder.exists():
        raise RuntimeError(f"Use a fresh capture label; already exists: {folder}")
    (folder / "assets").mkdir(parents=True)
    shutil.copytree(BASELINE / "data", folder / "assets/data")
    shutil.copytree(PROJECT / "shader", folder / "shader")
    for cso in PROJECT.glob("*.cso"):
        shutil.copy2(cso, folder / cso.name)
    # PowerShell creates junctions to immutable graphical resources; data is copied.
    for resource in ("model", "texture"):
        destination = str(folder / "assets" / resource).replace("'", "''")
        source = str(PROJECT / "assets" / resource).replace("'", "''")
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"New-Item -ItemType Junction -Path '{destination}' -Target '{source}' | Out-Null"],
                       check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    data = folder / "assets/data"
    for name in ("balance_validation", "balance_autoplay", "dynamic_balance"):
        config = read(data / f"{name}.json")
        config["enabled"] = False
        write(data / f"{name}.json", config)
    write(data / "player_deck.json", {"deck": [f"player_{ball}"] * 3})
    write(data / "game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": True,
        "bridge_directory": "runtime/game_mcp", "state_publish_interval_frames": 1,
        "command_poll_interval_frames": 1})
    write(folder / "saves/settings.json", {"bgm_volume": 0, "se_volume": 0,
        "fullscreen": False, "resolution_index": 0, "camera_shake_enabled": False})
    return folder


def pause_and_resume(folder, process):
    """Exercise the actual pause menu while the one-shot fixture is moving."""
    hwnd, _ = window_for(process)
    user = ctypes.windll.user32
    user.GetForegroundWindow.restype = wintypes.HWND
    user.SetForegroundWindow.argtypes = [wintypes.HWND]
    user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    previous = user.GetForegroundWindow()
    user.ShowWindow(hwnd, 9)
    user.SetForegroundWindow(hwnd)
    time.sleep(0.15)

    def escape():
        assert user.GetForegroundWindow() == hwnd, "Only send input to our test window"
        user.keybd_event(0x1B, 0, 0, 0)
        time.sleep(0.12)
        user.keybd_event(0x1B, 0, 2, 0)
        time.sleep(0.12)

    try:
        escape()
        state_path = folder / "runtime/game_mcp/game_state.json"
        paused = read(state_path)
        assert paused["game_state"] == "balls_moving"
        time.sleep(0.8)
        assert read(state_path)["sequence"] == paused["sequence"], "Pause must stop game updates"
        escape()
        resumed = wait_for(lambda: (s if (s := read(state_path)).get("sequence", 0) >
                            paused["sequence"] else None), process)
        assert resumed["physics_clock"]["tick"] - paused["physics_clock"]["tick"] < 25, "Paused time was replayed"
        write(folder / "pause_check.json", {"paused": paused["physics_clock"],
            "resumed": resumed["physics_clock"], "paused_seconds": 0.8, "passed": True})
    finally:
        user.ShowWindow(hwnd, 0)
        user.SetForegroundWindow(previous)


def capture(args):
    exe = Path(args.exe).resolve() if args.exe else (BASELINE / "DX22_01_plane.exe" if args.baseline else
           PROJECT.parent / "x64/Debug/DX22_01_plane.exe")
    results = []
    for case, ball, enemies, direction, power in CASES:
        if args.case and case != args.case:
            continue
        folder = prepare(args.label, case, ball)
        env = os.environ.copy()
        if args.render_hz:
            env["DX22_TEST_RENDER_HZ"] = str(args.render_hz)
        with (folder / "process.log").open("w", encoding="utf-8") as log:
            process = subprocess.Popen([str(exe)], cwd=folder, env=env,
                stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                wait_for(lambda: read(folder / "runtime/game_mcp/game_state.json").get("scene") == "title", process)
                window = wait_for(lambda: window_for(process), process)
                ctypes.windll.user32.ShowWindow(window[0], 0)
                command(folder, process, "start_new_run", run_seed=20260903,
                        controller_profile="fixed_timestep_fixture", build_profile=ball)
                kinds = ["enemy_tank", "enemy_striker", "enemy_strong"] if args.mixed_enemies else ["enemy_strong"] * len(enemies)
                placements = [{"enemy_id": kind, "x": x, "z": z}
                              for kind, (x, z) in zip(kinds, enemies)]
                if args.reverse_enemies:
                    placements.reverse()
                command(folder, process, "set_next_stage_layout", layout_id=case,
                        stage_type="normal", difficulty=1, par=4, enemies=placements)
                command(folder, process, "choose_destination", route_index=0)
                initial = wait_for(lambda: (s if "fire_shot" in (s := read(
                    folder / "runtime/game_mcp/game_state.json")).get("available_actions", []) else None), process)
                assert initial["player"]["ball"]["definition_id"] == f"player_{ball}", initial["player"]
                write(folder / "initial.json", initial)
                started = time.monotonic()
                command(folder, process, "fire_shot", direction_x=direction[0],
                        direction_z=direction[1], power=power)
                if args.pause:
                    pause_and_resume(folder, process)
                frames = []
                sequence = -1
                def finished():
                    nonlocal sequence
                    state = read(folder / "runtime/game_mcp/game_state.json")
                    if not state or state["sequence"] == sequence:
                        return None
                    sequence = state["sequence"]
                    frames.append(state)
                    return state if state["game_state"] in ("aiming_direction", "clear_reward", "game_over") else None
                final = wait_for(finished, process, 60)
                write(folder / "frames.json", frames)
                write(folder / "final.json", final)
                results.append({"case": case, "ball": ball, "direction": direction,
                    "power": power, "elapsed_seconds": time.monotonic() - started,
                    "final_player": final["player"], "final_enemies": final["enemies"],
                    "physics_clock": final.get("physics_clock"),
                    "game_state": final["game_state"]})
                print(f"{args.label}: {case}: {final['game_state']}, HP={final['player']['current_hp']}", flush=True)
            finally:
                if process.poll() is None:
                    window = window_for(process)
                    if window:
                        ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
                    exit_code = process.wait(timeout=15)
                    assert exit_code == 0, f"Test game did not exit cleanly: {exit_code}"
    write(ROOT / args.label / "summary.json", {
        "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "render_hz": args.render_hz or 60, "results": results})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--render-hz", type=int)
    parser.add_argument("--case")
    parser.add_argument("--pause", action="store_true")
    parser.add_argument("--reverse-enemies", action="store_true")
    parser.add_argument("--mixed-enemies", action="store_true")
    parser.add_argument("--exe")
    capture(parser.parse_args())
