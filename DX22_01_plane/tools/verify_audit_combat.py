"""Replay combat audit regressions with a Debug executable in private folders."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import capture_fixed_timestep as harness
from verify_variant_balls import assert_prediction_matches


CASES = {
    "lethal_chain": ("player_chain_impact", [
        ("enemy_strong", 20, 0), ("enemy_normal", 20, 9),
        ("enemy_normal", -45, 20)], 6),
    "corpse_collision": ("player_standard", [
        ("enemy_strong", 14, 0), ("enemy_normal", 28, 0),
        ("enemy_normal", -45, 20)], 3),
    "high_friction": ("player_standard", [("enemy_normal", 14, 0)], 2),
    "protection": ("player_standard", [
        ("enemy_normal", 20, 0), ("enemy_protection_resonator", 20, 9),
        ("enemy_normal", -45, 20)], 3),
}


def run_case(exe: Path, label: str, name: str) -> dict:
    ball, placements, power = CASES[name]
    # prepare() expects BASELINE/data; use current assets so a clean checkout
    # does not need an archived, ignored baseline fixture directory.
    harness.BASELINE = harness.PROJECT / "assets"
    folder = harness.prepare(label, name, "standard")
    data = folder / "assets/data"
    for config_name in ("balance_validation", "balance_autoplay", "dynamic_balance"):
        config = harness.read(data / f"{config_name}.json")
        config["enabled"] = False
        harness.write(data / f"{config_name}.json", config)
    harness.write(data / "player_deck.json", {"deck": [ball] * 3})
    harness.write(data / "game_mcp_bridge.json", {
        "enabled": True, "allow_write_actions": True,
        "bridge_directory": "runtime/game_mcp", "state_publish_interval_frames": 1,
        "command_poll_interval_frames": 1})
    enemies = harness.read(data / "enemy_data.json")
    for enemy in enemies["enemies"]:
        if enemy["id"] == "enemy_normal":
            enemy["status"]["maxHp"] = 20
            if name == "high_friction":
                enemy["status"].update(friction=1.0, mass=2.0)
        if enemy["id"] == "enemy_strong":
            enemy["status"].update(maxHp=1, attack=7, defense=0)
    harness.write(data / "enemy_data.json", enemies)
    state_path = folder / "runtime/game_mcp/game_state.json"
    with (folder / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(exe)], cwd=folder,
            env=dict(os.environ, DX22_TEST_PREDICTION="1"), stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            harness.wait_for(lambda: harness.read(state_path).get("scene") == "title", process)
            harness.command(folder, process, "start_new_run", run_seed=20261125)
            harness.command(folder, process, "set_next_stage_layout", layout_id=name,
                stage_type="normal", difficulty=1, par=6,
                enemies=[{"enemy_id": kind, "x": x, "z": z} for kind, x, z in placements])
            state = harness.read(state_path)
            route = next(item for item in state["route_options"] if item["destination"] == "battle")
            harness.command(folder, process, "choose_destination", route_index=route["route_index"])
            initial = harness.wait_for(lambda: value if "fire_shot" in
                (value := harness.read(state_path)).get("available_actions", []) else None, process)
            harness.write(folder / "initial.json", initial)
            harness.command(folder, process, "fire_shot", direction_x=1, direction_z=0, power=power)
            final = harness.wait_for(lambda: value if (value := harness.read(state_path)).get(
                "game_state") in ("aiming_direction", "clear_reward", "game_over") else None,
                process, 60)
            harness.write(folder / "final.json", final)
            if not (folder / "runtime/shot_prediction_expected.json").exists():
                raise RuntimeError("Prediction diagnostics require a Debug executable.")
            expected, _, error = assert_prediction_matches(folder)
            if name == "lethal_chain":
                assert expected["chain_impact_hits"] >= 1
                assert any(b["enemy_id"] == "enemy_strong" and b["defeated"] for b in expected["balls"])
            result = {"case": name, "matched": True, "max_error": error,
                "ticks": expected["ticks"], "chain_hits": expected["chain_impact_hits"],
                "game_state": final["game_state"]}
            print(json.dumps(result), flush=True)
            return result
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--case", choices=CASES)
    args = parser.parse_args()
    if re.fullmatch(r"[A-Za-z0-9_-]+", args.label) is None:
        parser.error("--label must contain only letters, digits, underscores, or hyphens")
    results = [run_case(args.exe.resolve(), args.label, name)
        for name in ([args.case] if args.case else CASES)]
    harness.write(harness.ROOT / args.label / "summary.json", {"results": results})


if __name__ == "__main__":
    main()
