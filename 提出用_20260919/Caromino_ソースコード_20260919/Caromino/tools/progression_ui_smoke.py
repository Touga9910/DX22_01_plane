"""Capture the mouse-opened progression screen in an isolated game window."""
import ctypes
from ctypes import wintypes
import subprocess
import sys
import time

from PIL import ImageGrab
import capture_fixed_timestep as h


h.ROOT = h.PROJECT / "tools/runtime_tests/meta_progression_ui"
folder = h.prepare(sys.argv[1], "ui", "standard")
h.write(folder / "saves/profile_progress.json", {
    "version": 1, "total_runs": 12, "total_clears": 11,
    "highest_area": 15, "highest_unlocked_ascension": 10,
    "selected_ascension": 10,
    "achievements": ["first_victory", "damage_eight", "area_five",
                     "midboss", "collector", "damage_fifteen", "final_boss"]})
exe = h.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
user = ctypes.windll.user32
user.SetProcessDPIAware()

with (folder / "process.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([str(exe)], cwd=folder, stdout=log, stderr=log,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        state_path = folder / "runtime/game_mcp/game_state.json"
        h.wait_for(lambda: h.read(state_path).get("scene") == "title", process)
        window = h.wait_for(lambda: h.window_for(process), process)
        hwnd = window[0]
        user.ShowWindow(hwnd, 9)
        user.SetForegroundWindow(hwnd)
        time.sleep(0.5)
        point = wintypes.POINT(800, 585)
        user.ClientToScreen(hwnd, ctypes.byref(point))
        user.SetCursorPos(point.x, point.y)
        user.mouse_event(2, 0, 0, 0, 0)
        user.mouse_event(4, 0, 0, 0, 0)
        time.sleep(0.7)
        rect = wintypes.RECT()
        user.GetClientRect(hwnd, ctypes.byref(rect))
        origin = wintypes.POINT(0, 0)
        user.ClientToScreen(hwnd, ctypes.byref(origin))
        output = folder / "progression.png"
        ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + rect.right,
                             origin.y + rect.bottom)).save(output)
        print(output)
    finally:
        if process.poll() is None:
            window = h.window_for(process)
            if window:
                user.PostThreadMessageW(window[1], 0x0012, 0, 0)
            process.wait(timeout=20)
