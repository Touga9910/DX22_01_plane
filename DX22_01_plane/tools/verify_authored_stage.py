"""Verify saved authoring data is honored through the ordinary run path (no override)."""
import ctypes
import subprocess
import sys
import verify_debug_mode as v
h = v.h
h.ROOT = h.PROJECT / "tools/runtime_tests/stage_editor/captures"

folder = h.prepare(sys.argv[1], "ordinary_run", "standard")
source = h.read(h.ROOT / "initial_check/ui/assets/data/stage_01.json")
stage = next(s for s in source["stages"] if s["id"] == "editor_runtime_test")
h.write(folder / "assets/data/stage_01.json", {"stages":[stage]})
with (folder / "process.log").open("w", encoding="utf-8") as log:
    process = subprocess.Popen([str(v.EXE)], cwd=folder, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        path = folder / "runtime/game_mcp/game_state.json"
        h.wait_for(lambda:h.read(path).get("scene") == "title",process)
        window = h.wait_for(lambda:h.window_for(process),process)
        ctypes.windll.user32.ShowWindow(window[0],0)
        h.command(folder,process,"start_new_run",run_seed=123)
        state = h.wait_for(lambda:s if (s:=h.read(path)).get("scene") == "stage_select" else None,process)
        print(state["route_options"],flush=True)
        route = next(r for r in state["route_options"] if r.get("destination") == "battle")
        h.command(folder,process,"choose_destination",route_index=route["route_index"])
        state = h.wait_for(lambda:s if (s:=h.read(path)).get("scene") == "battle" and len(s.get("enemies",[])) == 4 else None,process)
        assert not state["debug_mode"]["active"]
        for actual, expected in zip(state["enemies"], stage["enemies"]):
            assert abs(actual["position"]["x"] - expected["position"][0]) < .001
            assert abs(actual["position"]["z"] - expected["position"][2]) < .001
        h.write(folder / "verified.json", {"passed":True,"state":state})
        print("PASS ordinary run preserves all four saved positions; no debug override",flush=True)
    finally:
        if process.poll() is None:
            window = h.window_for(process)
            if window: ctypes.windll.user32.PostThreadMessageW(window[1],0x0012,0,0)
            process.wait(timeout=20)
