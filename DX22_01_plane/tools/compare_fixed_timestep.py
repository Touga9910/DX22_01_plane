"""Compare physical outcomes and ordered damage records from isolated captures."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "runtime_tests/fixed_timestep"


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def close(a, b, path=""):
    if isinstance(a, dict):
        assert a.keys() == b.keys(), (path, a.keys(), b.keys())
        for key in a:
            close(a[key], b[key], f"{path}.{key}")
    elif isinstance(a, list):
        assert len(a) == len(b), (path, len(a), len(b))
        for i, (left, right) in enumerate(zip(a, b)):
            close(left, right, f"{path}[{i}]")
    elif isinstance(a, float):
        assert abs(a - b) <= 1.0e-5, (path, a, b)
    else:
        assert a == b, (path, a, b)


def outcome(folder):
    state = read(folder / "final.json")
    # Stage-4 captures predate this field; ordinary enemies have always been pocketable.
    for enemy in state["enemies"]:
        enemy.setdefault("pocket_immune", False)
    logs = list((folder / "logs/balance").glob("run_*.json"))
    assert len(logs) == 1, logs
    log = read(logs[0])
    assert len(log["stages"]) == 1
    stage = log["stages"][0]
    assert len(stage["shots"]) == 1, "A single command must produce exactly one shot"
    shot = stage["shots"][0]
    shot_fields = ["ball_id", "power", "position", "velocity", "upgrade_level",
        "player_hp_before", "player_hp_after", "enemies_alive_before", "enemies_alive_after",
        "enemies_defeated", "enemy_damage_events", "enemy_enemy_damage", "enemy_enemy_hit_count",
        "player_enemy_damage", "player_enemy_hit_count", "total_damage_collision_count", "total_enemy_damage"]
    return {
        "player": {key: state["player"][key] for key in (
            "current_hp", "position", "pocketed", "idle", "defense", "attack")},
        "enemies": state["enemies"], "game_state": state["game_state"],
        "shot": {key: shot[key] for key in shot_fields},
        "damage_events": stage["damage_events"],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--ignore-target-order", action="store_true")
    parser.add_argument("--ignore-enemy-attack-order", action="store_true",
                        help="Compare enemy-turn attacks by source/count, retaining ordered collision and pocket events")
    args = parser.parse_args()
    summary = read(ROOT / args.after / "summary.json")
    reports = []
    for case in summary["results"]:
        name = case["case"]
        before = outcome(ROOT / args.before / name)
        after = outcome(ROOT / args.after / name)
        if args.ignore_target_order:
            for value in (before, after):
                for enemy in value["enemies"]:
                    enemy.pop("target_id", None)
                value["enemies"].sort(key=lambda enemy: (
                    enemy["enemy_id"], enemy["position"]["x"], enemy["position"]["z"], enemy["hp"]))
        # Damage timestamps are wall time, not part of the physical result.
        for value in (before, after):
            for event in value["damage_events"]:
                for key in ("at", "timestamp", "recorded_at", "elapsed_from_run_start_ms", "elapsed_from_stage_start_ms"):
                    event.pop(key, None)
            if args.ignore_enemy_attack_order:
                value["enemy_attack_events"] = sorted(
                    [event for event in value["damage_events"] if event["source"] == "enemy_attack"],
                    key=lambda event: (event["source_id"], event["damage"]))
                value["damage_events"] = [event for event in value["damage_events"]
                                          if event["source"] != "enemy_attack"]
        close(before, after, name)
        frames = read(ROOT / args.after / name / "frames.json")
        clocks = [frame["physics_clock"] for frame in frames if "physics_clock" in frame]
        max_steps = max((clock["steps_last_frame"] for clock in clocks), default=0)
        assert max_steps <= 8
        reports.append({"case": name, "matched": True, "observed_max_steps": max_steps,
                        "damage_contacts": after["shot"]["total_damage_collision_count"]})
        print(f"{name}: positions, HP, ordered collision damage, one shot and turn result MATCH; max steps={max_steps}")
    (ROOT / args.after / "comparison.json").write_text(
        json.dumps({"reference": args.before, "ignore_target_order": args.ignore_target_order,
                    "ignore_enemy_attack_order": args.ignore_enemy_attack_order,
                    "cases": reports}, indent=2), encoding="utf-8")
