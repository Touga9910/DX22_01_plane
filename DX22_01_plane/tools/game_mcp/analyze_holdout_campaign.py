"""Validate a frozen policy on predeclared, unused seeds; no policy fitting."""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime
import hashlib
import json
from pathlib import Path
import statistics

from analyze_policy_behavior import analyze, read
from compare_balance_campaigns import BUILDS, load_campaign, write_table


def validate_contract(reference, current, audit, reference_seeds):
    for key in ("exe_sha256", "profile_progress_sha256", "required_balance_version",
                "player_level", "validation_variant", "dynamic_balance", "policy_source_hashes"):
        if reference[key] != current[key]:
            raise ValueError(f"Held-out conditions changed: {key}")
    seeds = current["seeds_requested"]
    if len(seeds) != 10 or len(set(seeds)) != 10 or seeds != audit["seeds"]:
        raise ValueError("Held-out seeds differ from the predeclared ten seeds")
    if set(seeds) & set(reference_seeds):
        raise ValueError("Held-out seeds overlap the reference cohort")
    if audit["matches"] or audit.get("calibration_updated") is not False:
        raise ValueError("Seed novelty or frozen calibration could not be verified")
    if datetime.fromisoformat(audit["created_at"]) > datetime.fromisoformat(current["started_at"]):
        raise ValueError("Unused-seed audit must precede collection")


def summarize(rows):
    rows = [r for r in rows if r["complete"]]
    count = len(rows)
    battles = sum(len(r["stages"]) for r in rows)
    return {"runs": count, "wins": sum(r["won"] for r in rows),
            "win_rate": sum(r["won"] for r in rows) / count if count else None,
            "mean_damage": statistics.mean(sum(s["result"]["player_damage_taken"] for s in r["stages"]) for r in rows) if count else None,
            "mean_shots": statistics.mean(r["shots"] for r in rows) if count else None,
            "mean_battles": battles / count if count else None,
            "damage_per_battle": sum(s["result"]["player_damage_taken"] for r in rows for s in r["stages"]) / battles if battles else None,
            "shots_per_battle": sum(r["shots"] for r in rows) / battles if battles else None,
            "mean_final_hp": statistics.mean(r["final_hp"] for r in rows) if count else None,
            "mean_heals": statistics.mean(r["rest_heals"] for r in rows) if count else None,
            "mean_target_ball_share": statistics.mean(r["target_ball_shot_share"] for r in rows) if count else None}


def stage_groups(rows):
    groups = defaultdict(list)
    for row in rows:
        if row["complete"]:
            for stage in row["stages"]:
                groups[stage["id"]].append(stage)
    return [{"stage": name, "battles": len(stages),
             "losses": sum(s["result"]["result"] == "game_over" for s in stages),
             "mean_damage": statistics.mean(s["result"]["player_damage_taken"] for s in stages),
             "mean_shots": statistics.mean(s["result"]["total_shots"] for s in stages),
             "settled_phase_turns": sum(s["audit"].get("settled_phase_turns", 0) for s in stages),
             "phase_prediction_absolute_error": sum(s["audit"].get("phase_prediction_absolute_error", 0) for s in stages),
             "mean_entry_hp": statistics.mean(s["start_hp"] for s in stages)}
            for name, stages in sorted(groups.items())]


def recovery_summary(rows):
    rows = [r for r in rows if r["complete"]]
    decisions = [d for r in rows for d in r["rest_decisions"]]
    heals = [d for d in decisions if d["recommended_action"] == "heal"]
    exits = [s["result"]["player_hp"] for r in rows for s in r["stages"]]
    return {"decisions": len(decisions),
            "action_counts": dict(Counter(d["recommended_action"] for d in decisions)),
            "reason_counts": dict(Counter(d["reason"] for d in decisions)),
            "recorded_heals": sum(r["rest_heals"] for r in rows),
            "minimum_battle_exit_hp": min(exits) if exits else None,
            "minimum_hp_at_rest": min((d["current_hp"] for d in decisions), default=None),
            "mean_heal_amount": statistics.mean(d["recovery_amount"] for d in heals) if heals else None,
            "heals_remaining_below_reserve": sum(
                d.get("survival_reserve") is not None
                and d["current_hp"] + d["recovery_amount"] < d["survival_reserve"]["required_hp"] for d in heals),
            "upgrades_at_or_below_reserve": sum(
                d["recommended_action"] == "upgrade_ball"
                and d.get("survival_reserve") is not None
                and d["current_hp"] <= d["survival_reserve"]["required_hp"] for d in decisions),
            "interpretation": "Observed choices and outcomes only; no counterfactual proof of optimal healing"}


def evaluate(folder, reference_dir, audit_file, include_active=False):
    folder, reference_dir = Path(folder).resolve(), Path(reference_dir).resolve()
    current_manifest = read(folder / "overnight_status.json")
    reference_manifest = read(reference_dir / "overnight_status.json")
    reference = load_campaign(reference_dir)
    seed_audit = read(audit_file)
    validate_contract(reference_manifest, current_manifest, seed_audit,
                      {seed for seed, _ in reference["pairs"]})
    for directory, manifest in ((folder, current_manifest), (reference_dir, reference_manifest)):
        for name, expected in manifest["policy_source_hashes"].items():
            path = directory / "policy_snapshot/tools/game_mcp" / name
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Frozen policy was modified: {path}")
    seen = set()
    for path in (folder / "batches").glob("*/all_runs.jsonl"):
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            record = json.loads(line)
            pair = record["run_seed"], record["build_profile"]
            if pair in seen or pair[0] not in seed_audit["seeds"] or pair[1] not in BUILDS:
                raise ValueError(f"Duplicate or unexpected held-out pair: {pair}")
            seen.add(pair)
            raw = read(record["source_log"])
            context = raw["run_context"]
            if (not record["normal_end"] or record["configuration_signature"] != reference["signature"]
                    or raw["balance_version"] != reference["version"]
                    or (context.get("run_combat_mode"), context.get("mcp_shot_policy"), context.get("controller_profile")) != ("legacy", "standard", "intermediate")
                    or context.get("dynamic_balance_enabled_at_start") is not False):
                raise ValueError(f"Held-out run conditions disagree: {pair}")
    if current_manifest["state"] == "completed" and seen != {(seed, build) for seed in seed_audit["seeds"] for build in BUILDS}:
        raise ValueError("Completed holdout lacks the full 50 seed/build pairs")
    current = analyze(folder, include_active=include_active)
    known = analyze(reference_dir)
    builds = [{"build": build,
               "reference": summarize([r for r in known["runs"] if r["build"] == build]),
               "heldout": summarize([r for r in current["runs"] if r["build"] == build])} for build in BUILDS]
    seeds = [{"seed": seed, "runs": len(rows), "wins": sum(r["won"] for r in rows)}
             for seed in seed_audit["seeds"]
             for rows in [[r for r in current["runs"] if r["complete"] and r["seed"] == seed]]]
    result = {"created_at": datetime.now().astimezone().isoformat(),
              "analysis_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "behavior_analysis_source_sha256": current["analysis_source_sha256"],
              "state": current_manifest["state"], "seed_audit": seed_audit,
              "reference_directory": str(reference_dir), "heldout_directory": str(folder),
              "same_policy_and_game_conditions": True, "disjoint_seeds": True,
              "comparison_type": "Different-seed descriptive generalization check, not a paired policy-change comparison",
              "reference": summarize(known["runs"]), "heldout": summarize(current["runs"]),
              "builds": builds, "seeds": seeds, "reference_behavior": known["audit"],
              "reference_stages": stage_groups(known["runs"]), "heldout_stages": stage_groups(current["runs"]),
              "reference_recovery": recovery_summary(known["runs"]),
              "heldout_recovery": recovery_summary(current["runs"]),
              "heldout_behavior": current["audit"], "heldout_runs": current["runs"],
              "failures": [r for r in current["runs"] if r["complete"] and not r["won"]]}
    (folder / "holdout_analysis.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    write_table(folder / "holdout_builds.csv", builds)
    write_table(folder / "holdout_seeds.csv", seeds)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--seed-audit", type=Path, required=True)
    parser.add_argument("--include-active", action="store_true")
    args = parser.parse_args()
    result = evaluate(args.campaign, args.reference, args.seed_audit, args.include_active)
    print(json.dumps({k: result[k] for k in ("state", "heldout", "heldout_behavior")}, ensure_ascii=True))
    print(json.dumps({"failures": [{"seed": r["seed"], "build": r["build"], "source": r["source"]} for r in result["failures"]]}))
