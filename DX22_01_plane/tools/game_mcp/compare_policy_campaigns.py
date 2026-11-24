"""Compare frozen MCP policies under identical game rules and seed/build pairs."""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import statistics

from compare_balance_campaigns import BUILDS, load_campaign, write_table


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def records(directory):
    for batch in read(directory / "analysis.json")["batches"]:
        for line in (Path(batch) / "all_runs.jsonl").read_text(encoding="utf-8").splitlines():
            if line.strip():
                record = json.loads(line)
                yield record, read(record["source_log"])


def metrics(record, raw):
    shots = [shot for stage in raw["stages"] for shot in stage.get("shots", [])]
    events = raw.get("events", [])
    return {
        "seed": record["run_seed"], "build": record["build_profile"],
        "won": bool(record["final_boss_defeated"]),
        "damage": sum(stage["stage_result"]["player_damage_taken"] for stage in raw["stages"]),
        "shots": len(shots), "final_hp": record["final_hp"],
        "battles": len(raw["stages"]),
        "route_nodes": [event.get("details", {}).get("map_node_id") for event in events
                        if event.get("event_type") == "route_choice"],
        "enemy_enemy_hits": sum(shot.get("enemy_enemy_hit_count", 0) for shot in shots),
        "rest_visits": sum(event.get("event_type") == "route_area_completed"
                           and str(event.get("details", {}).get("area_type", "")).lower() in {"rest", "rest_site"}
                           for event in events),
        "rest_upgrades": sum(event.get("event_type") == "ball_upgraded"
                             and str(event.get("details", {}).get("source_scene", "")).lower() in {"rest", "rest_site"}
                             for event in events),
        "build_complete": record.get("build_complete", False),
        "target_ball_shot_share": record.get("target_build_ball_shot_share", 0),
        "generic_shield_absorption_event_count": sum(event.get("event_type") == "stop_shield_absorbed" for event in events),
        "balls_used": dict(Counter(shot["ball_id"] for shot in shots)),
        "events": dict(Counter(event["event_type"] for event in events)),
    }


def compare(before_dir, after_dir, target=10):
    before_dir, after_dir = Path(before_dir).resolve(), Path(after_dir).resolve()
    before, after = load_campaign(before_dir), load_campaign(after_dir)
    seeds = {seed for seed, _ in before["pairs"]}
    expected = {(seed, build) for seed in seeds for build in BUILDS}
    if len(seeds) != target:
        raise ValueError("Baseline must contain the requested number of distinct seeds")
    if before["pairs"] != expected or after["pairs"] != expected:
        raise ValueError("Both policies require every requested seed/build exactly once")
    if before["version"] != after["version"] or before["signature"] != after["signature"]:
        raise ValueError("Game balance or configuration changed between policies")
    manifests = [read(folder / "overnight_status.json") for folder in (before_dir, after_dir)]
    for folder, manifest in zip((before_dir, after_dir), manifests):
        for name, expected_hash in manifest["policy_source_hashes"].items():
            source = folder / "policy_snapshot/tools/game_mcp" / name
            if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected_hash:
                raise ValueError(f"Frozen policy source changed: {source}")
    for key in ("exe_sha256", "profile_progress_sha256", "player_level", "validation_variant"):
        if manifests[0][key] != manifests[1][key]:
            raise ValueError(f"Policy comparison changed {key}")
    source_hashes = [manifest["policy_source_hashes"] for manifest in manifests]
    changes = [key for key in sorted(set(source_hashes[0]) | set(source_hashes[1]))
               if source_hashes[0].get(key) != source_hashes[1].get(key)]
    samples = [[metrics(record, raw) for record, raw in records(folder)]
               for folder in (before_dir, after_dir)]
    indexed = [{(row["seed"], row["build"]): row for row in rows} for rows in samples]
    pairs = []
    for seed, build in sorted(expected):
        left, right = [index[(seed, build)] for index in indexed]
        pairs.append({"seed": seed, "build": build,
                      "route_changed": left["route_nodes"] != right["route_nodes"],
                      **{f"{key}_before": left[key] for key in ("won", "damage", "shots", "battles", "final_hp", "enemy_enemy_hits", "rest_visits", "rest_upgrades", "build_complete", "target_ball_shot_share")},
                      **{f"{key}_after": right[key] for key in ("won", "damage", "shots", "battles", "final_hp", "enemy_enemy_hits", "rest_visits", "rest_upgrades", "build_complete", "target_ball_shot_share")}})
    builds = []
    for build in BUILDS:
        rows = [row for row in pairs if row["build"] == build]
        summary = {"build": build, "pairs": len(rows),
                   "loss_to_win": sum(not row["won_before"] and row["won_after"] for row in rows),
                   "win_to_loss": sum(row["won_before"] and not row["won_after"] for row in rows),
                   "route_changes": sum(row["route_changed"] for row in rows)}
        for side in ("before", "after"):
            summary[f"wins_{side}"] = sum(row[f"won_{side}"] for row in rows)
            for key in ("damage", "shots", "battles", "final_hp", "enemy_enemy_hits", "rest_visits", "rest_upgrades", "build_complete", "target_ball_shot_share"):
                summary[f"mean_{key}_{side}"] = statistics.mean(row[f"{key}_{side}"] for row in rows)
        builds.append(summary)
    result = {"created_at": datetime.now().astimezone().isoformat(),
              "analysis_source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "before": str(before_dir), "after": str(after_dir),
              "balance_version": before["version"], "configuration_signature": before["signature"],
              "policy_source_changes": changes, "paired_runs": len(pairs),
              "measurement_notes": ["stop_shield_absorbed uses the shared player shield pool and cannot prove anchor-ball activation"],
              "builds": builds, "pairs": pairs, "samples_before": samples[0], "samples_after": samples[1]}
    (after_dir / "policy_comparison.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    write_table(after_dir / "policy_paired_runs.csv", pairs)
    write_table(after_dir / "policy_builds.csv", builds)
    lines = [datetime.now().astimezone().strftime("作成日：%Y年%m月%d日"),
             "MCP判断基準の修正前後比較", "",
             f"同一バランス・実行ファイル・初期セーブ、中級者、固定難度、共通seed/build {len(pairs)}組。",
             "判断ソースの差分：" + ", ".join(changes), ""]
    for row in builds:
        lines.append(f"{row['build']}: 勝利 {row['wins_before']}/{target} → {row['wins_after']}/{target}、"
                     f"平均被ダメージ {row['mean_damage_before']:.1f} → {row['mean_damage_after']:.1f}、"
                     f"平均手数 {row['mean_shots_before']:.1f} → {row['mean_shots_after']:.1f}、"
                     f"敗北→勝利 {row['loss_to_win']}組、勝利→敗北 {row['win_to_loss']}組。")
        lines.append(f"  平均戦闘数 {row['mean_battles_before']:.1f} → {row['mean_battles_after']:.1f}、"
                     f"経路変更 {row['route_changes']}組、ビルド完成率 {row['mean_build_complete_before']:.0%} → {row['mean_build_complete_after']:.0%}、"
                     f"対象球の使用割合平均 {row['mean_target_ball_shot_share_before']:.1%} → {row['mean_target_ball_shot_share_after']:.1%}。")
    lines += ["", "被ダメージは回復で相殺せず、敗北も集計する。",
              "手数・衝突数は到達ステージや経路の差にも左右される。勝率と併せて判断する。",
              "ビルド完成は収集器定義のコア球をすべて所持した状態。未完成でも軸能力を使える場合はある。",
              f"各軸{target}ランでは1勝で{100/target:.0f}ポイント変わる。性能順位や面白さは確定できない。",
              "球種別使用数と能力イベントはpolicy_comparison.jsonのsamples_before/afterに保存。"]
    lines.append("stop_shield_absorbedは共通シールドの吸収記録であり、錨型固有の能力発動とは判定しない。")
    lines.append("原本all_runs.jsonlの当時の集計は保持した。休憩所件数は比較JSONで生ログから再計算している。")
    report = after_dir / datetime.now().astimezone().strftime("%m%d_MCP判断基準の修正前後比較.txt")
    report.write_text("\n".join(lines) + "\n", encoding="utf-8-sig")
    return str(report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    print(compare(args.before, args.after))
