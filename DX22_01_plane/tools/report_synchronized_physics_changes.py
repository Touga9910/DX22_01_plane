"""Report intended outcome changes between sequential and synchronized physics.

Unlike the FPS/permutation check, this does not require legacy outcomes to match.
"""
import argparse
import json
import math
from pathlib import Path
from compare_fixed_timestep import ROOT, outcome, read


def summary(label, case):
    folder = ROOT / label / case
    value = outcome(folder)
    clock = read(folder / "final.json").get("physics_clock", {})
    return {
        "player_hp": value["player"]["current_hp"],
        "player_position": value["player"]["position"],
        "damage": value["shot"]["total_enemy_damage"],
        "contacts": value["shot"]["total_damage_collision_count"],
        "enemy_enemy_contacts": value["shot"]["enemy_enemy_hit_count"],
        "enemies_defeated": value["shot"]["enemies_defeated"],
        "substep_limit_count": clock.get("substep_limit_count"),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("before")
    parser.add_argument("after")
    args = parser.parse_args()
    rows = []
    for entry in read(ROOT / args.after / "summary.json")["results"]:
        case = entry["case"]
        a, b = summary(args.before, case), summary(args.after, case)
        assert b["substep_limit_count"] == 0, (case, b)
        distance = math.sqrt(sum((a["player_position"][c] - b["player_position"][c]) ** 2
                                 for c in ("x", "y", "z")))
        rows.append({"case": case, "before": a, "after": b, "player_stop_distance_change": distance})
        print(f"{case}: HP {a['player_hp']}->{b['player_hp']}; damage {a['damage']}->{b['damage']}; "
              f"contacts {a['contacts']}->{b['contacts']}; stopped-position delta {distance:.3f}")
    (ROOT / args.after / "legacy_changes.json").write_text(
        json.dumps({"reference": args.before, "cases": rows}, indent=2), encoding="utf-8")
