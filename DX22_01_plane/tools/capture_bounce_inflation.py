"""Capture deterministic Bounce-axis reflection, attack, and economy telemetry."""

import argparse
import copy
import ctypes
import hashlib
import math
import os
from pathlib import Path
import shutil
import subprocess

import capture_fixed_timestep as harness
import capture_heavy_resource_economy as economy
import capture_weight_inflation as weight


PROJECT = Path(os.environ.get("DX22_TEST_PROJECT", harness.PROJECT))
harness.PROJECT = PROJECT
economy.PROJECT = PROJECT
weight.PROJECT = PROJECT
EXE = PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
CAPTURE_ROOT = PROJECT / "tools/runtime_tests/bounce_inflation/captures"
FIXED_SEED = 20260904
POWER = 8
BANK_DIRECTION = (0.9238795325, 0.3826834324)

SINGLE_CASES = {
    "bounce_plus_0_dense": {"upgrade": 0, "layout": "dense"},
    "bounce_plus_1_dense": {"upgrade": 1, "layout": "dense"},
    "bounce_plus_2_dense": {"upgrade": 2, "layout": "dense"},
    "bounce_plus_0_line": {"upgrade": 0, "layout": "line"},
    "bounce_plus_0_random": {"upgrade": 0, "layout": "random"},
}

PINBALL_CASES = {
    "ricochet_plus_0_insufficient": {
        "upgrade": 0, "acquire": False,
    },
    "ricochet_plus_0_ready": {"upgrade": 0, "acquire": True},
    "ricochet_plus_1_ready": {"upgrade": 1, "acquire": True},
    "ricochet_plus_2_ready": {"upgrade": 2, "acquire": True},
}

ECONOMY_SCENARIOS = {
    "acquisition_bounce": {
        "deck": [
            ("player_cushion_charge", 0),
            ("player_cushion_charge", 1),
            ("player_cushion_charge", 2),
        ],
        "policy": "acquire",
    },
    "consumption_bounce": {
        "deck": [
            ("player_cushion_charge", 2),
            ("player_cushion_charge", 2),
            ("player_ricochet_finisher", 2),
        ],
        "policy": "consume",
    },
    "mixed_bounce": {
        "deck": [
            ("player_cushion_charge", 1),
            ("player_bounce", 2),
            ("player_ricochet_finisher", 2),
        ],
        "policy": "mixed",
    },
}


def definitions() -> dict:
    source = harness.read(PROJECT / "assets/data/player_status.json")
    return {item["id"]: item for item in source["balls"]}


def configured_ball(
    source: dict,
    definition_id: str,
    upgrade: int,
    instance_id: int,
) -> dict:
    ball = economy.make_ball(source, definitions()[definition_id], instance_id)
    ball["upgrade_level"] = upgrade
    if upgrade > 0:
        ball["status"] = copy.deepcopy(ball["upgrade_table"][upgrade - 1])
    return ball


def make_preset(layout: str, deck: list[tuple[str, int]]) -> dict:
    preset = weight.make_preset({
        "upgrade": 0,
        "hp": 1.5,
        "layout": layout,
        "enemy_count": 8,
    })
    preset["hp"] = 50
    preset["max_hp"] = 50
    source = harness.read(weight.BASE_PRESET)["deck"][0]
    preset["deck"] = [
        configured_ball(source, ball_id, upgrade, index + 1)
        for index, (ball_id, upgrade) in enumerate(deck)
    ]
    return preset


def prepare(label: str, case_id: str, preset: dict) -> Path:
    folder = CAPTURE_ROOT / label / case_id
    if folder.exists():
        raise RuntimeError(f"Use a fresh label; already exists: {folder}")
    harness.ROOT = CAPTURE_ROOT
    harness.BASELINE = PROJECT / "assets"
    prepared = harness.prepare(label, case_id, "bounce")
    shutil.copy2(
        PROJECT / "assets/data/player_deck.json",
        prepared / "assets/data/player_deck.json",
    )
    harness.write(prepared / "saves/debug_battle_setup.json", preset)
    return prepared


def start(prepared: Path):
    log = (prepared / "process.log").open("w", encoding="utf-8")
    process = subprocess.Popen(
        [str(EXE), "--debug-battle"],
        cwd=prepared,
        env=dict(os.environ, DX22_TEST_PREDICTION="1"),
        stdout=log,
        stderr=log,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
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
    return process, log, state_path, state


def stop(process, log) -> None:
    try:
        if process.poll() is None:
            window = harness.window_for(process)
            if window:
                ctypes.windll.user32.PostThreadMessageW(
                    window[1], 0x0012, 0, 0
                )
            process.wait(timeout=20)
    finally:
        log.close()


def direction_to_nearest(state: dict) -> tuple[float, float]:
    player = state["player"]["position"]
    enemies = [enemy for enemy in state["enemies"] if not enemy["defeated"]]
    target = min(
        enemies,
        key=lambda enemy: math.hypot(
            enemy["position"]["x"] - player["x"],
            enemy["position"]["z"] - player["z"],
        ),
    )
    x = target["position"]["x"] - player["x"]
    z = target["position"]["z"] - player["z"]
    length = math.hypot(x, z)
    return (x / length, z / length) if length > 0.001 else (0.0, 1.0)


def offer_for(state: dict, definition_id: str) -> dict:
    for offer in state["offered_balls"]:
        if offer["definition_id"] == definition_id:
            return offer
    raise AssertionError(f"{definition_id} is not offered")


def fire(
    prepared: Path,
    process,
    state_path: Path,
    state: dict,
    definition_id: str,
    direction: tuple[float, float],
) -> dict:
    offer = offer_for(state, definition_id)
    state = harness.command(
        prepared, process, "select_ball", offer_index=offer["index"]
    )
    fired = harness.command(
        prepared,
        process,
        "fire_shot",
        direction_x=direction[0],
        direction_z=direction[1],
        power=POWER,
    )
    sequence = int(fired.get("sequence", 0))
    return harness.wait_for(
        lambda: current
        if int((current := harness.read(state_path)).get("sequence", 0)) > sequence
        and current.get("latest_shot_bounce_axis")
        and (
            "fire_shot" in current.get("available_actions", [])
            or current.get("debug_mode", {}).get("finished")
        )
        else None,
        process,
        90,
    )


def record(turn: int, ball_id: str, direction: tuple[float, float], state: dict):
    data = copy.deepcopy(state["latest_shot_bounce_axis"])
    return {
        "turn": turn,
        "ball_id": ball_id,
        "direction_x": direction[0],
        "direction_z": direction[1],
        "power": POWER,
        "telemetry": data,
    }


def capture_single(label: str, case_id: str, case: dict) -> dict:
    preset = make_preset(case["layout"], [("player_bounce", case["upgrade"])])
    prepared = prepare(label, case_id, preset)
    process, log, state_path, state = start(prepared)
    try:
        direction = direction_to_nearest(state)
        state = fire(
            prepared, process, state_path, state, "player_bounce", direction
        )
        result = {
            "case_id": case_id,
            "case": case,
            "seed": FIXED_SEED,
            "direction_x": direction[0],
            "direction_z": direction[1],
            "power": POWER,
            "telemetry": state["latest_shot_bounce_axis"],
            "physics": state["latest_shot_physics_chain"],
            "battle_result": state.get("battle_result"),
            "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "capture_directory": str(prepared),
        }
        harness.write(prepared / "measurement.json", result)
        return result
    finally:
        stop(process, log)


def capture_pinball(label: str, case_id: str, case: dict) -> dict:
    deck = [
        ("player_cushion_charge", 2),
        ("player_cushion_charge", 2),
        ("player_ricochet_finisher", case["upgrade"]),
    ]
    prepared = prepare(label, case_id, make_preset("dense", deck))
    process, log, state_path, state = start(prepared)
    records = []
    try:
        if case["acquire"]:
            for turn in range(1, 6):
                if int(state["table"]["reflection_stacks"]) >= 6:
                    break
                state = fire(
                    prepared,
                    process,
                    state_path,
                    state,
                    "player_cushion_charge",
                    BANK_DIRECTION,
                )
                records.append(
                    record(
                        turn, "player_cushion_charge", BANK_DIRECTION, state
                    )
                )
                if state.get("debug_mode", {}).get("finished"):
                    break
        if not state.get("debug_mode", {}).get("finished"):
            direction = direction_to_nearest(state)
            state = fire(
                prepared,
                process,
                state_path,
                state,
                "player_ricochet_finisher",
                direction,
            )
            records.append(
                record(
                    len(records) + 1,
                    "player_ricochet_finisher",
                    direction,
                    state,
                )
            )
        finisher = next(
            (
                item
                for item in reversed(records)
                if item["ball_id"] == "player_ricochet_finisher"
            ),
            None,
        )
        result = {
            "case_id": case_id,
            "case": case,
            "seed": FIXED_SEED,
            "turns": records,
            "finisher_shot": finisher,
            "finisher_executed": finisher is not None,
            "battle_result": state.get("battle_result"),
            "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "capture_directory": str(prepared),
        }
        harness.write(prepared / "measurement.json", result)
        return result
    finally:
        stop(process, log)


def choose_economy_ball(state: dict, policy: str, turn: int) -> str:
    stacks = int(state["table"]["reflection_stacks"])
    offered = {offer["definition_id"] for offer in state["offered_balls"]}
    if policy == "acquire":
        preferred = ["player_cushion_charge"]
    elif policy == "consume":
        preferred = (
            ["player_ricochet_finisher", "player_cushion_charge"]
            if stacks >= 6
            else ["player_cushion_charge", "player_ricochet_finisher"]
        )
    elif policy == "mixed":
        if stacks >= 6:
            preferred = ["player_ricochet_finisher"]
        elif stacks > 0 and turn % 2 == 0:
            preferred = ["player_bounce", "player_cushion_charge"]
        else:
            preferred = ["player_cushion_charge", "player_bounce"]
    else:
        raise ValueError(policy)
    preferred.extend(
        ["player_cushion_charge", "player_bounce", "player_ricochet_finisher"]
    )
    return next(item for item in preferred if item in offered)


def capture_economy(label: str, case_id: str, scenario: dict) -> dict:
    prepared = prepare(label, case_id, make_preset("dense", scenario["deck"]))
    process, log, state_path, state = start(prepared)
    records = []
    player_hp = int(state["player"]["current_hp"])
    try:
        for turn in range(1, 21):
            if state.get("debug_mode", {}).get("finished"):
                break
            ball_id = choose_economy_ball(state, scenario["policy"], turn)
            direction = (
                BANK_DIRECTION
                if ball_id == "player_cushion_charge"
                else direction_to_nearest(state)
            )
            state = fire(
                prepared, process, state_path, state, ball_id, direction
            )
            records.append(record(turn, ball_id, direction, state))
        telemetry = [item["telemetry"] for item in records]
        summary = {
            "turn_count": len(records),
            "total_physical_reflections": sum(
                item["physical_reflection_count"] for item in telemetry
            ),
            "total_effective_reflections": sum(
                item["effective_reflection_count"] for item in telemetry
            ),
            "total_gained": sum(
                item["reflection_stacks_generated"] for item in telemetry
            ),
            "total_consumed": sum(
                item["reflection_stacks_consumed"] for item in telemetry
            ),
            "ending_balance": (
                telemetry[-1]["reflection_stacks_after"] if telemetry else 0
            ),
            "stack_max_reach_count": 0,
            "fizzle_count": sum(
                item["pinball_fizzle_count"] for item in telemetry
            ),
            "total_effective_attacks": sum(
                item["attacks"]["effective_attack_count"] for item in telemetry
            ),
            "total_applied_damage": sum(
                item["attacks"]["applied_hp_damage"] for item in telemetry
            ),
            "enemies_defeated": sum(
                item["enemies_defeated"] for item in telemetry
            ),
            "player_total_damage": max(
                0, player_hp - int(state["player"]["current_hp"])
            ),
            "result": (
                state.get("battle_result")
                if state.get("battle_result") not in (None, "", "none")
                else "turn_limit"
            ),
        }
        result = {
            "scenario_id": case_id,
            "scenario": scenario,
            "seed": FIXED_SEED,
            "turns": records,
            "summary": summary,
            "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "capture_directory": str(prepared),
        }
        harness.write(prepared / "measurement.json", result)
        return result
    finally:
        stop(process, log)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", required=True)
    parser.add_argument(
        "--group",
        choices=("single", "pinball", "economy", "all"),
        default="all",
    )
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args()
    results = []
    if args.group in ("single", "all"):
        selected = args.case or list(SINGLE_CASES)
        results.extend(
            capture_single(args.label, case_id, SINGLE_CASES[case_id])
            for case_id in selected
            if case_id in SINGLE_CASES
        )
    if args.group in ("pinball", "all"):
        selected = args.case or list(PINBALL_CASES)
        results.extend(
            capture_pinball(args.label, case_id, PINBALL_CASES[case_id])
            for case_id in selected
            if case_id in PINBALL_CASES
        )
    if args.group in ("economy", "all"):
        selected = args.case or list(ECONOMY_SCENARIOS)
        results.extend(
            capture_economy(args.label, case_id, ECONOMY_SCENARIOS[case_id])
            for case_id in selected
            if case_id in ECONOMY_SCENARIOS
        )
    known = set(SINGLE_CASES) | set(PINBALL_CASES) | set(ECONOMY_SCENARIOS)
    unknown = set(args.case) - known
    if unknown:
        parser.error("unknown case: " + ", ".join(sorted(unknown)))
    output = CAPTURE_ROOT / args.label / "measurements.json"
    harness.write(output, {"results": results})
    for result in results:
        telemetry = result.get("telemetry") or (
            result.get("finisher_shot") or {}
        ).get("telemetry")
        if telemetry:
            print(
                result["case_id"],
                "physical=", telemetry["physical_reflection_count"],
                "effective=", telemetry["effective_reflection_count"],
                "attacks=", telemetry["attacks"]["effective_attack_count"],
                "damage=", telemetry["attacks"]["applied_hp_damage"],
                flush=True,
            )
        else:
            print(result["scenario_id"], result["summary"], flush=True)
    print(output, flush=True)


if __name__ == "__main__":
    main()
