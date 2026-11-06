"""Capture deterministic Pierce attack, minion, and resource telemetry."""

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
CAPTURE_ROOT = PROJECT / "tools/runtime_tests/pierce_inflation/captures"
FIXED_SEED = 20260904
FIXED_DIRECTION = (-0.9916810938126976, -0.1287191057087161)
POWER = 8


SINGLE_CASES = {
    "pierce_plus_0_dense": {"upgrade": 0, "hp": 1.5, "layout": "dense"},
    "pierce_plus_1_dense": {"upgrade": 1, "hp": 1.5, "layout": "dense"},
    "pierce_plus_2_dense": {"upgrade": 2, "hp": 1.5, "layout": "dense"},
    "pierce_plus_0_line": {
        "upgrade": 0, "hp": 1.5, "layout": "line", "aim": "nearest"
    },
    "pierce_plus_0_random": {
        "upgrade": 0, "hp": 1.5, "layout": "random", "aim": "nearest"
    },
    "pierce_plus_0_hp_1_0": {"upgrade": 0, "hp": 1.0, "layout": "dense"},
    "pierce_plus_0_hp_2_0": {"upgrade": 0, "hp": 2.0, "layout": "dense"},
}

STAGE_CASES = {
    "trace_driver_stage_3": {
        "consumer": "player_trace_driver", "upgrade": 0, "target": 3
    },
    "trace_driver_stage_6": {
        "consumer": "player_trace_driver", "upgrade": 1, "target": 6
    },
    "trace_driver_stage_9": {
        "consumer": "player_trace_driver", "upgrade": 2, "target": 9
    },
    "minion_finisher_overcharge": {
        "consumer": "player_minion_finisher", "upgrade": 0, "target": 15
    },
}

ECONOMY_SCENARIOS = {
    "acquisition_pierce": {
        "deck": [("player_pierce", 0)] * 3,
        "policy": "acquire",
    },
    "consumption_pierce": {
        "deck": [
            ("player_pierce", 0),
            ("player_trace_driver", 2),
            ("player_minion_finisher", 0),
        ],
        "policy": "consume",
    },
    "mixed": {
        "deck": [
            ("player_pierce", 0),
            ("player_pierce", 1),
            ("player_trace_driver", 1),
        ],
        "policy": "mixed",
    },
}


def definitions() -> dict:
    player = harness.read(PROJECT / "assets/data/player_status.json")
    return {item["id"]: item for item in player["balls"]}


def configured_ball(
    source: dict,
    definition_id: str,
    upgrade: int,
    instance_id: int,
) -> dict:
    ball = economy.make_ball(
        source,
        definitions()[definition_id],
        instance_id,
    )
    ball["upgrade_level"] = upgrade
    if upgrade > 0:
        ball["status"] = copy.deepcopy(ball["upgrade_table"][upgrade - 1])
    return ball


def make_preset(case: dict, deck: list[tuple[str, int]]) -> dict:
    preset = weight.make_preset({
        "upgrade": 0,
        "hp": case.get("hp", 1.5),
        "layout": case.get("layout", "dense"),
        "enemy_count": 8,
    })
    preset["hp"] = 50
    preset["max_hp"] = 50
    source = harness.read(weight.BASE_PRESET)["deck"][0]
    preset["deck"] = [
        configured_ball(source, definition_id, upgrade, index + 1)
        for index, (definition_id, upgrade) in enumerate(deck)
    ]
    return preset


def prepare(label: str, case_id: str, preset: dict) -> Path:
    folder = CAPTURE_ROOT / label / case_id
    if folder.exists():
        raise RuntimeError(f"Use a fresh label; already exists: {folder}")
    harness.ROOT = CAPTURE_ROOT
    harness.BASELINE = PROJECT / "assets"
    prepared = harness.prepare(label, case_id, "pierce")
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
                ctypes.windll.user32.PostThreadMessageW(window[1], 0x0012, 0, 0)
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
    offset_x = target["position"]["x"] - player["x"]
    offset_z = target["position"]["z"] - player["z"]
    length = math.hypot(offset_x, offset_z)
    return (
        (offset_x / length, offset_z / length)
        if length > 0.001
        else (0.0, 1.0)
    )


def offer_for(state: dict, definition_id: str) -> dict:
    for offer in state["offered_balls"]:
        if offer["definition_id"] == definition_id:
            return offer
    raise AssertionError(
        f"{definition_id} is not offered: "
        + ", ".join(offer["definition_id"] for offer in state["offered_balls"])
    )


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
        prepared,
        process,
        "select_ball",
        offer_index=offer["index"],
    )
    fired = harness.command(
        prepared,
        process,
        "fire_shot",
        direction_x=direction[0],
        direction_z=direction[1],
        power=POWER,
    )
    fired_sequence = int(fired.get("sequence", 0))
    return harness.wait_for(
        lambda: current
        if int((current := harness.read(state_path)).get("sequence", 0))
        > fired_sequence
        and current.get("latest_shot_pierce_attack")
        and (
            "fire_shot" in current.get("available_actions", [])
            or current.get("debug_mode", {}).get("finished")
        )
        else None,
        process,
        90,
    )


def shot_record(turn: int, ball_id: str, direction: tuple[float, float], state: dict) -> dict:
    telemetry = state["latest_shot_pierce_attack"]
    return {
        "turn": turn,
        "ball_id": ball_id,
        "direction_x": direction[0],
        "direction_z": direction[1],
        "power": POWER,
        "stacks_before": telemetry["pierce_stacks_before"],
        "uncapped_gain": telemetry["pierce_stacks_uncapped_gain"],
        "actual_gain": telemetry["pierce_stacks_generated"],
        "consumed": telemetry["pierce_stacks_consumed"],
        "stacks_after": telemetry["pierce_stacks_after"],
        "stack_max_reached": telemetry["pierce_stack_max_reached"],
        "fizzle_count": telemetry["pierce_minion_fizzle_count"],
        "shot_duration_ms": telemetry["shot_duration_ms"],
        "attack": telemetry,
    }


def capture_single(label: str, case_id: str, case: dict) -> dict:
    preset = make_preset(case, [("player_pierce", case["upgrade"])])
    prepared = prepare(label, case_id, preset)
    process, log, state_path, state = start(prepared)
    try:
        direction = (
            direction_to_nearest(state)
            if case.get("aim") == "nearest"
            else FIXED_DIRECTION
        )
        player_hp_before = state["player"]["current_hp"]
        state = fire(
            prepared,
            process,
            state_path,
            state,
            "player_pierce",
            direction,
        )
        result = {
            "case_id": case_id,
            "case": case,
            "seed": FIXED_SEED,
            "direction_x": direction[0],
            "direction_z": direction[1],
            "power": POWER,
            "player_hp_before": player_hp_before,
            "player_hp_after": state["player"]["current_hp"],
            "battle_result": state.get("battle_result"),
            "telemetry": state["latest_shot_pierce_attack"],
            "physics": state["latest_shot_physics_chain"],
            "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "capture_directory": str(prepared),
        }
        harness.write(prepared / "measurement.json", result)
        return result
    finally:
        stop(process, log)


def capture_stage(label: str, case_id: str, case: dict) -> dict:
    deck = [
        ("player_pierce", 0),
        ("player_pierce", 0),
        (case["consumer"], case["upgrade"]),
    ]
    preset = make_preset({"hp": 2.0, "layout": "dense"}, deck)
    prepared = prepare(label, case_id, preset)
    process, log, state_path, state = start(prepared)
    records = []
    try:
        for turn in range(1, 16):
            if state.get("debug_mode", {}).get("finished"):
                break
            stacks = int(state["ball_synergies"]["pierce_stacks"])
            use_consumer = stacks >= case["target"]
            ball_id = case["consumer"] if use_consumer else "player_pierce"
            direction = direction_to_nearest(state)
            state = fire(
                prepared, process, state_path, state, ball_id, direction
            )
            records.append(shot_record(turn, ball_id, direction, state))
            if use_consumer:
                break
        consumer_shot = next(
            (item for item in records if item["ball_id"] == case["consumer"]),
            None,
        )
        result = {
            "case_id": case_id,
            "case": case,
            "seed": FIXED_SEED,
            "turns": records,
            "consumer_shot": consumer_shot,
            "consumer_executed": consumer_shot is not None,
            "battle_result": state.get("battle_result"),
            "executable_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
            "capture_directory": str(prepared),
        }
        harness.write(prepared / "measurement.json", result)
        return result
    finally:
        stop(process, log)


def choose_economy_ball(state: dict, policy: str, turn: int) -> str:
    offered = {offer["definition_id"] for offer in state["offered_balls"]}
    stacks = int(state["ball_synergies"]["pierce_stacks"])
    if policy == "acquire":
        preferred = ["player_pierce"]
    elif policy == "consume":
        if turn == 1:
            preferred = ["player_trace_driver", "player_minion_finisher"]
        elif stacks >= 9:
            preferred = ["player_trace_driver", "player_minion_finisher"]
        else:
            preferred = ["player_pierce"]
    elif policy == "mixed":
        preferred = ["player_trace_driver"] if stacks >= 6 else ["player_pierce"]
    else:
        raise ValueError(policy)
    preferred.extend(["player_pierce", "player_trace_driver", "player_minion_finisher"])
    return next(item for item in preferred if item in offered)


def capture_economy(label: str, scenario_id: str, scenario: dict) -> dict:
    preset = make_preset({"hp": 1.5, "layout": "dense"}, scenario["deck"])
    prepared = prepare(label, scenario_id, preset)
    process, log, state_path, state = start(prepared)
    records = []
    player_start_hp = state["player"]["current_hp"]
    try:
        for turn in range(1, 21):
            if state.get("debug_mode", {}).get("finished"):
                break
            ball_id = choose_economy_ball(state, scenario["policy"], turn)
            direction = direction_to_nearest(state)
            state = fire(
                prepared, process, state_path, state, ball_id, direction
            )
            records.append(shot_record(turn, ball_id, direction, state))
        run_paths = sorted((prepared / "logs/debug_battle").glob("run_*.json"))
        if not run_paths:
            raise AssertionError("The debug balance log was not written.")
        completed_shots = [
            shot
            for stage in harness.read(run_paths[-1]).get("stages", [])
            for shot in stage.get("shots", [])
        ]
        summary = {
            "turn_count": len(records),
            "total_gained": sum(item["actual_gain"] for item in records),
            "total_consumed": sum(item["consumed"] for item in records),
            "ending_balance": records[-1]["stacks_after"] if records else 0,
            "stack_max_reach_count": sum(item["stack_max_reached"] for item in records),
            "fizzle_count": sum(item["fizzle_count"] for item in records),
            "minions_generated": sum(
                item["attack"]["minions_generated"] for item in records
            ),
            "minion_effective_attacks": sum(
                item["attack"]["sources"]["minion"]["effective_attack_count"]
                for item in records
            ),
            "total_effective_attacks": sum(
                item["attack"]["total"]["effective_attack_count"]
                for item in records
            ),
            "enemies_defeated": sum(
                int(shot.get("enemies_defeated", 0)) for shot in completed_shots
            ),
            "result": (
                state.get("battle_result")
                if state.get("battle_result") not in (None, "", "none")
                else "turn_limit"
            ),
            "player_total_damage": max(
                0, player_start_hp - int(state["player"]["current_hp"])
            ),
        }
        result = {
            "scenario_id": scenario_id,
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
        choices=("single", "stage", "economy", "all"),
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
    if args.group in ("stage", "all"):
        selected = args.case or list(STAGE_CASES)
        results.extend(
            capture_stage(args.label, case_id, STAGE_CASES[case_id])
            for case_id in selected
            if case_id in STAGE_CASES
        )
    if args.group in ("economy", "all"):
        selected = args.case or list(ECONOMY_SCENARIOS)
        results.extend(
            capture_economy(args.label, case_id, ECONOMY_SCENARIOS[case_id])
            for case_id in selected
            if case_id in ECONOMY_SCENARIOS
        )
    known = set(SINGLE_CASES) | set(STAGE_CASES) | set(ECONOMY_SCENARIOS)
    unknown = set(args.case) - known
    if unknown:
        parser.error("unknown case: " + ", ".join(sorted(unknown)))
    output = CAPTURE_ROOT / args.label / "measurements.json"
    harness.write(output, {"results": results})
    for result in results:
        telemetry = result.get("telemetry") or (
            result.get("consumer_shot") or {}
        ).get("attack")
        summary = result.get("summary")
        if telemetry:
            print(
                result.get("case_id"),
                "effective=", telemetry["total"]["effective_attack_count"],
                "damage=", telemetry["total"]["applied_hp_damage"],
                "targets=", telemetry["unique_effective_target_count"],
                "minions=", telemetry["minions_generated"],
                flush=True,
            )
        elif summary:
            print(result["scenario_id"], summary, flush=True)
        else:
            print(result.get("case_id"), "consumer_not_executed", flush=True)
    print(output, flush=True)


if __name__ == "__main__":
    main()
