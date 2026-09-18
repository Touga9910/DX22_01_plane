"""Verify stop shield, chain impact and refractive pierce in isolated games."""

from __future__ import annotations

import ctypes
import json
import os
import shutil
import subprocess
import sys

import capture_fixed_timestep as harness


def close_game(process):
    if process.poll() is not None:
        return
    window = harness.window_for(process)
    if window:
        ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
    process.wait(timeout=20)


def start_case(label, case, deck, enemies):
    folder = harness.prepare(label, case, "standard")
    data = folder / "assets/data"
    shutil.copy2(harness.PROJECT / "assets/data/player_status.json", data / "player_status.json")
    harness.write(data / "player_deck.json", {"deck": deck})
    exe = harness.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
    env = dict(os.environ, DX22_TEST_PREDICTION="1")
    log = (folder / "process.log").open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(exe)], cwd=folder, env=env, stdout=log, stderr=log,
        creationflags=subprocess.CREATE_NO_WINDOW)
    state_path = folder / "runtime/game_mcp/game_state.json"
    harness.wait_for(lambda: harness.read(state_path).get("scene") == "title", process)
    window = harness.wait_for(lambda: harness.window_for(process), process)
    ctypes.windll.user32.ShowWindow(window[0], 0)
    harness.command(folder, process, "start_new_run", run_seed=20260912)
    harness.command(
        folder, process, "set_next_stage_layout", layout_id=case,
        stage_type="normal", difficulty=1, par=6, enemies=enemies)
    state = harness.read(state_path)
    route = next(item for item in state["route_options"] if item["destination"] == "battle")
    harness.command(folder, process, "choose_destination", route_index=route["route_index"])
    return folder, process, log, state_path


def wait_offer(folder, process, state_path, definition_id):
    state = harness.wait_for(
        lambda: value if "select_ball" in (value := harness.read(state_path)).get(
            "available_actions", []) else None, process)
    offer = next(ball for ball in state["offered_balls"] if ball["definition_id"] == definition_id)
    harness.command(folder, process, "select_ball", offer_index=offer["index"])
    return state, offer


def fire_and_finish(folder, process, state_path, direction_x, direction_z, power):
    harness.command(
        folder, process, "fire_shot", direction_x=direction_x,
        direction_z=direction_z, power=power)
    return harness.wait_for(
        lambda: value if (value := harness.read(state_path)).get("game_state") in
        ("aiming_direction", "clear_reward", "game_over") else None,
        process, 60)


def assert_prediction_matches(folder):
    expected = harness.read(folder / "runtime/shot_prediction_expected.json")
    actual = harness.read(folder / "runtime/shot_prediction_actual.json")
    assert expected["complete"] and expected["world_unchanged"]
    for field in ("player_enemy_contacts", "enemy_enemy_contacts"):
        assert expected[field] == actual[field], (field, expected[field], actual[field])
    expected_balls = {ball["id"]: ball for ball in expected["balls"]}
    actual_balls = {ball["id"]: ball for ball in actual["balls"]}
    assert expected_balls.keys() == actual_balls.keys()
    maximum_error = 0.0
    for key, expected_ball in expected_balls.items():
        actual_ball = actual_balls[key]
        for field in ("position", "velocity"):
            for lhs, rhs in zip(expected_ball[field], actual_ball[field]):
                maximum_error = max(maximum_error, abs(lhs - rhs))
                assert abs(lhs - rhs) <= 1e-5, (key, field, lhs, rhs)
        for field in ("hp", "active", "defeated", "pocketed"):
            assert expected_ball[field] == actual_ball[field], (key, field)
    return expected, actual, maximum_error


def run_chain(label):
    folder, process, log, state_path = start_case(
        label, "chain_impact",
        ["player_chain_impact"] * 3,
        [
            {"enemy_id": "enemy_tank", "x": 20, "z": 0},
            {"enemy_id": "enemy_tank", "x": 20, "z": 9},
            {"enemy_id": "enemy_tank", "x": -45, "z": 20},
        ])
    try:
        state, offer = wait_offer(folder, process, state_path, "player_chain_impact")
        assert offer["category"] == "heavy"
        initial_hp = {enemy["target_id"]: enemy["hp"] for enemy in state["enemies"]}
        final = fire_and_finish(folder, process, state_path, 1, 0, 6)
        expected, _, error = assert_prediction_matches(folder)
        by_position = sorted(final["enemies"], key=lambda enemy: enemy["position"]["z"])
        direct_or_near = [enemy for enemy in final["enemies"] if float(enemy["position"]["x"]) > 10]
        far = next(enemy for enemy in final["enemies"] if float(enemy["position"]["x"]) < 0)
        assert len(direct_or_near) == 2
        assert all(enemy["hp"] < initial_hp[enemy["target_id"]] for enemy in direct_or_near)
        assert far["hp"] == initial_hp[far["target_id"]]
        assert expected["chain_impact_hits"] == 1
        return {"prediction_max_error": error, "chain_hits": expected["chain_impact_hits"]}
    finally:
        close_game(process)
        log.close()


def run_refract(label):
    folder, process, log, state_path = start_case(
        label, "refractive_pierce",
        ["player_refractive_pierce"] * 3,
        [{"enemy_id": "enemy_tank", "x": -3, "z": 20}])
    try:
        _, offer = wait_offer(folder, process, state_path, "player_refractive_pierce")
        assert offer["category"] == "pierce"
        assert offer["status"]["refractAfterPierce"]
        final = fire_and_finish(folder, process, state_path, 0, 1, 7)
        expected, _, error = assert_prediction_matches(folder)
        assert expected["player_enemy_contacts"] >= 1
        target = final["enemies"][0]
        assert abs(float(target["position"]["x"]) + 3.0) <= 1e-5
        assert abs(float(target["position"]["z"]) - 20.0) <= 1e-5
        return {"prediction_max_error": error,
                "player_enemy_contacts": expected["player_enemy_contacts"],
                "enemy_remained_stationary": True,
                "glancing_contact": True}
    finally:
        close_game(process)
        log.close()


def run_shield(label):
    folder, process, log, state_path = start_case(
        label, "stop_shield",
        ["player_stop_shield"] * 3,
        [{"enemy_id": "enemy_strong", "x": -55, "z": -20}])
    try:
        state, offer = wait_offer(folder, process, state_path, "player_stop_shield")
        assert offer["category"] == "anchor"
        hp_before = state["player"]["current_hp"]
        final = fire_and_finish(folder, process, state_path, 1, 0, 1)
        expected, _, error = assert_prediction_matches(folder)
        assert expected["stop_shield_granted"] == 3
        # enemy_strong attacks for 2. Player-ball defense no longer exists,
        # leaving one point of the freshly granted shield.
        assert final["player"]["current_hp"] == hp_before
        assert final["player"]["temporary_shield"] == 1

        wait_offer(folder, process, state_path, "player_stop_shield")
        moving = harness.command(
            folder, process, "fire_shot", direction_x=1, direction_z=0, power=1)
        assert moving["game_state"] == "balls_moving"
        assert moving["player"]["temporary_shield"] == 0
        return {"prediction_max_error": error, "shield_granted": 3,
                "shield_after_enemy_attack": 1, "cleared_on_next_shot": True}
    finally:
        close_game(process)
        log.close()


def main(label):
    report = {
        "chain_impact": run_chain(label),
        "refractive_pierce": run_refract(label),
        "stop_shield": run_shield(label),
        "passed": True,
    }
    destination = harness.ROOT / label / "variant_ball_verification.json"
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS stop shield, chain impact, refractive pierce and prediction parity")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_variant_balls.py LABEL")
    main(sys.argv[1])
