"""Read-only policy/turn audits for completed and in-progress MCP campaigns."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def audit_raw(raw):
    counts = Counter()
    stages = []
    error_examples = []
    hp_examples = []
    for stage in raw.get("stages", []):
        result = stage.get("stage_result", {})
        events = stage.get("damage_events", [])
        if "player_damage_taken" in result:
            if sum(e["damage"] for e in events) != result["player_damage_taken"]:
                raise ValueError("HP damage events disagree with stage result")
        phase = stage.get("stage_context", {}).get("combat_mode") == "phase_prototype"
        shots = stage.get("shots", [])
        stage_counts = Counter()
        for index, shot in enumerate(shots):
            evaluation = shot.get("shot_context", {}).get("mcp_telemetry", {}).get("build_evaluation", {})
            if evaluation:
                stage_counts["geometric_evaluated_shots"] += 1
                projection = evaluation.get("coupled_chain")
                if projection:
                    stage_counts["coupled_chain_shots"] += 1
                    stage_counts["coupled_chain_complete"] += projection.get("complete") is True
                    stage_counts["coupled_chain_truncated"] += projection.get("complete") is False
                    stage_counts["coupled_chain_predicted_post_contacts"] += len(projection.get("contacts", []))
                    native = shot.get('physics_chain', {})
                    resolved = bool(shot.get('resolved_at')) or index+1 < len(shots) or result.get('result') in {'clear','game_over'}
                    if resolved and native.get('enabled') and isinstance(native.get('effective_enemy_enemy_collision_count'), (int, float)):
                        predicted_pairs = sum(not c.get('player_contact') for c in projection.get('contacts', []))
                        actual_pairs = native['effective_enemy_enemy_collision_count']
                        stage_counts['coupled_native_observed_shots'] += 1
                        stage_counts['coupled_enemy_pair_absolute_error'] += abs(predicted_pairs-actual_pairs)
                        stage_counts['coupled_enemy_pair_underestimated_contacts'] += max(0, actual_pairs-predicted_pairs)
                        stage_counts['coupled_enemy_pair_overestimated_contacts'] += max(0, predicted_pairs-actual_pairs)
                        stage_counts['coupled_enemy_pair_predicted_contacts'] += predicted_pairs
                        stage_counts['coupled_enemy_pair_actual_contacts'] += actual_pairs
                if evaluation.get("robustness"):
                    stage_counts["stress_evaluated_shots"] += 1
                    stage_counts["stress_variants"] += len(evaluation["robustness"].get("samples", []))
                stage_counts["retained_body_fallback_shots"] += bool(evaluation.get("retained_body_blocker"))
                if evaluation.get("shot_type") == "bank":
                    stage_counts["geometric_bank_shots"] += 1
                    stage_counts["geometric_bank_with_any_player_enemy_hit"] += bool(shot.get("player_enemy_hit_count", 0))
            decision = shot.get("decision_log", {})
            comparison = decision.get("candidate_comparison", {})
            candidates = comparison.get("candidates", [])
            if stage.get("stage_type") != "boss" and candidates:
                stage_counts["joint_shots"] += 1
                mismatch = comparison.get("actual_matches_recommendation") is False
                stage_counts["ball_recommendation_mismatches"] += mismatch
                actual = next((c for c in candidates if c.get("instance_id") == comparison.get("actual_selected_instance_id")), None)
                if mismatch and actual and max(c["score"] for c in candidates) > actual["score"] + .001:
                    stage_counts["higher_score_ball_ignored"] += 1
            if not phase:
                continue
            prediction = evaluation.get("expected_incoming_damage")
            stage_counts["phase_shots"] += 1
            stage_counts["phase_positive_incoming_prediction"] += isinstance(prediction, (int, float)) and prediction > 0
            stage_counts["phase_forecast_telemetry"] += bool(evaluation.get("enemy_turn_forecast"))
            start = shot["elapsed_from_run_start_ms"]
            if index + 1 < len(shots):
                end = shots[index + 1]["elapsed_from_run_start_ms"]
            elif result.get("result") in {"clear", "game_over"}:
                end = stage["elapsed_from_run_start_ms"] + result["duration_ms"] + 1
            else:
                continue  # Enemy action may not yet have finished.
            actual_damage = sum(e["damage"] for e in events if e["source"] == "enemy_attack" and start <= e["elapsed_from_run_start_ms"] < end)
            stage_counts["settled_phase_turns"] += 1
            stage_counts["phase_actual_attack_turns"] += actual_damage > 0
            turn_plans = [e.get("details", {}) for e in raw.get("events", [])
                          if e.get("event_type") == "enemy_action_plan" and start <= e.get("elapsed_from_run_start_ms", -1) < end]
            attacking_targets = {a.get("target_id") for plan in turn_plans for a in plan.get("actions", [])
                                 if a.get("action") in {"normal_attack", "charge_attack"}}
            stopped_targets = {key for key, value in (evaluation.get("expected_attack_prevention_by_target") or {}).items()
                               if value.get("reason") == "defeat_or_pocket_control"}
            stage_counts["phase_defeat_prevention_target_checks"] += len(stopped_targets)
            # Legacy counter name: target_id is a component-list index, not a
            # persistent identity. Removal can reuse it before the next plan.
            # This intersection alone cannot prove a failed kill prediction.
            stage_counts["phase_defeat_prevention_targets_still_attacking"] += len(stopped_targets & attacking_targets)
            stage_counts["phase_predicted_kills"] += decision.get("predicted_kills", 0) or 0
            stage_counts["phase_actual_kills"] += shot.get("enemies_defeated", 0) or 0
            if isinstance(prediction, (int, float)):
                stage_counts["phase_predicted_hp_damage"] += prediction
                stage_counts["phase_prediction_absolute_error"] += abs(prediction - actual_damage)
                if actual_damage > 0:
                    stage_counts["phase_error_on_actual_attack_turns"] += abs(prediction - actual_damage)
                stage_counts["phase_predicted_damage_but_no_hp_loss"] += prediction > 0 and actual_damage == 0
                stage_counts["phase_underestimated_hp_damage"] += max(0, actual_damage - prediction)
                stage_counts["phase_overestimated_hp_damage"] += max(0, prediction - actual_damage)
                conservative = evaluation.get('conservative_incoming_damage', prediction)
                stage_counts['phase_conservative_absolute_error'] += abs(conservative-actual_damage)
                stage_counts['phase_conservative_underestimated_hp_damage'] += max(0, actual_damage-conservative)
                stage_counts['phase_conservative_overestimated_hp_damage'] += max(0, conservative-actual_damage)
                if actual_damage > prediction or abs(prediction - actual_damage) >= 1 or stopped_targets & attacking_targets:
                    error_examples.append({"stage": stage["stage_id"], "stage_index": stage.get("stage_index"), "shot": shot.get("shot_no", index + 1),
                        "ball": shot.get("ball_id"), "hp_at_shot": shot.get("player_hp_before"),
                        "predicted_incoming": prediction, "actual_enemy_hp_damage": actual_damage,
                        "absolute_error": abs(prediction - actual_damage),
                        "predicted_kills": decision.get("predicted_kills"), "actual_kills": shot.get("enemies_defeated"),
                        "target_id": evaluation.get("target_id"), "shot_type": evaluation.get("shot_type"),
                        "shot_goal": evaluation.get("shot_goal"), "contact_kind": evaluation.get("contact_kind"),
                        "predicted_attack_prevention": evaluation.get("expected_attack_prevention_by_target"),
                        "expected_defeated_target_ids": evaluation.get("expected_defeated_target_ids"),
                        "speculative_defeated_target_ids": evaluation.get("speculative_defeated_target_ids"),
                        "retained_body_blocker": evaluation.get("retained_body_blocker"),
                        "robust_primary_contact": evaluation.get("robust_primary_contact"),
                        "expected_shield_after_pocket": evaluation.get("expected_shield_after_pocket"),
                        "expected_self_pocket_hp_loss": evaluation.get("expected_self_pocket_hp_loss"),
                        "conservative_incoming_damage": evaluation.get("conservative_incoming_damage"),
                        "conservative_net_hp_loss": evaluation.get("conservative_net_hp_loss"),
                        "chain_confidence_weight": evaluation.get("chain_confidence_weight"),
                        "collision_shield_events": evaluation.get("collision_shield_events"),
                        "coupled_chain": evaluation.get("coupled_chain"),
                        "robustness": evaluation.get("robustness"),
                        "score_breakdown": evaluation.get("score_breakdown"),
                        "predicted_stopped_targets_still_attacking": sorted(stopped_targets & attacking_targets),
                        "forecast": evaluation.get("enemy_turn_forecast"),
                        "predicted_self_pocket_risk": evaluation.get("self_pocket_risk"),
                        "predicted_safety_metric": evaluation.get("metrics", {}).get("safety"),
                        "actual_player_damage_events": [e for e in events if start <= e["elapsed_from_run_start_ms"] < end],
                        "shield_and_pocket_events": [e for e in raw.get("events", []) if start <= e.get("elapsed_from_run_start_ms", -1) < end and e.get("event_type") in {"stop_shield_absorbed", "player_pocket_returned", "player_shield_expired"}],
                        "human_error": shot.get("shot_context", {}).get("mcp_telemetry", {}).get("human_error"),
                        "actual_defeat_events": [e.get("details", {}) for e in shot.get("shot_events", []) if e.get("event_type") == "enemy_defeated"],
                        "enemy_action_plans": [e.get("details", {}) for e in raw.get("events", []) if e.get("event_type") == "enemy_action_plan" and start <= e.get("elapsed_from_run_start_ms", -1) < end]})
            stage_counts["phase_zero_prediction_with_actual_attack"] += prediction == 0 and actual_damage > 0
            if prediction == 0 and actual_damage > 0:
                stage_counts["zero_prediction_with_kill_overestimate"] += (decision.get("predicted_kills", 0) > shot.get("enemies_defeated", 0))
            stage_counts["phase_enemy_hp_damage"] += actual_damage
        # Gross HP loss includes self-pocket damage; repair is not subtracted.
        # Audit this for every geometric shot, including legacy/midboss stages.
        for index, shot in enumerate(shots):
            ev = shot.get("shot_context", {}).get("mcp_telemetry", {}).get("build_evaluation", {})
            if not isinstance(ev.get("expected_incoming_damage"), (int, float)):
                continue
            start = shot["elapsed_from_run_start_ms"]
            if index + 1 < len(shots): end = shots[index + 1]["elapsed_from_run_start_ms"]
            elif result.get("result") in {"clear", "game_over"}: end = stage["elapsed_from_run_start_ms"] + result["duration_ms"] + 1
            else: continue
            turn_events = [e for e in events if start <= e["elapsed_from_run_start_ms"] < end]
            actual_total = sum(e["damage"] for e in turn_events)
            actual_pocket = sum(e["damage"] for e in turn_events if e["source"] == "pocket")
            shield_events = [e for e in raw.get('events', []) if start <= e.get('elapsed_from_run_start_ms', -1) < end
                             and e.get('event_type') in {'stop_shield_absorbed', 'player_pocket_returned', 'player_shield_expired'}]
            fell = actual_pocket > 0 or any(e['event_type'] == 'player_pocket_returned' for e in shield_events)
            pocket_risk = float(ev.get('self_pocket_risk') or 0)
            predicted_pocket = float(ev.get("expected_self_pocket_hp_loss") or 0)
            predicted_total = ev["expected_incoming_damage"] + predicted_pocket
            stage_counts["geometric_settled_hp_forecasts"] += 1
            stage_counts["geometric_total_hp_absolute_error"] += abs(predicted_total - actual_total)
            stage_counts["geometric_total_zero_prediction_with_damage"] += predicted_total == 0 and actual_total > 0
            stage_counts["geometric_pocket_zero_prediction_with_damage"] += predicted_pocket == 0 and actual_pocket > 0
            stage_counts["geometric_actual_pocket_damage"] += actual_pocket
            stage_counts["geometric_expected_pocket_damage"] += predicted_pocket
            stage_counts["geometric_actual_gross_damage"] += actual_total
            stage_counts["geometric_expected_gross_damage"] += predicted_total
            stage_counts['geometric_actual_self_pocket_turns'] += fell
            stage_counts['geometric_zero_risk_with_self_pocket'] += pocket_risk == 0 and fell
            stage_counts['geometric_pocket_risk_sum'] += pocket_risk
            if actual_total > predicted_total or actual_pocket > predicted_pocket or abs(predicted_total - actual_total) >= 2 or (pocket_risk == 0 and fell):
                hp_examples.append({"stage": stage["stage_id"], "stage_index": stage.get("stage_index"),
                    "shot": shot.get("shot_no", index + 1), "ball": shot.get("ball_id"),
                    "predicted_gross": predicted_total, "actual_gross": actual_total,
                    "predicted_pocket": predicted_pocket, "actual_pocket": actual_pocket,
                    "self_pocket_risk": pocket_risk, 'actual_self_pocket': fell,
                    "expected_shield_after_pocket": ev.get('expected_shield_after_pocket'),
                    "conservative_incoming_damage": ev.get('conservative_incoming_damage'),
                    "expected_defeated_target_ids": ev.get('expected_defeated_target_ids'),
                    "speculative_defeated_target_ids": ev.get('speculative_defeated_target_ids'),
                    "score_breakdown": ev.get('score_breakdown'), 'shield_and_pocket_events': shield_events,
                    "human_error": shot.get('shot_context', {}).get('mcp_telemetry', {}).get('human_error'),
                    "damage_events": turn_events, "coupled_chain": ev.get("coupled_chain"), "robustness": ev.get("robustness")})
        counts.update(stage_counts)
        stages.append({"id": stage["stage_id"], "mode": stage.get("stage_context", {}).get("combat_mode"),
                       "start_hp": stage["stage_start"]["player_hp"], "result": result,
                       "minimum_observed_hp": min([stage["stage_start"]["player_hp"]]
                           + [shot[k] for shot in shots for k in ("player_hp_before", "player_hp_after") if isinstance(shot.get(k), (int, float))]
                           + ([result["player_hp"]] if isinstance(result.get("player_hp"), (int, float)) else [])),
                       "damage_sources": dict(Counter({source: sum(e["damage"] for e in events if e["source"] == source)
                                                      for source in {e["source"] for e in events}})), "audit": dict(stage_counts)})
    error_examples.sort(key=lambda row: -row["absolute_error"])
    selected_errors = error_examples[:12]
    for row in error_examples:
        if (row["actual_enemy_hp_damage"] > row["predicted_incoming"] or row["predicted_stopped_targets_still_attacking"]) and row not in selected_errors:
            selected_errors.append(row)  # Never hide an underestimate behind larger overestimates.
    return {"audit": dict(counts), "stages": stages, "prediction_error_examples": selected_errors,
            "hp_prediction_error_examples": hp_examples}


def analyze(folder, baseline=None, include_active=False):
    folder = Path(folder).resolve()
    rows = []
    seen = set()
    for path in sorted((folder / "batches").glob("*/all_runs.jsonl")):
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            record = json.loads(line)
            raw = read(record["source_log"])
            seen.add(raw["run_id"])
            rest = record.get("rest_decision_history", [])
            shots = [shot for stage in raw["stages"] for shot in stage.get("shots", [])]
            resource_fields = ("heavy_stacks_generated", "heavy_stacks_consumed", "pierce_stacks_generated",
                               "pierce_stacks_consumed", "reflection_stacks_generated", "reflection_stacks_consumed")
            rows.append({"seed": record["run_seed"], "build": record["build_profile"],
                         "won": record["final_boss_defeated"], "complete": True, "source": record["source_log"],
                         "shots": record["total_shots"], "final_hp": record["final_hp"],
                         "rest_decisions": rest, "rest_heals": record["heal_count"],
                         "ball_usage": record["ball_usage_by_definition"],
                         "ball_ability_activations": record.get("ball_ability_activation_counts", {}),
                         "resource_flow": {name: sum(shot.get(name, 0) or 0 for shot in shots) for name in resource_fields},
                         "target_ball_shot_share": record["target_build_ball_shot_share"], **audit_raw(raw)})
    completed_counts = Counter(row["build"] for row in rows)
    if include_active:
        for worker in (folder / "isolated").glob("*"):
            logs = sorted((worker / "logs/balance").glob("run_*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
            if not logs:
                continue
            try:
                raw = read(logs[0])
            except (OSError, ValueError):
                continue
            if raw["run_id"] in seen:
                continue
            rows.append({"seed": raw.get("run_context", {}).get("randomness", {}).get("run_seed"),
                         "build": worker.name, "complete": False, "source": str(logs[0]), **audit_raw(raw)})
    counts = Counter()
    for row in rows:
        counts.update(row["audit"])
    result = {"directory": str(folder), "completed_by_build": dict(completed_counts),
              "analysis_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "completed_runs": sum(completed_counts.values()), "includes_partial": include_active,
              "audit": dict(counts), "runs": rows,
              "interpretation": "HP loss is assigned through the following enemy turn; partial final turns are excluded. Prediction errors include geometry/kill errors and do not alone prove an attack-timer bug."}
    if baseline:
        old = analyze(baseline)
        indexed = {(row["seed"], row["build"]): row for row in old["runs"]}
        paired = [(indexed[(row["seed"], row["build"])], row) for row in rows if row["complete"] and (row["seed"], row["build"]) in indexed]
        result["paired_runs"] = len(paired)
        result["loss_to_win"] = [{"seed": b["seed"], "build": b["build"]} for a, b in paired if not a["won"] and b["won"]]
        result["win_to_loss"] = [{"seed": b["seed"], "build": b["build"]} for a, b in paired if a["won"] and not b["won"]]
        result["prior_defeat_case_comparisons"] = [{"seed": b["seed"], "build": b["build"],
                                                    "before": a, "after": b} for a, b in paired if not a["won"]]
        result["new_defeats"] = [row for row in rows if row["complete"] and not row["won"]]
        result["baseline_audit"] = old["audit"]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--include-active", action="store_true")
    args = parser.parse_args()
    result = analyze(args.campaign, args.baseline, args.include_active)
    destination = args.campaign / "behavior_analysis.json"
    destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({key: result[key] for key in ["completed_runs", "completed_by_build", "audit"]}, ensure_ascii=True))
    if args.baseline:
        print(json.dumps({key: result[key] for key in ["paired_runs", "loss_to_win", "win_to_loss"]}))
