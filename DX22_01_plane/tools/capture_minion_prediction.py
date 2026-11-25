"""Verify stopped Pierce minions against an isolated Debug game, without GUI input.

Only private fixture data is written. The first shot creates traces/stacks through
normal rules; the second uses player_minion_finisher. No gameplay source or user
save is changed. A Debug build supports DX22_TEST_HIDDEN_WINDOW=1.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

PROJECT = Path(__file__).resolve().parents[1]
REPO = PROJECT.parent
sys.path.insert(0, str(PROJECT / "tools/game_mcp"))
from bridge_store import GameBridgeStore, GameBridgeError


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def merge(base: dict, update: dict) -> dict:
    result = copy.deepcopy(base)
    for key, value in update.items():
        result[key] = merge(result.get(key, {}), value) if isinstance(value, dict) else value
    return result


def preset() -> dict:
    player_data = read(PROJECT / "assets/data/player_status.json")
    definitions = {value["id"]: value for value in player_data["balls"]}
    deck = []
    for index, definition_id in enumerate(("player_pierce", "player_minion_finisher"), 1):
        definition = definitions[definition_id]
        status = merge(player_data["status"], definition["status"])
        status.setdefault("shotShield", 0)
        status.setdefault("enemyCollisionShield", 0)
        upgrades = []
        upgraded = status
        for upgrade in definition["upgrades"]:
            upgraded = merge(upgraded, upgrade)
            upgrades.append(upgraded)
        deck.append(dict(definition_id=definition_id, category=definition["category"],
                         instance_id=index, upgrade_level=0, upgrade_model_version=7,
                         status=status, upgrade_table=upgrades))
    enemy = read(PROJECT / "assets/data/enemy_data.json")["enemies"][0]
    enemy_status = copy.deepcopy(enemy["status"])
    enemy_status.update(attack=0, maxHp=100)
    return dict(version=2, hp=100, max_hp=100, money=0, seed=20261125,
                player_x=0.0, player_z=0.0, deck=deck,
                enemies=[dict(id=enemy["id"], x=14.0, z=0.0, hp=100,
                              max_hp=100, status=enemy_status)],
                relics=[False] * 11, selected_ascension=0,
                highest_unlocked_ascension=0, achievements=[True] * 7,
                armor=2, break_shots=0, break_balls=[],
                enemy_validation=dict(enabled=False, hp_multiplier=1.0,
                                      enemy_count=6, layout_pattern="dense"))


def prepare(folder: Path) -> None:
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copytree(PROJECT / "assets/data", folder / "assets/data")
    # Graphical resources, settings, data and saves are copied into the private
    # fixture, so the harness never needs a window API or shared writable data.
    for resource in ("model", "texture", "fonts"):
        source = PROJECT / "assets" / resource
        if source.exists():
            shutil.copytree(source, folder / "assets" / resource)
    shutil.copytree(PROJECT / "shader", folder / "shader")
    for path in PROJECT.glob("*.cso"):
        shutil.copy2(path, folder / path.name)
    for name in ("balance_autoplay", "balance_validation", "dynamic_balance"):
        path = folder / f"assets/data/{name}.json"
        configuration = read(path)
        configuration["enabled"] = False
        write(path, configuration)
    write(folder / "assets/data/game_mcp_bridge.json", dict(
        enabled=True, allow_write_actions=True, bridge_directory="runtime/game_mcp",
        state_publish_interval_frames=1, command_poll_interval_frames=1))
    write(folder / "saves/settings.json", dict(
        bgm_volume=0.0, se_volume=0.0, fullscreen=False, resolution_index=0,
        camera_shake_enabled=False))
    write(folder / "saves/debug_battle_setup.json", preset())
    write(folder / "fixture.json", dict(
        seed=20261125, stacks_injected=False,
        shots=[dict(ball="player_pierce", direction=[1.0, 0.0], power=1.0),
               dict(ball="player_minion_finisher", direction=[0.0, 1.0], power=1.0)],
        purpose="Trace generation followed by minion creation and natural stopped removal"))


def wait_state(store: GameBridgeStore, process: subprocess.Popen, predicate, timeout=60) -> dict:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Owned test game exited: {process.returncode}")
        try:
            state = store.read_state()
            if predicate(state):
                return state
        except GameBridgeError:
            pass
        time.sleep(0.025)
    raise TimeoutError("Private test game did not reach the required state")


def compare(expected: dict, actual: dict, minion_count: int = 0) -> dict:
    assert expected["complete"] and expected["world_unchanged"], expected
    for name in ("player_enemy_contacts", "enemy_enemy_contacts", "heavy_stacks",
                 "heavy_stacks_gained_this_turn", "cushions"):
        assert expected[name] == actual[name], (name, expected[name], actual[name])
    predicted = {ball["id"]: ball for ball in expected["balls"]}
    observed = {ball["id"]: ball for ball in actual["balls"]}
    # Synthesized prediction minions use max uintptr_t - launch index. Runtime
    # uses component addresses and removes stopped actors before final capture.
    # Restrict the exception to exactly those launches, inactive with zero speed;
    # never ignore missing players, enemies, neutrals, or an active minion.
    minion_ids = {(1 << 64) - 1 - index for index in range(minion_count)}
    removed = predicted.keys() - observed.keys()
    assert removed == minion_ids, ("unexpected missing balls", removed, minion_ids)
    assert not (observed.keys() - predicted.keys()), "Unexpected actual actors"
    for key in removed:
        ball = predicted[key]
        assert not any(ball[name] for name in (
            "player", "enemy_id", "boss", "boss_minion", "break_ball",
            "active", "pocketed", "defeated")), ball
        assert ball["hp"] == 1 and ball["velocity"] == [0.0, 0.0, 0.0], ball
    maximum_error = 0.0
    for key in observed:
        wanted, seen = predicted[key], observed[key]
        for name in ("position", "velocity"):
            for first, second in zip(wanted[name], seen[name], strict=True):
                error = abs(first - second)
                maximum_error = max(maximum_error, error)
                assert error <= 1e-5, (key, name, wanted[name], seen[name])
        for name in ("hp", "active", "defeated", "pocketed", "boss", "boss_minion",
                     "break_ball", "break_ball_used", "armor", "break_shots_remaining"):
            assert wanted[name] == seen[name], (key, name, wanted[name], seen[name])
    return dict(matched=True, max_error=maximum_error, compared_actors=len(observed),
                stopped_minion_tombstones=len(removed), ticks=expected["ticks"],
                player_enemy_contacts=expected["player_enemy_contacts"],
                enemy_enemy_contacts=expected["enemy_enemy_contacts"],
                matched_hp=[dict(player=ball["player"], enemy_id=ball["enemy_id"], hp=ball["hp"])
                            for ball in observed.values()])


def shot(folder: Path, store: GameBridgeStore, process: subprocess.Popen,
         index: int, definition_id: str, direction: tuple[float, float], power: float) -> dict:
    destination = folder / f"shot_{index:02d}"
    destination.mkdir()
    before = store.read_state()
    write(destination / "before.json", before)
    offer = next(ball for ball in before["offered_balls"]
                 if ball["definition_id"] == definition_id)
    selected = store.submit_command("select_ball", dict(offer_index=offer["index"]))
    assert selected["ok"], selected
    # Delete only these two known diagnostic files in this private fixture.
    # Waiting for a new actual file prevents reusing the previous shot's state.
    for kind in ("expected", "actual"):
        (folder / f"runtime/shot_prediction_{kind}.json").unlink(missing_ok=True)
    result = store.submit_command("fire_shot", dict(
        direction_x=direction[0], direction_z=direction[1], power=power))
    assert result["ok"], result
    write(destination / "command_result.json", result)
    final = wait_state(store, process, lambda state:
        (folder / "runtime/shot_prediction_actual.json").is_file()
        and (state.get("latest_shot_pierce_attack") or {}).get("shot_duration_ms", 0) > 0
        and ("fire_shot" in state.get("available_actions", [])
             or state.get("debug_mode", {}).get("finished")), timeout=90)
    write(destination / "final.json", final)
    for kind in ("expected", "actual"):
        shutil.copy2(folder / f"runtime/shot_prediction_{kind}.json",
                     destination / f"{kind}.json")
    telemetry = final["latest_shot_pierce_attack"]
    minion_count = telemetry["minions_generated"] if definition_id == "player_minion_finisher" else 0
    if definition_id == "player_minion_finisher":
        assert before["ball_synergies"]["pierce_stacks"] > 0, before["ball_synergies"]
        assert minion_count > 0, telemetry
        assert all(minion["destruction_reason"] == "stopped"
                   and minion["movement_distance_xz"] > 0
                   for minion in telemetry["minions"]), telemetry["minions"]
    verification = compare(read(destination / "expected.json"),
                           read(destination / "actual.json"), minion_count)
    verification.update(ball=definition_id, minions=telemetry["minions"],
                        stacks_before=before["ball_synergies"]["pierce_stacks"],
                        player_hp=final["player"]["current_hp"])
    write(destination / "verification.json", verification)
    print(json.dumps(verification, ensure_ascii=False), flush=True)
    return final


def run(folder: Path, exe: Path) -> None:
    environment = dict(os.environ, DX22_TEST_PREDICTION="1",
                       DX22_TEST_HIDDEN_WINDOW="1", DX22_TEST_RENDER_HZ="60")
    environment.pop("DX22_TEST_RESUME_RUN", None)
    with (folder / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(exe), "--debug-battle"], cwd=folder,
                                   env=environment, stdout=log, stderr=log,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            store = GameBridgeStore(folder / "runtime/game_mcp")
            initial = wait_state(store, process, lambda state:
                                 "fire_shot" in state.get("available_actions", []))
            write(folder / "initial.json", initial)
            assert initial["ball_synergies"]["pierce_stacks"] == 0
            first = shot(folder, store, process, 1, "player_pierce", (1.0, 0.0), 1.0)
            assert first["ball_synergies"]["pierce_stacks"] > 0, first["ball_synergies"]
            shot(folder, store, process, 2, "player_minion_finisher", (0.0, 1.0), 1.0)
            write(folder / "summary.json", dict(
                passed=True, exe=str(exe), exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                gui_operations=False, user_saves_read=False, fixture=str(folder)))
        finally:
            if process.poll() is None:
                # The file bridge has no application-exit command. Reap only
                # the Popen-owned child; normal user game processes are untouched.
                process.terminate()
                process.wait(timeout=15)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--use-prepared", action="store_true")
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", arguments.label):
        parser.error("Use a fresh ASCII label")
    folder = REPO / "logs/minion_prediction" / arguments.label
    if not arguments.use_prepared:
        prepare(folder)
    if arguments.prepare_only:
        print(folder, flush=True)
        return
    if not arguments.exe or not arguments.exe.is_file():
        parser.error("--exe must point to a built Debug executable")
    run(folder, arguments.exe.resolve())


if __name__ == "__main__":
    main()
