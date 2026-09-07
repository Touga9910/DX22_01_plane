"""Exercise debug fixtures in isolated folders, never in the user's live run."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid
import capture_fixed_timestep as h

h.ROOT = h.PROJECT / "tools/runtime_tests/debug_mode/captures"
h.BASELINE = h.PROJECT / "assets"
EXE = h.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
SENTINEL = b'{"debug_test_sentinel":"must remain byte-identical"}'


def run_case(label, name):
    folder = h.prepare(label, name, "standard")
    shutil.copy2(h.PROJECT / "assets/data/player_deck.json", folder / "assets/data/player_deck.json")
    preset = h.read(h.PROJECT / f"tools/runtime_tests/debug_mode/setup_{name}.json")
    h.write(folder / "saves/debug_battle_setup.json", preset)
    save = folder / "saves/run_save.json"
    save.write_bytes(SENTINEL)
    with (folder / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(EXE), "--debug-battle"], cwd=folder,
            env=dict(os.environ, DX22_TEST_PREDICTION="1"), stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            path = folder / "runtime/game_mcp/game_state.json"
            state = h.wait_for(lambda: s if (s := h.read(path)).get("debug_mode", {}).get("active") and "fire_shot" in s.get("available_actions", []) else None, process)
            window = h.wait_for(lambda: h.window_for(process), process)
            ctypes.windll.user32.ShowWindow(window[0], 0)
            h.write(folder / "initial.json", state)
            assert state["player"]["current_hp"] == preset["hp"], state["player"]
            assert state["player"]["max_hp"] == preset["max_hp"]
            assert state["player"]["money"] == preset["money"]
            assert not state["dynamic_balance"]["enabled"]
            assert len(state["enemies"]) == len(preset["enemies"])
            assert save.read_bytes() == SENTINEL
            assert not list((folder / "logs/balance").glob("run_*.json"))
            debug_logs = list((folder / "logs/debug_battle").glob("run_*.json"))
            assert len(debug_logs) == 1
            run = h.read(debug_logs[0])
            assert run["controller_type"] == "debug_sandbox" and run["run_context"]["debug_sandbox"]
            assert len(run["initial_player"]["deck"]) == len(preset["deck"])
            if name == "boss":
                assert state["boss_state"]["armor"] == 0
                assert state["boss_state"]["break_shots_remaining"] == 1
                assert state["boss_state"]["hp"] == 30
                assert len(state["break_balls"]) == 2
                assert abs(state["player"]["position"]["z"] + 10) < .001
                h.command(folder, process, "fire_shot", direction_x=1, direction_z=0, power=2)
                state = h.wait_for(lambda: s if (s := h.read(path)).get("boss_state", {}).get("armor") == 2 and "fire_shot" in s.get("available_actions", []) else None, process)
            else:
                h.command(folder, process, "fire_shot", direction_x=1 if name == "win" else -1, direction_z=0, power=8 if name == "win" else 2)
                state = h.wait_for(lambda: s if (s := h.read(path)).get("debug_mode", {}).get("finished") else None, process, 60)
                assert state["debug_mode"]["editor_open"]
                assert set(state["available_actions"]) == {"validate_stage_layout", "propose_stage_layout"}
                if name == "loss": assert state["player"]["current_hp"] == 0
                run = h.read(debug_logs[0])
                assert run["run_result"]["result"] == ("debug_clear" if name == "win" else "debug_game_over")
            assert save.read_bytes() == SENTINEL
            # Mode-incompatible bridge writes are rejected without altering the save or mode.
            cid = uuid.uuid4().hex
            h.write(folder / "runtime/game_mcp/pending_command.json", {"command_id":cid,"action":"start_new_run","arguments":{}})
            response = h.wait_for(lambda: r if (r := h.read(folder / "runtime/game_mcp/last_result.json")).get("command_id") == cid else None, process)
            assert not response["ok"] and save.read_bytes() == SENTINEL
            h.write(folder / "final.json", state)
            print(f"PASS {name}: configured state, combat transition, protected save and isolated log", flush=True)
        finally:
            if process.poll() is None:
                window = h.window_for(process)
                if window: ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
                process.wait(timeout=20)
            assert save.read_bytes() == SENTINEL
            h.write(folder / "exe.json", {"sha256":hashlib.sha256(EXE.read_bytes()).hexdigest(),"exit_code":process.returncode})
    return str(folder)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    report = [run_case(args.label, name) for name in ("win", "loss", "boss")]
    h.write(h.ROOT / args.label / "verified.json", {"cases":report,"passed":True})
