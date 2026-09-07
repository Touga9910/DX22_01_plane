"""Verify progression gates and Ascension effects in isolated game processes."""
import ctypes
import math
import subprocess
import sys

import capture_fixed_timestep as h


ROOT = h.PROJECT / "tools/runtime_tests/meta_progression"
h.ROOT = ROOT


def run_game(folder, body):
    exe = h.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
    with (folder / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(exe)], cwd=folder, stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            state_path = folder / "runtime/game_mcp/game_state.json"
            h.wait_for(lambda: h.read(state_path).get("scene") == "title", process)
            window = h.wait_for(lambda: h.window_for(process), process)
            ctypes.windll.user32.ShowWindow(window[0], 0)
            body(process, state_path)
        finally:
            if process.poll() is None:
                window = h.window_for(process)
                if window:
                    ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
                process.wait(timeout=20)


def verify_fresh_profile(label):
    folder = h.prepare(label, "fresh", "standard")

    def check(process, state_path):
        state = h.read(state_path)
        progression = state["meta_progression"]
        assert progression["active_ascension"] == 0
        assert progression["highest_unlocked_ascension"] == 0
        assert len(progression["ascension_rules"]) == 11
        assert not any(item["unlocked"] for item in progression["achievements"])
        assert [item["definition_id"] for item in state["catalog_balls"]] == [
            "player_standard", "player_heavy"]
        ball_unlocks = {item["definition_id"]: item["unlocked"]
                        for item in progression["ball_unlocks"]}
        assert ball_unlocks == {
            "player_standard": True, "player_heavy": True,
            "player_pierce": False, "player_bounce": False,
            "player_anchor": False}
        assert sum(not relic["unlocked"] for relic in state["relics"]) == 5
        h.write(folder / "verified.json", {"passed": True, "state": state})

    run_game(folder, check)


def verify_ascension_ten(label):
    folder = h.prepare(label, "ascension_10", "standard")
    achievements = [
        "first_victory", "damage_eight", "area_five", "midboss",
        "collector", "damage_fifteen", "final_boss"]
    profile = {
        "version": 1, "total_runs": 12, "total_clears": 11,
        "highest_area": 15, "highest_unlocked_ascension": 10,
        "selected_ascension": 10, "achievements": achievements}
    h.write(folder / "saves/profile_progress.json", profile)

    def check(process, state_path):
        title = h.read(state_path)
        assert len(title["catalog_balls"]) == 5
        assert all(relic["unlocked"] for relic in title["relics"])
        state = h.command(folder, process, "start_new_run", run_seed=20260905)
        state = h.wait_for(
            lambda: s if (s := h.read(state_path)).get("scene") == "stage_select" else None,
            process)
        progression = state["meta_progression"]
        assert progression["active_ascension"] == 10
        assert abs(progression["enemy_hp_multiplier"] - 1.35) < 0.0001
        assert progression["enemy_attack_bonus"] == 3
        assert progression["persistent_rewards_eligible"] is False
        assert state["player"]["max_hp"] == 40
        assert state["player"]["current_hp"] == 40
        assert state["rest_heal"]["heal_percent"] == 15

        h.command(folder, process, "set_next_stage_layout",
                  layout_id="ascension_check", stage_type="normal",
                  difficulty=1, par=4,
                  enemies=[{"enemy_id": "enemy_normal", "x": 0, "z": 12}])
        route = next(item for item in state["route_options"]
                     if item["destination"] == "battle")
        h.command(folder, process, "choose_destination",
                  route_index=route["route_index"])
        battle = h.wait_for(
            lambda: s if (s := h.read(state_path)).get("scene") == "battle"
            and len(s.get("enemies", [])) == 1 else None, process)
        enemy = battle["enemies"][0]
        enemy_source = h.read(folder / "assets/data/enemy_ball.json")
        definitions = enemy_source.get("enemies", enemy_source)
        if isinstance(definitions, list):
            base = next(item for item in definitions if item.get("id") == "enemy_normal")
            base_hp = base.get("maxHp", base.get("max_hp"))
            base_attack = base.get("attack", base.get("status", {}).get("attack"))
            assert enemy["max_hp"] == math.floor(base_hp * 1.35 + 0.5)
            assert enemy["attack"] == base_attack + 3
        else:
            assert enemy["attack"] >= 4
        assert h.read(folder / "saves/profile_progress.json") == profile
        h.write(folder / "verified.json", {
            "passed": True, "title": title, "battle": battle})

    run_game(folder, check)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_meta_progression.py LABEL")
    verify_fresh_profile(sys.argv[1])
    verify_ascension_ten(sys.argv[1])
    print("PASS fresh unlock gates, MCP visibility, A10 player/enemy effects, and no MCP profile mutation")
