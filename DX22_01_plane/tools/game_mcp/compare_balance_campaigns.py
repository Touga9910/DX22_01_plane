"""Offline, paired-seed before/after analysis. Never calls an external model."""
from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

BUILDS = ("standard", "heavy", "pierce", "bounce", "anchor")


def load_campaign(directory):
    manifest = json.loads((directory/"analysis.json").read_text(encoding="utf-8-sig"))
    runs, stages, raw_versions, signatures = [], [], set(), set()
    for batch in manifest["batches"]:
        for line in (Path(batch)/"all_runs.jsonl").read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            record = json.loads(line)
            raw = json.loads(Path(record["source_log"]).read_text(encoding="utf-8-sig"))
            context = raw["run_context"]
            if (context.get("run_combat_mode"), context.get("mcp_shot_policy"), context.get("controller_profile")) != ("legacy", "standard", "intermediate"):
                raise ValueError("Campaign uses different combat, shot-policy or player-level conditions")
            if context.get("dynamic_balance_enabled_at_start") is not False or not record["normal_end"]:
                raise ValueError("Only validated, normal-end fixed-balance runs can be compared")
            raw_versions.add(raw["balance_version"])
            signatures.add(record["configuration_signature"])
            build, seed = record["build_profile"], record["run_seed"]
            row = {"build": build, "seed": seed, "won": bool(record["final_boss_defeated"]),
                   "damage": 0, "pre_boss_damage": 0, "boss_reached": False,
                   "boss_won": False, "boss_shots": None, "boss_damage": None,
                   "break_starts": 0, "pocket_effects": 0, "pocket_finishers": 0}
            boss_stages = [stage for stage in raw["stages"] if stage["stage_type"] == "boss"]
            if len(boss_stages) > 1:
                raise ValueError("Expected at most one final-boss stage per run")
            for stage in raw["stages"]:
                result = stage["stage_result"]
                if result["result"] not in {"clear", "game_over"}:
                    raise ValueError("Unfinished/abandoned stage in an accepted run")
                damage = result.get("player_damage_taken")
                if damage is None:
                    raise ValueError("Missing stage damage must not be treated as zero")
                won = result["result"] == "clear"
                stage_row = {"build": build, "seed": seed, "stage_id": stage["stage_id"],
                             "stage_type": stage["stage_type"], "stage_index": stage["stage_index"],
                             "won": won, "damage": damage, "shots": result["total_shots"]}
                stages.append(stage_row)
                row["damage"] += damage
                if stage["stage_type"] == "boss":
                    row.update(boss_reached=True, boss_won=won,
                               boss_shots=result["total_shots"], boss_damage=damage)
                else:
                    row["pre_boss_damage"] += damage
            for event in raw["events"]:
                name, details = event["event_type"], event.get("details", {})
                row["break_starts"] += name == "boss_break_started"
                row["pocket_effects"] += name == "boss_pocket_resolved"
                row["pocket_finishers"] += name == "enemy_pocket_defeat" and details.get("stage_type") == "boss"
            if row["boss_won"] != row["won"]:
                raise ValueError("Final-boss stage result disagrees with the run result")
            runs.append(row)
    keys = [(r["seed"], r["build"]) for r in runs]
    if len(keys) != len(set(keys)) or len(signatures) != 1 or len(raw_versions) != 1:
        raise ValueError("Duplicate pairs or configuration/version changes within a campaign")
    return {"runs": runs, "stages": stages, "version": next(iter(raw_versions)),
            "signature": next(iter(signatures)), "pairs": set(keys)}


def average(values):
    values = list(values)
    return statistics.mean(values) if values else None


def summary(rows):
    n = len(rows)
    wins = sum(r["won"] for r in rows)
    return {"samples": n, "wins": wins, "losses": n-wins,
            "win_rate": wins/n if n else None,
            "mean_damage": average(r["damage"] for r in rows)}


def boss_summary(rows):
    reached = [r for r in rows if r["boss_reached"]]
    wins = [r for r in reached if r["boss_won"]]
    shots = [r["boss_shots"] for r in wins]
    return {"run_samples": len(rows), "reached": len(reached), "wins": len(wins),
            "win_rate_after_reaching": len(wins)/len(reached) if reached else None,
            "mean_shots_to_kill": average(shots),
            "median_shots_to_kill": statistics.median(shots) if shots else None,
            "min_shots_to_kill": min(shots) if shots else None,
            "max_shots_to_kill": max(shots) if shots else None,
            "one_shot_wins": sum(value == 1 for value in shots),
            "mean_shots_all_attempts": average(r["boss_shots"] for r in reached),
            "mean_damage_all_attempts": average(r["boss_damage"] for r in reached),
            "break_starts": sum(r["break_starts"] for r in reached),
            "pocket_effects": sum(r["pocket_effects"] for r in reached),
            "pocket_finishers": sum(r["pocket_finishers"] for r in reached)}


def delta(after, before):
    return after-before if after is not None and before is not None else None


def group_comparison(before, after, keys):
    left, right = defaultdict(list), defaultdict(list)
    for row in before:
        left[tuple(row[k] for k in keys)].append(row)
    for row in after:
        right[tuple(row[k] for k in keys)].append(row)
    results = []
    for key in sorted(set(left)|set(right)):
        a, b = summary(left[key]), summary(right[key])
        results.append({**dict(zip(keys, key)), "before": a, "after": b,
                        "win_rate_delta": delta(b["win_rate"], a["win_rate"]),
                        "mean_damage_delta": delta(b["mean_damage"], a["mean_damage"])})
    return results


def write_table(path, rows):
    if not rows:
        return
    flat = []
    for row in rows:
        item = {}
        for key, value in row.items():
            if isinstance(value, dict):
                item.update({key+"_"+subkey: subvalue for subkey, subvalue in value.items()})
            else:
                item[key] = value
        flat.append(item)
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(flat[0]))
        writer.writeheader()
        writer.writerows(flat)


def collection_target(directory, target):
    """A user may lower the target of an already-running collector."""
    override = Path(directory)/"collection_target.json"
    if override.exists():
        value = json.loads(override.read_text(encoding="utf-8-sig"))["valid_runs_per_build"]
        if type(value) is not int or value <= 0:
            raise ValueError("Invalid collection target override")
        return value
    return target


def compare_campaigns(baseline, current, target=10):
    baseline, current = Path(baseline).resolve(), Path(current).resolve()
    target = collection_target(current, target)
    before, after = load_campaign(baseline), load_campaign(current)
    if (before['version'], after['version']) in (('B0007', 'B0008'), ('B0008', 'B0009'), ('B0009', 'B0010'), ('B0010','B0011'), ('B0011','B0012'), ('B0012','B0013')):
        from compare_shield_campaigns import compare_campaigns as compare_shield
        return compare_shield(baseline, current, target)
    expected = Counter(dict.fromkeys(BUILDS, target))
    if Counter(r["build"] for r in before["runs"]) != expected or Counter(r["build"] for r in after["runs"]) != expected:
        raise ValueError("Both campaigns must have exactly the target valid runs for every build")
    if before["pairs"] != after["pairs"]:
        raise ValueError("Before/after comparisons require identical seed/build pairs")
    if before["version"] != "B0006" or after["version"] != "B0007":
        raise ValueError("This comparison requires the archived B0006 and the new B0007 campaign")
    builds = group_comparison(before["runs"], after["runs"], ("build",))
    builds.sort(key=lambda row: BUILDS.index(row["build"]))
    stage_ids = group_comparison(before["stages"], after["stages"], ("stage_id", "stage_type"))
    build_stages = group_comparison(before["stages"], after["stages"], ("build", "stage_id", "stage_type"))
    build_stages.sort(key=lambda row: (BUILDS.index(row["build"]), row["stage_id"], row["stage_type"]))
    battle_orders = group_comparison(before["stages"], after["stages"], ("stage_index",))
    bosses, paired = [], []
    for build in BUILDS:
        left = [r for r in before["runs"] if r["build"] == build]
        right = [r for r in after["runs"] if r["build"] == build]
        a, b = boss_summary(left), boss_summary(right)
        bosses.append({"build": build, "before": a, "after": b,
                       "mean_shots_to_kill_delta": delta(b["mean_shots_to_kill"], a["mean_shots_to_kill"])})
        indexed = {r["seed"]: r for r in left}
        paired.append({"build": build, "samples": len(right),
                       "improved_loss_to_win": sum(not indexed[r["seed"]]["won"] and r["won"] for r in right),
                       "regressed_win_to_loss": sum(indexed[r["seed"]]["won"] and not r["won"] for r in right),
                       "mean_pre_boss_damage_before": average(r["pre_boss_damage"] for r in left),
                       "mean_pre_boss_damage_after": average(r["pre_boss_damage"] for r in right)})
    a_boss, b_boss = boss_summary(before["runs"]), boss_summary(after["runs"])
    before_index = {(r["seed"], r["build"]): r for r in before["runs"]}
    reach_changes = sum(r["boss_reached"] != before_index[(r["seed"], r["build"])]["boss_reached"] for r in after["runs"])
    result = {"created_at": datetime.now().astimezone().isoformat(),
              "before_directory": str(baseline), "after_directory": str(current),
              "before_version": before["version"], "after_version": after["version"],
              "before_signature": before["signature"], "after_signature": after["signature"],
              "paired_seed_build_pairs": len(after["pairs"]),
              "overall_before": summary(before["runs"]), "overall_after": summary(after["runs"]),
              "builds": builds, "stage_ids": stage_ids, "build_stages": build_stages,
              "battle_orders": battle_orders, "bosses": bosses,
              "boss_overall_before": a_boss, "boss_overall_after": b_boss,
              "paired_outcomes": paired,
              "paired_boss_reach_changed_count": reach_changes,
              "definitions": {"stage_win_rate": "wins / reached-and-finished battles; unreached is not a loss",
                              "mean_damage": "recorded HP damage, including losses, without subtracting healing",
                              "shots_to_kill": "player shots in successful final-boss stages only; failures shown separately"}}
    (current/"B0006_vs_B0007_comparison.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    for name, rows in (("builds", builds), ("stage_ids", stage_ids), ("build_stages", build_stages),
                       ("battle_orders", battle_orders), ("bosses", bosses)):
        write_table(current/("B0006_vs_B0007_"+name+".csv"), rows)
    pct = lambda value: f"{value:.1%}" if value is not None else "未到達・未測定"
    num = lambda value: f"{value:.2f}" if value is not None else "未到達・未測定"
    lines = [datetime.now().astimezone().strftime("作成日：%Y年%m月%d日"),
             "B0006とB0007の100ラン比較分析", "",
             f"各軸{target}有効ラン、共通seed/build {len(after['pairs'])}組を照合。各版の設定フィンガープリントはそれぞれ1種類。",
             "中級・legacy・ショット方針standard・動的バランスOFF。", "",
             "全体勝率："+pct(result['overall_before']['win_rate'])+" → "+pct(result['overall_after']['win_rate']),
             "最終ボス到達後の勝率："+pct(a_boss['win_rate_after_reaching'])+" → "+pct(b_boss['win_rate_after_reaching']),
             "最終ボス撃破ショット数（勝利戦のみの平均）："+num(a_boss['mean_shots_to_kill'])+" → "+num(b_boss['mean_shots_to_kill']),
             f"最終ボス1ショット勝利数：{a_boss['one_shot_wins']} → {b_boss['one_shot_wins']}",
             "", "軸別：勝利数/ラン数、勝率、1ラン平均被ダメージ（修正前 → 修正後）"]
    for row in builds:
        a, b = row['before'], row['after']
        lines.append(f"{row['build']}: {a['wins']}/{a['samples']} → {b['wins']}/{b['samples']}, {pct(a['win_rate'])} → {pct(b['win_rate'])}, 被ダメージ {num(a['mean_damage'])} → {num(b['mean_damage'])}")
    lines += ["", "最終ボス：到達数、勝利数、撃破ショット数の平均・中央値・範囲（勝利戦のみ）"]
    for row in bosses:
        a, b = row['before'], row['after']
        lines.append(f"{row['build']}: 到達 {a['reached']} → {b['reached']}, 勝利 {a['wins']} → {b['wins']}, 平均 {num(a['mean_shots_to_kill'])} → {num(b['mean_shots_to_kill'])}, 修正後中央値 {num(b['median_shots_to_kill'])}, 範囲 {b['min_shots_to_kill']}～{b['max_shots_to_kill']}, 失敗も含む被ダメージ平均 {num(a['mean_damage_all_attempts'])} → {num(b['mean_damage_all_attempts'])}")
    lines += ["", "ステージID別：勝利/到達、勝率、平均被ダメージ（修正前 → 修正後）"]
    for row in stage_ids:
        a, b = row['before'], row['after']
        lines.append(f"{row['stage_id']} ({row['stage_type']}): {a['wins']}/{a['samples']} → {b['wins']}/{b['samples']}, {pct(a['win_rate'])} → {pct(b['win_rate'])}, 被ダメージ {num(a['mean_damage'])} → {num(b['mean_damage'])}")
    lines += ["", "各軸×ステージの比較"]
    for row in build_stages:
        a, b = row['before'], row['after']
        lines.append(f"{row['build']} / {row['stage_id']}: 勝率 {pct(a['win_rate'])} → {pct(b['win_rate'])} (n={a['samples']} → {b['samples']}), 被ダメージ {num(a['mean_damage'])} → {num(b['mean_damage'])}")
    lines += ["", "戦闘順別の比較（通常戦・中ボス・ボスが混在するため、ID別と併読）"]
    for row in battle_orders:
        a, b = row['before'], row['after']
        lines.append(f"第{row['stage_index']}戦: {a['wins']}/{a['samples']} → {b['wins']}/{b['samples']}, 勝率 {pct(a['win_rate'])} → {pct(b['win_rate'])}, 被ダメージ {num(a['mean_damage'])} → {num(b['mean_damage'])}")
    top = max(builds, key=lambda row: row['after']['win_rate'])
    dropped = min(builds, key=lambda row: row['win_rate_delta'])
    toughest = sorted([row for row in stage_ids if row['after']['samples']],
                       key=lambda row: row['after']['mean_damage'], reverse=True)[:3]
    lines += ["", "共通seedごとの変化とボス到達前の消耗"]
    for row in paired:
        lines.append(f"{row['build']}: 敗北→勝利 {row['improved_loss_to_win']}組、勝利→敗北 {row['regressed_win_to_loss']}組、ボス以外の合計被ダメージ平均 {num(row['mean_pre_boss_damage_before'])} → {num(row['mean_pre_boss_damage_after'])}")
    lines += ["", "結果の読み取り",
              f"修正後の最高勝率は{top['build']}の{pct(top['after']['win_rate'])}。勝率変化が最も低いのは{dropped['build']}で{dropped['win_rate_delta']*100:+.1f}ポイント。",
              f"ボス到達の有無が修正前後で変わったseed/buildは{reach_changes}/{len(after['pairs'])}組。到達までの変化とボス戦自体の変化を区別する。",
              "修正後の平均被ダメージ上位ステージ："+"、".join(f"{r['stage_id']} {num(r['after']['mean_damage'])}" for r in toughest)+"。",
              f"B0007ではボス戦中のBreak開始が計{b_boss['break_starts']}回、ポケット効果が計{b_boss['pocket_effects']}回、ポケットによるとどめが計{b_boss['pocket_finishers']}回記録された。",
              ("修正前のボス一撃勝利は、今回のB0007では観測されなかった。" if b_boss['one_shot_wins']==0 else f"B0007でもボスの1ショット勝利が{b_boss['one_shot_wins']}件ある。旧ポケット即死と同じ原因かは個別ログの確認が必要。"),
              "1ショット勝利の減少だけでなく、撃破ショット数、Break・ポケットの利用、到達後勝率を合わせて修正の効果を判断する。",
              "各軸20ランでは1勝で5ポイント動く。勝率差だけで軸の性能順位を確定しない。到達戦闘を分母とするため、後半の平均値には生存者・経路の偏りがある。",
              "最終ボスの失敗戦は撃破ショット数の平均に含めず、到達数と勝利数を必ず併記した。",
              "旧B0006の最初のStandard/Heavy各1ランは報酬選択修正前、最初のPierceは再開を挟んだ。旧データを上書きせずそのまま比較した。",
              "CSVはさらに詳しい数値、JSONは定義と監査情報を含む。"]
    filename = datetime.now().astimezone().strftime("%m%d_B0006とB0007の100ラン比較分析.txt")
    report = current/filename
    report.write_text("\n".join(lines)+"\n", encoding="utf-8-sig")
    return str(report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--target", type=int, default=10)
    args = parser.parse_args()
    print(compare_campaigns(args.baseline, args.current, args.target))
