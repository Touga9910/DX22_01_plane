"""Capture reproducible one-shot Heavy physics-chain measurements."""

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


PROJECT = Path(os.environ.get("DX22_TEST_PROJECT", harness.PROJECT))
harness.PROJECT = PROJECT
EXE = PROJECT.parent / "x64/Debug/DX22_01_plane.exe"
CAPTURE_ROOT = PROJECT / "tools/runtime_tests/weight_inflation/captures"
BASE_PRESET = PROJECT / "tools/runtime_tests/debug_mode/setup_win.json"
FIXED_SEED = 20260904


def heavy_ball(source: dict, upgrade_level: int) -> dict:
    ball = copy.deepcopy(source)
    ball["definition_id"] = "player_heavy"
    ball["category"] = "heavy"
    ball["upgrade_level"] = upgrade_level

    def make_status(level: int) -> dict:
        status = copy.deepcopy(source["status"])
        status["attack"] = (2, 3, 4)[level]
        status["mass"] = (4.0, 5.0, 6.0)[level]
        status["knockbackTransfer"] = (1.0, 1.15, 1.3)[level]
        status["abilities"]["heavyDistanceStacks"] = True
        return status

    ball["status"] = make_status(upgrade_level)
    ball["upgrade_table"] = [make_status(1), make_status(2)]
    return ball


def make_preset(case: dict) -> dict:
    preset = harness.read(BASE_PRESET)
    preset["seed"] = FIXED_SEED
    preset["player_x"] = 0.0
    preset["player_z"] = 0.0
    preset["deck"] = [heavy_ball(preset["deck"][0], case["upgrade"])]
    preset["relics"] = [False] * len(preset["relics"])
    preset["selected_ascension"] = 0
    preset["highest_unlocked_ascension"] = 0
    if case["layout"] == "stage_normal_002":
        template = copy.deepcopy(preset["enemies"][0])
        template["hp"] = template["max_hp"]
        preset["enemies"] = []
        for x, z in ((-42.0, -14.0), (0.0, 20.0), (42.0, -14.0)):
            enemy = copy.deepcopy(template)
            enemy["x"] = x
            enemy["z"] = z
            preset["enemies"].append(enemy)
        preset["enemy_validation"] = {
            "enabled": False,
            "hp_multiplier": 1.0,
            # Disabled validation still validates its dormant count field.
            "enemy_count": 6,
            "layout_pattern": "dense",
        }
    else:
        preset["enemy_validation"] = {
            "enabled": True,
            "hp_multiplier": case["hp"],
            "enemy_count": case.get("enemy_count", 8),
            "layout_pattern": case["layout"],
        }
    return preset


def normalized_direction(enemy: dict) -> tuple[float, float]:
    position = enemy["position"]
    length = math.hypot(position["x"], position["z"])
    return position["x"] / length, position["z"] / length


def shot_direction(enemies: list[dict], strategy: str) -> tuple[float, float]:
    if strategy == "nearest":
        target = min(
            enemies,
            key=lambda enemy: math.hypot(
                enemy["position"]["x"], enemy["position"]["z"]),
        )
        return normalized_direction(target)

    center_x = sum(enemy["position"]["x"] for enemy in enemies)
    center_z = sum(enemy["position"]["z"] for enemy in enemies)
    center_length = math.hypot(center_x, center_z)
    if center_length <= 0.0001:
        nearest_x, nearest_z = shot_direction(enemies, "nearest")
        center_x, center_z = nearest_x, nearest_z
        center_length = 1.0
    center_x /= center_length
    center_z /= center_length
    if strategy == "cluster_center":
        return center_x, center_z
    if strategy == "sparse_opposite":
        return -center_x, -center_z
    raise ValueError(f"Unknown direction strategy: {strategy}")


def extract_latest_shot(run: dict) -> dict:
    shots = [
        shot
        for stage in run.get("stages", [])
        for shot in stage.get("shots", [])
    ]
    if not shots:
        raise AssertionError("The debug balance log contains no completed shot.")
    return shots[-1]


def capture_case(label: str, case: dict) -> dict:
    folder = CAPTURE_ROOT / label / case["id"]
    if folder.exists():
        raise RuntimeError(f"Use a fresh capture label; already exists: {folder}")

    harness.ROOT = CAPTURE_ROOT
    harness.BASELINE = PROJECT / "assets"
    prepared = harness.prepare(label, case["id"], "heavy")
    player_status_path = prepared / "assets/data/player_status.json"
    player_status = harness.read(player_status_path)
    player_status["heavyStackDistancePerStack"] = case.get(
        "distance_per_stack", 40.0
    )
    player_status["heavyStackGainLimitPerTurn"] = case.get("gain_limit", 10)
    harness.write(player_status_path, player_status)
    shutil.copy2(
        PROJECT / "assets/data/player_deck.json",
        prepared / "assets/data/player_deck.json",
    )
    harness.write(
        prepared / "saves/debug_battle_setup.json",
        make_preset(case),
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
            initial = harness.wait_for(
                lambda: state
                if (state := harness.read(state_path)).get("debug_mode", {}).get("active")
                and "fire_shot" in state.get("available_actions", [])
                else None,
                process,
                60,
            )
            window = harness.wait_for(lambda: harness.window_for(process), process)
            ctypes.windll.user32.ShowWindow(window[0], 0)

            enemies = initial["enemies"]
            direction_x, direction_z = shot_direction(
                enemies,
                case.get("direction", "nearest"),
            )
            harness.command(
                prepared,
                process,
                "fire_shot",
                direction_x=direction_x,
                direction_z=direction_z,
                power=case.get("power", 8),
            )
            final = harness.wait_for(
                lambda: state
                if (state := harness.read(state_path)).get(
                    "latest_shot_physics_chain"
                )
                and (
                    "fire_shot" in state.get("available_actions", [])
                    or state.get("debug_mode", {}).get("finished")
                )
                else None,
                process,
                90,
            )

            logs = list((prepared / "logs/debug_battle").glob("run_*.json"))
            if len(logs) != 1:
                raise AssertionError(f"Expected one debug log, got {len(logs)}")
            shot = extract_latest_shot(harness.read(logs[0]))
            chain = shot["physics_chain"]
            result = {
                "case": case,
                "seed": FIXED_SEED,
                "shot": {
                    "ball_id": shot["ball_id"],
                    "upgrade_level": shot["upgrade_level"],
                    "direction_x": direction_x,
                    "direction_z": direction_z,
                    "power": case.get("power", 8),
                    "initial_enemy_count": shot["enemies_alive_before"],
                    "enemies_defeated": shot["enemies_defeated"],
                    "final_enemy_count": shot["enemies_alive_after"],
                    "duration_ms": shot["duration_ms"],
                    "heavy_stacks_uncapped_gain": shot[
                        "heavy_stacks_uncapped_gain"
                    ],
                    "heavy_stacks_generated": shot["heavy_stacks_generated"],
                    "heavy_stack_gain_limit_per_turn": shot[
                        "heavy_stack_gain_limit_per_turn"
                    ],
                    "heavy_stack_distance_per_stack": shot[
                        "heavy_stack_distance_per_stack"
                    ],
                    "heavy_stacks_discarded_by_turn_limit": shot[
                        "heavy_stacks_discarded_by_turn_limit"
                    ],
                    "heavy_stacks_discarded_by_storage_limit": shot[
                        "heavy_stacks_discarded_by_storage_limit"
                    ],
                    "heavy_stacks_consumed": shot["heavy_stacks_consumed"],
                    "physics_chain": chain,
                },
                "initial_enemies": [
                    {
                        "target_id": enemy["target_id"],
                        "enemy_id": enemy["enemy_id"],
                        "hp": enemy["hp"],
                        "max_hp": enemy["max_hp"],
                        "position": enemy["position"],
                    }
                    for enemy in enemies
                ],
                "final_state_chain": final["latest_shot_physics_chain"],
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
    parser.add_argument(
        "--case",
        action="append",
        default=[],
        help="Run only the named case. Repeat to select multiple cases.",
    )
    args = parser.parse_args()
    cases = [
        {"id": "comparison_distance_5", "upgrade": 0, "hp": 1.5,
         "layout": "dense", "distance_per_stack": 5.0},
        {"id": "comparison_distance_40", "upgrade": 0, "hp": 1.5,
         "layout": "dense", "distance_per_stack": 40.0},
        {"id": "standard_repeat_1", "upgrade": 0, "hp": 1.5, "layout": "dense"},
        {"id": "standard_repeat_2", "upgrade": 0, "hp": 1.5, "layout": "dense"},
        {"id": "heavy_plus_1", "upgrade": 1, "hp": 1.5, "layout": "dense"},
        {"id": "heavy_plus_2", "upgrade": 2, "hp": 1.5, "layout": "dense"},
        {"id": "hp_1_0", "upgrade": 0, "hp": 1.0, "layout": "dense"},
        {"id": "hp_2_0", "upgrade": 0, "hp": 2.0, "layout": "dense"},
        {"id": "layout_line", "upgrade": 0, "hp": 1.5, "layout": "line"},
        {"id": "layout_random", "upgrade": 0, "hp": 1.5, "layout": "random"},
        {"id": "limit_5", "upgrade": 0, "hp": 1.5, "layout": "dense", "gain_limit": 5},
        {"id": "limit_15", "upgrade": 0, "hp": 1.5, "layout": "dense", "gain_limit": 15},
        {"id": "normal3_nearest_weak", "upgrade": 0, "hp": 1.0, "layout": "stage_normal_002", "enemy_count": 3, "direction": "nearest", "power": 2},
        {"id": "normal3_nearest_medium", "upgrade": 0, "hp": 1.0, "layout": "stage_normal_002", "enemy_count": 3, "direction": "nearest", "power": 5},
        {"id": "normal3_nearest_strong", "upgrade": 0, "hp": 1.0, "layout": "stage_normal_002", "enemy_count": 3, "direction": "nearest", "power": 8},
        {"id": "dense6_nearest_weak", "upgrade": 0, "hp": 1.5, "layout": "dense", "enemy_count": 6, "direction": "nearest", "power": 2},
        {"id": "dense6_nearest_medium", "upgrade": 0, "hp": 1.5, "layout": "dense", "enemy_count": 6, "direction": "nearest", "power": 5},
        {"id": "dense6_nearest_strong", "upgrade": 0, "hp": 1.5, "layout": "dense", "enemy_count": 6, "direction": "nearest", "power": 8},
        {"id": "dense6_cluster_strong", "upgrade": 0, "hp": 1.5, "layout": "dense", "enemy_count": 6, "direction": "cluster_center", "power": 8},
        {"id": "dense6_sparse_strong", "upgrade": 0, "hp": 1.5, "layout": "dense", "enemy_count": 6, "direction": "sparse_opposite", "power": 8},
        {"id": "random6_nearest_strong", "upgrade": 0, "hp": 1.5, "layout": "random", "enemy_count": 6, "direction": "nearest", "power": 8},
        {"id": "random6_cluster_strong", "upgrade": 0, "hp": 1.5, "layout": "random", "enemy_count": 6, "direction": "cluster_center", "power": 8},
        {"id": "random6_sparse_strong", "upgrade": 0, "hp": 1.5, "layout": "random", "enemy_count": 6, "direction": "sparse_opposite", "power": 8},
    ]
    if args.case:
        requested = set(args.case)
        cases = [case for case in cases if case["id"] in requested]
        missing = requested - {case["id"] for case in cases}
        if missing:
            parser.error("unknown case: " + ", ".join(sorted(missing)))
    results = [capture_case(args.label, case) for case in cases]
    output = CAPTURE_ROOT / args.label / "measurements.json"
    harness.write(output, {"results": results})
    for result in results:
        shot = result["shot"]
        chain = shot["physics_chain"]
        print(
            result["case"]["id"],
            "collisions=", chain["effective_enemy_enemy_collision_count"],
            "involved=", chain["involved_enemy_count"],
            "defeated=", shot["enemies_defeated"],
            "distance=", round(chain["total_enemy_distance_xz"], 3),
            "uncapped=", shot["heavy_stacks_uncapped_gain"],
            "granted=", shot["heavy_stacks_generated"],
            "turn_discarded=", shot["heavy_stacks_discarded_by_turn_limit"],
            "duration_ms=", shot["duration_ms"],
            flush=True,
        )
    print(output, flush=True)


if __name__ == "__main__":
    main()
