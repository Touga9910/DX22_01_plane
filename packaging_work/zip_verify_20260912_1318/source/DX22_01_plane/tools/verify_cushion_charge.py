"""Verify cushion-charge state and real/predicted behavior in an isolated game."""

from __future__ import annotations

import ctypes
import json
import os
import shutil
import subprocess
import sys

import capture_fixed_timestep as harness


def main(label: str) -> None:
    folder = harness.prepare(label, "cushion_charge", "standard")
    data = folder / "assets/data"
    shutil.copy2(harness.PROJECT / "assets/data/player_status.json", data / "player_status.json")
    harness.write(data / "player_deck.json", {
        "deck": ["player_cushion_charge", "player_standard", "player_standard"]
    })
    exe = harness.PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
    env = dict(os.environ, DX22_TEST_PREDICTION="1")
    with (folder / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(exe)], cwd=folder, env=env, stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            state_path = folder / "runtime/game_mcp/game_state.json"
            harness.wait_for(lambda: harness.read(state_path).get("scene") == "title", process)
            window = harness.wait_for(lambda: harness.window_for(process), process)
            ctypes.windll.user32.ShowWindow(window[0], 0)
            harness.command(folder, process, "start_new_run", run_seed=20260911)
            harness.command(
                folder, process, "set_next_stage_layout", layout_id="cushion_charge",
                stage_type="normal", difficulty=1, par=4,
                enemies=[{"enemy_id": "enemy_tank", "x": -55, "z": -20}])
            state = harness.read(state_path)
            route = next(item for item in state["route_options"] if item["destination"] == "battle")
            harness.command(folder, process, "choose_destination", route_index=route["route_index"])
            state = harness.wait_for(
                lambda: value if "select_ball" in (value := harness.read(state_path)).get("available_actions", []) else None,
                process)
            cushion_offer = next(
                ball for ball in state["offered_balls"]
                if ball["definition_id"] == "player_cushion_charge")
            assert cushion_offer["category"] == "bounce"
            assert abs(cushion_offer["status"]["cushionChargeSpeedMultiplier"] - 1.1) < 0.0001
            harness.command(folder, process, "select_ball", offer_index=cushion_offer["index"])
            harness.command(folder, process, "fire_shot", direction_x=1.0, direction_z=0.0, power=4.0)
            final = harness.wait_for(
                lambda: value if (value := harness.read(state_path)).get("game_state") in
                ("aiming_direction", "clear_reward", "game_over") else None,
                process, 60)
            assert len(final["table"]["cushions"]) == 12
            assert any(region["charged"] for region in final["table"]["cushions"])
            assert all(
                not region["usable_this_shot"]
                for region in final["table"]["cushions"] if region["charged"])
            expected = harness.read(folder / "runtime/shot_prediction_expected.json")
            actual = harness.read(folder / "runtime/shot_prediction_actual.json")
            assert expected["world_unchanged"] and expected["cushions"] == actual["cushions"]

            # Aim the following standard ball at one charged rail region.
            state = harness.wait_for(
                lambda: value if "select_ball" in (value := harness.read(state_path)).get(
                    "available_actions", []) else None,
                process)
            standard_offer = next(
                ball for ball in state["offered_balls"]
                if ball["definition_id"] == "player_standard")
            charged = [region for region in state["table"]["cushions"] if region["charged"]]
            region = next((item for item in charged if item["side"] == "right"), charged[0])
            part = region["part"]
            if region["side"] == "top":
                target_x, target_z = -72.0 + (part + 0.5) * 36.0, 36.0
            elif region["side"] == "bottom":
                target_x, target_z = -72.0 + (part + 0.5) * 36.0, -36.0
            elif region["side"] == "left":
                target_x, target_z = -72.0, -36.0 + (part + 0.5) * 36.0
            else:
                target_x, target_z = 72.0, -36.0 + (part + 0.5) * 36.0
            player_position = state["player"]["position"]
            player_x = float(player_position["x"])
            player_z = float(player_position["z"])
            harness.command(folder, process, "select_ball", offer_index=standard_offer["index"])
            harness.command(
                folder, process, "fire_shot",
                direction_x=target_x - player_x,
                direction_z=target_z - player_z,
                power=4.0)
            second_final = harness.wait_for(
                lambda: value if (value := harness.read(state_path)).get("game_state") in
                ("aiming_direction", "clear_reward", "game_over") else None,
                process, 60)
            second_expected = harness.read(folder / "runtime/shot_prediction_expected.json")
            second_actual = harness.read(folder / "runtime/shot_prediction_actual.json")
            assert second_expected["world_unchanged"]
            assert second_expected["cushions"] == second_actual["cushions"]
            assert second_expected["cushion_boost_consumed"]
            assert second_actual["cushion_boost_consumed"]
            assert not second_final["table"]["cushion_boost_consumed_this_shot"]
            assert not any(item["charged"] for item in second_final["table"]["cushions"])
            (folder / "verified.json").write_text(json.dumps({
                "passed": True,
                "category": cushion_offer["category"],
                "charged_regions": [
                    region["region"] for region in final["table"]["cushions"] if region["charged"]
                ],
                "prediction_matches_actual": True,
                "next_shot_boost_consumed": True,
                "charges_expired_after_next_shot": True,
            }, indent=2), encoding="utf-8")
        finally:
            if process.poll() is None:
                window = harness.window_for(process)
                if window:
                    ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
                process.wait(timeout=20)
    print("PASS cushion charge next-shot use, one boost, expiry and prediction parity")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_cushion_charge.py LABEL")
    main(sys.argv[1])
