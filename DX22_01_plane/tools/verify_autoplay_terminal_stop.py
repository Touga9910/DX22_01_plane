"""Verify an isolated F8 autoplay stops on its terminal result."""
import ctypes
import subprocess
import sys
import time

import capture_fixed_timestep as h


h.ROOT = h.PROJECT / "tools/runtime_tests/autoplay_terminal_stop"
folder = h.prepare(sys.argv[1], "game_over", "standard")

autoplay = h.read(folder / "assets/data/balance_autoplay.json")
autoplay.update({
    "enabled": True,
    "restart_after_game_over": True,
    "stop_after_current_run": False,
    "decision_delay_frames": 30,
    "max_runs": 0,
})
h.write(folder / "assets/data/balance_autoplay.json", autoplay)

player = h.read(folder / "assets/data/player_status.json")
player["currentHp"] = 1
player["maxHp"] = 1
h.write(folder / "assets/data/player_status.json", player)

enemies = h.read(folder / "assets/data/enemy_data.json")
for enemy in enemies["enemies"]:
    enemy["status"]["maxHp"] = 100
    enemy["status"]["attack"] = 100
h.write(folder / "assets/data/enemy_data.json", enemies)

exe = h.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
with (folder / "process.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([str(exe)], cwd=folder, stdout=log, stderr=log,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        state_path = folder / "runtime/game_mcp/game_state.json"
        window = h.wait_for(lambda: h.window_for(process), process)
        h.wait_for(lambda: state if (state := h.read(state_path)).get("scene") == "battle"
                   else None, process)
        user = ctypes.windll.user32
        user.ShowWindow(window[0], 9)
        user.SetForegroundWindow(window[0])
        time.sleep(0.15)
        user.keybd_event(0x78, 0, 0, 0)  # F9
        user.keybd_event(0x78, 0, 2, 0)
        h.wait_for(lambda: state if (state := h.read(state_path)).get(
            "autoplay_stop_after_current_run") is True else None, process)
        user.ShowWindow(window[0], 0)
        result = h.wait_for(
            lambda: state if (state := h.read(state_path)).get("scene") == "result"
            and state.get("autoplay_enabled") is False else None,
            process, timeout=60)
        assert result["autoplay_stop_after_current_run"] is False
        time.sleep(1)
        after = h.read(state_path)
        assert after["scene"] == "result"
        assert after["autoplay_enabled"] is False
        h.write(folder / "verified.json", {"passed": True, "result": result})
        print("PASS F9 armed terminal stop; autoplay stopped at game over and did not start another run")
    finally:
        if process.poll() is None:
            window = h.window_for(process)
            if window:
                ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
            process.wait(timeout=20)
