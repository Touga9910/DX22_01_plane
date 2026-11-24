"""Audit paired target-prediction and prospective recovery policy campaigns."""
import argparse
import hashlib
import json
from pathlib import Path

from analyze_policy_behavior import analyze, read
from analyze_holdout_campaign import summarize, recovery_summary, stage_groups
from compare_balance_campaigns import BUILDS, load_campaign, write_table


def evaluate(folder, baseline, include_active=False):
    folder, baseline = Path(folder).resolve(), Path(baseline).resolve()
    before_manifest, after_manifest = (read(d / "overnight_status.json") for d in (baseline, folder))
    for key in ("exe_sha256", "profile_progress_sha256", "required_balance_version", "player_level", "validation_variant", "dynamic_balance"):
        if before_manifest[key] != after_manifest[key]:
            raise ValueError(f"Changed game conditions: {key}")
    for directory, manifest in ((baseline, before_manifest), (folder, after_manifest)):
        for name, digest in manifest["policy_source_hashes"].items():
            source = directory / "policy_snapshot/tools/game_mcp" / name
            if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                raise ValueError(f"Frozen source changed: {source}")
    reference = load_campaign(baseline)
    seen = set()
    for path in (folder / "batches").glob("*/all_runs.jsonl"):
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            record = json.loads(line)
            pair = record["run_seed"], record["build_profile"]
            if pair in seen or pair not in reference["pairs"] or not record["normal_end"] or record["configuration_signature"] != reference["signature"]:
                raise ValueError(f"Invalid paired run: {pair}")
            seen.add(pair)
    if after_manifest["state"] == "completed" and seen != reference["pairs"]:
        raise ValueError("Completed campaign lacks the exact baseline pairs")
    old, new = analyze(baseline), analyze(folder, include_active=include_active)
    indexed = {(r["seed"], r["build"]): r for r in old["runs"]}
    pairs = []
    for r in new["runs"]:
        if not r["complete"]:
            continue
        left = indexed[r["seed"], r["build"]]
        row = {"seed": r["seed"], "build": r["build"]}
        for label, run in (("before", left), ("after", r)):
            row.update({label + "_" + k: v for k, v in summarize([run]).items()})
            row[label + "_minimum_battle_exit_hp"] = min(s["result"]["player_hp"] for s in run["stages"])
        pairs.append(row)
    def danger(rows):
        return [{"seed": r["seed"], "build": r["build"], "stage": s["id"],
                 "entry_hp": s["start_hp"], "damage": s["result"]["player_damage_taken"],
                 "exit_hp": s["result"]["player_hp"], "source": r["source"]}
                for r in rows if r["complete"] for s in r["stages"] if s["result"]["player_hp"] <= 10]
    result = {"state": after_manifest["state"], "completed": new["completed_runs"],
              "completed_by_build": new["completed_by_build"], "paired_runs": len(pairs),
              "analysis_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "behavior_analysis_source_sha256": new["analysis_source_sha256"],
              "same_game_conditions": True, "exact_baseline_pairs_so_far": True,
              "before": summarize(old["runs"]), "after": summarize(new["runs"]),
              "paired_before": summarize([indexed[r["seed"], r["build"]] for r in new["runs"] if r["complete"]]),
              "before_audit": old["audit"], "after_audit": new["audit"],
              "before_recovery": recovery_summary(old["runs"]), "after_recovery": recovery_summary(new["runs"]),
              "before_danger": danger(old["runs"]), "after_danger": danger(new["runs"]),
              "before_low_hp_observations": [{"seed": r["seed"], "build": r["build"], "stage": s["id"], "minimum_observed_hp": s["minimum_observed_hp"], "exit_hp": s["result"].get("player_hp")}
                  for r in old["runs"] if r["complete"] for s in r["stages"] if s["minimum_observed_hp"] <= 10],
              "after_low_hp_observations": [{"seed": r["seed"], "build": r["build"], "stage": s["id"], "minimum_observed_hp": s["minimum_observed_hp"], "exit_hp": s["result"].get("player_hp")}
                  for r in new["runs"] if r["complete"] for s in r["stages"] if s["minimum_observed_hp"] <= 10],
              "active_low_hp_observations": [{"build": r["build"], "stage": s["id"], "minimum_observed_hp": s["minimum_observed_hp"], "source": r["source"]}
                  for r in new["runs"] if not r["complete"] for s in r["stages"] if s["minimum_observed_hp"] <= 10],
              "before_stages": stage_groups(old["runs"]), "after_stages": stage_groups(new["runs"]),
              "builds": [{"build": b, "before": summarize([r for r in old["runs"] if r["build"] == b]),
                          "after": summarize([r for r in new["runs"] if r["build"] == b])} for b in BUILDS],
              "pairs": pairs, "runs_after": new["runs"],
              "failures": [r for r in new["runs"] if r["complete"] and not r["won"]]}
    (folder / "survival_analysis.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    write_table(folder / "survival_pairs.csv", pairs)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--include-active", action="store_true")
    args = parser.parse_args()
    p = evaluate(args.campaign, args.baseline, args.include_active)
    print(json.dumps({k: p[k] for k in ("state", "completed_by_build", "after", "after_audit", "after_danger")}, ensure_ascii=True))
    print(json.dumps({"failures": [{"seed": r["seed"], "build": r["build"], "source": r["source"]} for r in p["failures"]]}))
