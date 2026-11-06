"""Capture small deterministic multi-turn Heavy resource-economy battles."""

import argparse
import copy
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess

import capture_fixed_timestep as harness
import capture_weight_inflation as weight


PROJECT = Path(os.environ.get("DX22_TEST_PROJECT", harness.PROJECT))
harness.PROJECT = PROJECT
weight.PROJECT = PROJECT
EXE = PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
CAPTURE_ROOT = PROJECT / "tools/runtime_tests/heavy_resource_economy/captures"
FIXED_SEED = 20260904


def merge_status(base: dict, override: dict) -> dict:
    result = copy.deepcopy(base)
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = merge_status(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def make_ball(source: dict, definition: dict, instance_id: int) -> dict:
    ball = copy.deepcopy(source)
    ball["definition_id"] = definition["id"]
    ball["category"] = definition["category"]
    ball["instance_id"] = instance_id
    ball["upgrade_level"] = 0
    status = merge_status(source["status"], definition["status"])
    ball["status"] = status
    upgrades = []
    previous = status
    for upgrade in definition.get("upgrades", []):
        previous = merge_status(previous, upgrade)
        upgrades.append(previous)
    while len(upgrades) < 2:
        upgrades.append(copy.deepcopy(previous))
    ball["upgrade_table"] = upgrades[:2]
    return ball


SCENARIOS = {
    "acquisition_heavy": {
        "deck": ["player_heavy", "player_heavy", "player_heavy"],
        "policy": "heavy_only",
    },
    "consumption_heavy": {
        "deck": [
            "player_heavy",
            "player_chain_impact",
            "player_collision_boost",
        ],
        "policy": "consumer_priority",
    },
    "mixed": {
        "deck": [
            "player_heavy",
            "player_heavy",
            "player_chain_impact",
            "player_collision_boost",
        ],
        "policy": "balanced",
    },
}


def make_preset(scenario: dict) -> dict:
    preset = weight.make_preset({
        "upgrade": 0,
        "hp": 1.5,
        "layout": "dense",
        "enemy_count": 8,
    })
    # Match the configured normal-run player HP instead of the low-HP editor
    # smoke fixture, so the resource loop can be observed through a full fight.
    preset["hp"] = 50
    preset["max_hp"] = 50
    definitions = {
        item["id"]: item
        for item in harness.read(PROJECT / "assets/data/player_status.json")["balls"]
    }
    # Build each serialized ball from the neutral debug-save schema. Using the
    # Heavy fixture as the base would leak heavyDistanceStacks into consumers.
    source = harness.read(weight.BASE_PRESET)["deck"][0]
    preset["deck"] = [
        make_ball(source, definitions[definition_id], index + 1)
        for index, definition_id in enumerate(scenario["deck"])
    ]
    return preset


def choose_offer(state: dict, policy: str, turn_index: int) -> dict:
    offers = state["offered_balls"]
    by_id = {offer["definition_id"]: offer for offer in offers}
    stacks = int(state["ball_synergies"]["heavy_stacks"])

    if policy == "heavy_only":
        preferred = ["player_heavy"]
    elif policy == "consumer_priority":
        if turn_index == 0:
            preferred = ["player_collision_boost", "player_chain_impact"]
        elif stacks >= 5:
            preferred = ["player_chain_impact", "player_collision_boost"]
        elif stacks >= 3:
            preferred = ["player_collision_boost", "player_chain_impact"]
        else:
            preferred = ["player_heavy"]
    elif policy == "balanced":
        if stacks < 5:
            preferred = ["player_heavy"]
        elif turn_index % 2 == 0:
            preferred = ["player_chain_impact", "player_collision_boost"]
        else:
            preferred = ["player_collision_boost", "player_chain_impact"]
    else:
        raise ValueError(policy)

    preferred.extend([
        "player_heavy",
        "player_chain_impact",
        "player_collision_boost",
    ])
    for definition_id in preferred:
        if definition_id in by_id:
            return by_id[definition_id]
    return offers[0]


def all_shots(run: dict) -> list[dict]:
    return [
        shot
        for stage in run.get("stages", [])
        for shot in stage.get("shots", [])
    ]


def capture_scenario(label: str, scenario_id: str, scenario: dict) -> dict:
    folder = CAPTURE_ROOT / label / scenario_id
    if folder.exists():
        raise RuntimeError(f"Use a fresh label; already exists: {folder}")

    harness.ROOT = CAPTURE_ROOT
    harness.BASELINE = PROJECT / "assets"
    prepared = harness.prepare(label, scenario_id, "heavy")
    player_path = prepared / "assets/data/player_status.json"
    player = harness.read(player_path)
    player["heavyStackDistancePerStack"] = 40.0
    player["heavyStackGainLimitPerTurn"] = 10
    harness.write(player_path, player)
    shutil.copy2(
        PROJECT / "assets/data/player_deck.json",
        prepared / "assets/data/player_deck.json",
    )
    harness.write(
        prepared / "saves/debug_battle_setup.json",
        make_preset(scenario),
    )

    with (prepared / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(EXE), "--debug-battle"],
            cwd=prepared,
            env=dict(os.environ, DX22_TEST_PREDICTION="1"),
            stdout=log,
            stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        try:
            state_path = prepared / "runtime/game_mcp/game_state.json"
            state = harness.wait_for(
                lambda: current
                if (current := harness.read(state_path)).get("debug_mode", {}).get("active")
                and "fire_shot" in current.get("available_actions", [])
                else None,
                process,
                60,
            )
            window = harness.wait_for(lambda: harness.window_for(process), process)
            ctypes.windll.user32.ShowWindow(window[0], 0)
            turn_records = []
            for turn_index in range(20):
                if state.get("debug_mode", {}).get("finished"):
                    break
                stacks_before = int(state["ball_synergies"]["heavy_stacks"])
                player_hp_before = int(state["player"]["current_hp"])
                offer = choose_offer(state, scenario["policy"], turn_index)
                state = harness.command(
                    prepared,
                    process,
                    "select_ball",
                    offer_index=offer["index"],
                )
                enemies = [enemy for enemy in state["enemies"] if not enemy["defeated"]]
                player_position = state["player"]["position"]
                target = min(
                    enemies,
                    key=lambda enemy: math.hypot(
                        enemy["position"]["x"] - player_position["x"],
                        enemy["position"]["z"] - player_position["z"],
                    ),
                )
                offset_x = target["position"]["x"] - player_position["x"]
                offset_z = target["position"]["z"] - player_position["z"]
                offset_length = math.hypot(offset_x, offset_z)
                direction_x, direction_z = (
                    (offset_x / offset_length, offset_z / offset_length)
                    if offset_length > 0.001 else (0.0, 1.0)
                )
                fired_state = harness.command(
                    prepared,
                    process,
                    "fire_shot",
                    direction_x=direction_x,
                    direction_z=direction_z,
                    power=8,
                )
                fired_sequence = int(fired_state.get("sequence", 0))
                state = harness.wait_for(
                    lambda: current
                    if int((current := harness.read(state_path)).get("sequence", 0))
                    > fired_sequence
                    and (
                        "fire_shot" in current.get("available_actions", [])
                        or current.get("debug_mode", {}).get("finished")
                    )
                    else None,
                    process,
                    90,
                )
                chain = state["latest_shot_physics_chain"]
                actual_gain = int(chain["heavy_stacks_generated"])
                stacks_after = int(state["ball_synergies"]["heavy_stacks"])
                consumed = max(0, stacks_before + actual_gain - stacks_after)
                fizzle = 0
                if offer["definition_id"] == "player_collision_boost" and stacks_before < 3:
                    fizzle = 1
                elif offer["definition_id"] == "player_chain_impact" and stacks_before == 0:
                    fizzle = 1

                expected_path = prepared / "runtime/shot_prediction_expected.json"
                actual_path = prepared / "runtime/shot_prediction_actual.json"
                expected = harness.read(expected_path)
                actual = harness.read(actual_path)
                turn_records.append({
                    "turn": turn_index + 1,
                    "ball_id": offer["definition_id"],
                    "direction_x": direction_x,
                    "direction_z": direction_z,
                    "power": 8,
                    "stacks_before": stacks_before,
                    "planned_gain": chain["heavy_stacks_uncapped_gain"],
                    "actual_gain": actual_gain,
                    "consumed": consumed,
                    "stacks_after": stacks_after,
                    "gain_cap_reached": actual_gain >= 10,
                    "storage_cap_reached": chain[
                        "heavy_stacks_discarded_by_storage_limit"
                    ] > 0,
                    "fizzle_count": fizzle,
                    "enemy_enemy_collisions": chain[
                        "effective_enemy_enemy_collision_count"
                    ],
                    "enemy_distance_xz": chain[
                        "total_enemy_distance_xz"
                    ],
                    "player_hp_before": player_hp_before,
                    "player_hp_after": int(state["player"]["current_hp"]),
                    "prediction_heavy_stacks": expected.get("heavy_stacks"),
                    "actual_verification_heavy_stacks": actual.get("heavy_stacks"),
                    "prediction_matches_actual": (
                        expected.get("heavy_stacks") == actual.get("heavy_stacks")
                        and expected.get("heavy_stacks_gained_this_turn")
                        == actual.get("heavy_stacks_gained_this_turn")
                    ),
                })
            turns = len(turn_records)
            player_start_hp = turn_records[0]["player_hp_before"] if turns else 0
            player_end_hp = int(state.get("player", {}).get(
                "current_hp", turn_records[-1]["player_hp_after"] if turns else 0
            ))
            summary = {
                "turn_count": turns,
                "total_gained": sum(turn["actual_gain"] for turn in turn_records),
                "total_consumed": sum(turn["consumed"] for turn in turn_records),
                "ending_balance": turn_records[-1]["stacks_after"] if turns else 0,
                "average_gain_per_turn": (
                    sum(turn["actual_gain"] for turn in turn_records) / turns
                    if turns else 0.0
                ),
                "average_consumption_per_turn": (
                    sum(turn["consumed"] for turn in turn_records) / turns
                    if turns else 0.0
                ),
                "gain_cap_reach_count": sum(
                    turn["gain_cap_reached"] for turn in turn_records
                ),
                "gain_cap_reach_rate": (
                    sum(turn["gain_cap_reached"] for turn in turn_records) / turns
                    if turns else 0.0
                ),
                "storage_cap_reach_count": sum(
                    turn["storage_cap_reached"] for turn in turn_records
                ),
                "storage_cap_reach_rate": (
                    sum(turn["storage_cap_reached"] for turn in turn_records) / turns
                    if turns else 0.0
                ),
                "fizzle_count": sum(turn["fizzle_count"] for turn in turn_records),
                "result": state.get("battle_result") or "turn_limit",
                "player_total_damage": max(0, player_start_hp - player_end_hp),
                "prediction_match_count": sum(
                    turn["prediction_matches_actual"] for turn in turn_records
                ),
            }
            result = {
                "scenario_id": scenario_id,
                "scenario": scenario,
                "seed": FIXED_SEED,
                "distance_per_stack": 40.0,
                "gain_limit_per_turn": 10,
                "storage_limit": "int_max_safety_limit",
                "turns": turn_records,
                "summary": summary,
                "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
                "capture_directory": str(prepared),
            }
            harness.write(prepared / "measurement.json", result)
            return result
        finally:
            if process.poll() is None:
                window = harness.window_for(process)
                if window:
                    ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
                process.wait(timeout=20)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument("--scenario", action="append", default=[])
    args = parser.parse_args()
    scenario_ids = args.scenario or list(SCENARIOS)
    unknown = set(scenario_ids) - set(SCENARIOS)
    if unknown:
        parser.error("unknown scenario: " + ", ".join(sorted(unknown)))
    results = [
        capture_scenario(args.label, scenario_id, SCENARIOS[scenario_id])
        for scenario_id in scenario_ids
    ]
    output = CAPTURE_ROOT / args.label / "measurements.json"
    harness.write(output, {"results": results})
    for result in results:
        summary = result["summary"]
        print(
            result["scenario_id"],
            "turns=", summary["turn_count"],
            "gain=", summary["total_gained"],
            "consume=", summary["total_consumed"],
            "end=", summary["ending_balance"],
            "cap_rate=", round(summary["gain_cap_reach_rate"], 3),
            "fizzle=", summary["fizzle_count"],
            "result=", summary["result"],
            flush=True,
        )
    print(output, flush=True)


if __name__ == "__main__":
    main()
