"""Exercise only an owned, isolated game window; leave the user's data/run untouched."""
import ctypes
from ctypes import wintypes
import json
import shutil
import subprocess
import sys
import time
import uuid
from PIL import ImageGrab
import verify_debug_mode as v
h = v.h
h.ROOT = h.PROJECT / "tools/runtime_tests/stage_editor/captures"
folder = h.prepare(sys.argv[1], "ui", "standard")
shutil.copy2(h.PROJECT / "assets/data/player_deck.json", folder / "assets/data/player_deck.json")
save = folder / "saves/run_save.json"
save.write_bytes(v.SENTINEL)
original_stages = (folder / "assets/data/stage_01.json").read_bytes()
user = ctypes.windll.user32
user.SetProcessDPIAware()
user.GetForegroundWindow.restype = wintypes.HWND
user.SetForegroundWindow.argtypes = [wintypes.HWND]
user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
previous = user.GetForegroundWindow()

def screenshot(name):
    rect = wintypes.RECT(); user.GetClientRect(window[0], ctypes.byref(rect))
    origin = wintypes.POINT(0, 0); user.ClientToScreen(window[0], ctypes.byref(origin))
    path = folder / (name + ".png")
    ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom)).save(path)
    print(path, flush=True)

def point(x, y):
    p = wintypes.POINT(x, y); user.ClientToScreen(window[0], ctypes.byref(p)); user.SetCursorPos(p.x, p.y); time.sleep(.1)

def request(action, arguments):
    cid = uuid.uuid4().hex
    h.write(folder / "runtime/game_mcp/pending_command.json", {"command_id":cid, "action":action, "arguments":arguments})
    return h.wait_for(lambda: r if (r := h.read(folder / "runtime/game_mcp/last_result.json")).get("command_id") == cid else None, process)

with (folder / "process.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([str(v.EXE)], cwd=folder, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        statepath = folder / "runtime/game_mcp/game_state.json"
        h.wait_for(lambda:h.read(statepath).get("scene") == "title", process)
        window = h.wait_for(lambda:h.window_for(process),process)
        user.ShowWindow(window[0],9); user.SetForegroundWindow(window[0]); time.sleep(.4)
        assert request("open_stage_editor", {})["ok"]
        state = h.wait_for(lambda:s if (s := h.read(statepath)).get("stage_editor", {}).get("open") else None, process)
        draft = state["stage_editor"]["draft"]
        invalid = json.loads(json.dumps(draft)); invalid["enemies"][0]["x"] = 10000
        assert not request("validate_stage_layout", {"layout":invalid})["ok"]
        assert request("validate_stage_layout", {"layout":draft})["ok"]
        assert not request("propose_stage_layout", {"layout":draft, "expected_revision":-1})["ok"]
        assert not request("start_new_run", {})["ok"]
        assert (folder / "assets/data/stage_01.json").read_bytes() == original_stages
        screenshot("editor_initial")
        print("READY: isolated stage editor; bridge validation and normal-save guard passed", flush=True)
        for line in sys.stdin:
            arg = json.loads(line)
            if arg["action"] == "quit": break
            if arg["action"] in ("click", "drag"):
                user.SetForegroundWindow(window[0]); assert user.GetForegroundWindow() == window[0]
                point(arg["x"], arg["y"]); user.mouse_event(2,0,0,0,0); time.sleep(.15)
                if arg["action"] == "drag":
                    for i in range(1, 9): point(round(arg["x"] + (arg["to_x"]-arg["x"])*i/8), round(arg["y"] + (arg["to_y"]-arg["y"])*i/8))
                user.mouse_event(4,0,0,0,0); time.sleep(.4)
            if arg["action"] == "propose":
                state = h.read(statepath); draft = state["stage_editor"]["draft"]
                proposal = json.loads(json.dumps(draft)); proposal["id"] = "editor_runtime_test"
                proposal["enemies"] = [{"enemy_id":"enemy_normal","x":x,"z":z} for x,z in [(-30,12),(-10,20),(15,-18),(42,8)]]
                revision = state["stage_editor"]["revision"]
                assert request("propose_stage_layout", {"layout":proposal,"expected_revision":revision})["ok"]
                time.sleep(.15)
                assert h.read(statepath)["stage_editor"]["draft"] == draft
                assert (folder / "assets/data/stage_01.json").read_bytes() == original_stages
            if arg["action"] == "verify_save":
                state = h.read(statepath); draft = state["stage_editor"]["draft"]
                data = h.read(folder / "assets/data/stage_01.json")
                entry = next(e for e in data["stages"] if e["id"] == draft["id"])
                assert entry["preserveLayout"] and len(entry["enemies"]) == 4
                assert [(e["position"][0], e["position"][2]) for e in entry["enemies"]] == [(e["x"], e["z"]) for e in draft["enemies"]]
                before = json.loads(original_stages)
                assert data["stages"][:-1] == before["stages"]
                assert (folder / "assets/data/stage_01.json.editor.bak").read_bytes() == original_stages
                print("PASS saved authored positions and preserved all original stages", flush=True)
            if arg["action"] == "verify_trial":
                state = h.wait_for(lambda:s if (s:=h.read(statepath)).get("debug_mode", {}).get("active") and "fire_shot" in s.get("available_actions", []) else None,process)
                draft = state["stage_editor"]["draft"]
                assert len(state["enemies"]) == len(draft["enemies"]) == 4
                for actual, expected in zip(state["enemies"], draft["enemies"]):
                    assert abs(actual["position"]["x"] - expected["x"]) < .001
                    assert abs(actual["position"]["z"] - expected["z"]) < .001
                h.write(folder / "verified.json", {"passed":True,"state":state})
                print("PASS exact trial positions and protected normal save", flush=True)
            assert save.read_bytes() == v.SENTINEL
            if "name" in arg: screenshot(arg["name"])
            state = h.read(statepath); h.write(folder / "ui_latest.json",state)
            print(json.dumps({"scene":state.get("scene"),"debug":state.get("debug_mode"),"editor":state.get("stage_editor",{}),"actions":state.get("available_actions")},ensure_ascii=False),flush=True)
    finally:
        if process.poll() is None:
            window = h.window_for(process)
            if window: user.PostThreadMessageW(window[1],0x0012,0,0)
            process.wait(timeout=20)
        user.SetForegroundWindow(previous)
        assert save.read_bytes() == v.SENTINEL
        print("CLOSED; normal save unchanged",flush=True)
