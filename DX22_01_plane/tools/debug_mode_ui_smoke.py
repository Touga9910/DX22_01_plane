"""Interactive UI checks limited to a game process owned by this script."""
import ctypes
from ctypes import wintypes
import json
import os
import shutil
import subprocess
import sys
import time
from PIL import ImageGrab
import verify_debug_mode as v
h = v.h

folder = h.prepare(sys.argv[1], "ui", "standard")
shutil.copy2(h.PROJECT / "assets/data/player_deck.json", folder / "assets/data/player_deck.json")
h.write(folder / "saves/debug_battle_setup.json", h.read(h.PROJECT / "tools/runtime_tests/debug_mode/setup_win.json"))
save = folder / "saves/run_save.json"
save.write_bytes(v.SENTINEL)
user = ctypes.windll.user32
user.GetForegroundWindow.restype = wintypes.HWND
previous = user.GetForegroundWindow()
user.SetForegroundWindow.argtypes = [wintypes.HWND]
user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]

def screenshot(name):
    rect = wintypes.RECT(); user.GetWindowRect(window[0], ctypes.byref(rect))
    ImageGrab.grab(bbox=(rect.left,rect.top,rect.right,rect.bottom)).save(folder / (name + ".png"))
    print(str(folder / (name + ".png")), flush=True)

with (folder / "process.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([str(v.EXE)], cwd=folder, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        statepath = folder / "runtime/game_mcp/game_state.json"
        h.wait_for(lambda:h.read(statepath).get("scene") == "title", process)
        window = h.wait_for(lambda:h.window_for(process),process)
        user.ShowWindow(window[0],9); user.SetForegroundWindow(window[0]); time.sleep(.4)
        screenshot("title")
        print("READY (coordinates are client-relative)", flush=True)
        for line in sys.stdin:
            arg = json.loads(line)
            if arg["action"] == "quit": break
            if arg["action"] == "click":
                user.SetForegroundWindow(window[0])
                assert user.GetForegroundWindow() == window[0]
                point = wintypes.POINT(arg["x"],arg["y"]); user.ClientToScreen(window[0],ctypes.byref(point))
                user.SetCursorPos(point.x,point.y); time.sleep(.15)
                user.mouse_event(2,0,0,0,0); time.sleep(.15); user.mouse_event(4,0,0,0,0); time.sleep(.4)
            if arg["action"] == "shot":
                h.command(folder,process,"fire_shot",direction_x=1,direction_z=0,power=8)
                h.wait_for(lambda:h.read(statepath).get("debug_mode",{}).get("finished"),process,60)
            assert save.read_bytes() == v.SENTINEL, "Normal save changed!"
            if "name" in arg: screenshot(arg["name"])
            state = h.read(statepath)
            h.write(folder / "ui_latest.json",state)
            print(json.dumps({"scene":state.get("scene"),"debug":state.get("debug_mode"),"player":state.get("player"),"actions":state.get("available_actions")},ensure_ascii=False),flush=True)
    finally:
        if process.poll() is None:
            window = h.window_for(process)
            if window: user.PostThreadMessageW(window[1],0x0012,0,0)
            process.wait(timeout=20)
        user.SetForegroundWindow(previous)
        assert save.read_bytes() == v.SENTINEL
        print("CLOSED; normal save unchanged",flush=True)
