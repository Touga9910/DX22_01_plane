"""球種ごとの狙い方と、現在の盤面で使える連鎖資源を公開する。"""

from __future__ import annotations

from typing import Any


# ボールの説明はゲーム内の PlayerBallText と同じ役割・効果に合わせる。
# 座標や成功率の断定はショット予測側に任せ、ここでは狙う配置を示す。
BALL_AIM_GUIDANCE: dict[str, dict[str, str]] = {
    "player_standard": {
        "role": "汎用",
        "target_pattern": "直射で確実に当てられる敵、またはポケットへ押し込める敵。",
        "shot_path": "障害物の少ない経路から命中と撃破を優先する。",
        "follow_up": "特別な準備がない場面の安定した一打として使う。",
    },
    "player_heavy": {
        "role": "重量カウント生成",
        "target_pattern": "後方や近くに別の敵がいる敵。",
        "shot_path": "敵を別の敵へ押し込む角度を狙い、敵同士の衝突を起こす。",
        "follow_up": "増やした重量カウントを連鎖衝撃球の決着に使う。",
    },
    "player_chain_impact": {
        "role": "重量カウント利用・決着",
        "target_pattern": "周囲にも敵がいる密集地点の敵。",
        "shot_path": "直接命中を起点に、連鎖衝撃の範囲へ複数の敵を入れる。",
        "follow_up": "重量カウントがあると決着ダメージが伸びる。",
    },
    "player_pierce": {
        "role": "貫通痕生成",
        "target_pattern": "一直線上に複数の敵が並ぶ配置。",
        "shot_path": "手前の敵を貫通し、奥の敵へ続く直線を通す。",
        "follow_up": "残した貫通痕を後続の貫通系ボールに利用させる。",
    },
    "player_refractive_pierce": {
        "role": "貫通痕生成・屈折",
        "target_pattern": "最初の敵の先で、屈折した進路にも敵がいる配置。",
        "shot_path": "最初の接触後に自球が曲がる方向まで見込んで狙う。",
        "follow_up": "屈折後の軌道にも貫通痕を残して次の球へつなぐ。",
    },
    "player_trace_driver": {
        "role": "貫通痕利用・展開",
        "target_pattern": "既存の貫通痕に沿って進み、先の壁へ届く経路。",
        "shot_path": "貫通痕をなぞってから反射し、曲がった先に新しい痕を伸ばす。",
        "follow_up": "新しい痕を貫通決着球の通り道にする。",
    },
    "player_pierce_finisher": {
        "role": "貫通痕利用・決着",
        "target_pattern": "貫通痕の先に、異なる敵が複数並ぶ配置。",
        "shot_path": "先に痕を利用し、同じショットで別々の敵を連続して貫く。",
        "follow_up": "痕の利用と複数対象への貫通を決着ダメージへ変える。",
    },
    "player_cushion_charge": {
        "role": "クッション蓄積生成",
        "target_pattern": "後続の反発球が通りやすい壁区画。",
        "shot_path": "狙った壁区画へ当て、クッションスタックを蓄積する。",
        "follow_up": "蓄積した区画へバウンド球や連続跳弾球を通す。",
    },
    "player_bounce": {
        "role": "クッション蓄積利用",
        "target_pattern": "蓄積した壁区画を経由して狙える敵。",
        "shot_path": "壁反射でスタックを使い、加速と攻撃上昇を得てから敵へ当てる。",
        "follow_up": "蓄積がない場合も、反射で敵の裏側へ回り込める経路を探す。",
    },
    "player_ricochet_finisher": {
        "role": "クッション蓄積利用・決着",
        "target_pattern": "複数の蓄積した壁区画を経由した先にいる敵。",
        "shot_path": "一打で複数の壁区画を反射し、スタックを重ねて使ってから命中させる。",
        "follow_up": "同じショットのスタック利用回数を決着攻撃へ変える。",
    },
    "player_anchor": {
        "role": "錨スタック生成",
        "target_pattern": "接触後に自球が近くで止まり、周囲にも敵がいる位置。",
        "shot_path": "敵へ力を伝えた後の停止位置を狙い、錨スタックを付ける。",
        "follow_up": "錨回収球が接触できる敵に印を残す。",
    },
    "player_stop_shield": {
        "role": "錨スタック生成・防御",
        "target_pattern": "敵の近くで早く停止できる接触位置。",
        "shot_path": "長い走行より停止を優先し、次の敵攻撃に備えるシールドを張る。",
        "follow_up": "停止で付けた錨スタックも後続の回収球へつなぐ。",
    },
    "player_anchor_finisher": {
        "role": "錨スタック回収・決着",
        "target_pattern": "錨スタックを持つ敵、特に周囲にも敵がいる敵。",
        "shot_path": "印のある敵へ直接当て、回収したスタックを最初の接触に集中する。",
        "follow_up": "スタックが十分あれば周囲への追加ダメージも狙う。",
    },
}


def _nonnegative_int(value: Any) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def _resource_observation(definition_id: str, state: dict[str, Any]) -> dict[str, Any] | None:
    synergies = state.get("ball_synergies", {})
    synergies = synergies if isinstance(synergies, dict) else {}
    table = state.get("table", {})
    table = table if isinstance(table, dict) else {}
    if definition_id == "player_chain_impact":
        count = _nonnegative_int(synergies.get("heavy_collision_count"))
        return {"kind": "heavy_collision_count", "count": count, "available": count > 0}
    if definition_id in {"player_trace_driver", "player_pierce_finisher"}:
        traces = synergies.get("pierce_traces", [])
        count = len(traces) if isinstance(traces, list) else 0
        return {"kind": "pierce_traces", "count": count, "available": count > 0}
    if definition_id in {"player_bounce", "player_ricochet_finisher"}:
        cushions = table.get("cushions", [])
        count = sum(
            _nonnegative_int(cushion.get("stack_count"))
            for cushion in cushions if isinstance(cushion, dict)
        ) if isinstance(cushions, list) else 0
        return {"kind": "cushion_stacks", "count": count, "available": count > 0}
    if definition_id == "player_anchor_finisher":
        player_count = _nonnegative_int(synergies.get("player_anchor_stacks"))
        enemy_stacks = synergies.get("enemy_anchor_stacks", [])
        enemy_count = sum(
            _nonnegative_int(enemy.get("stacks"))
            for enemy in enemy_stacks if isinstance(enemy, dict)
        ) if isinstance(enemy_stacks, list) else 0
        return {
            "kind": "anchor_stacks", "count": player_count + enemy_count,
            "player_count": player_count, "enemy_count": enemy_count,
            "available": player_count + enemy_count > 0,
        }
    return None


def attach_ball_aim_guidance(state: dict[str, Any]) -> None:
    """候補・デッキ・カタログの各球へ、状態を変えない狙いの指標を付ける。"""
    for collection_name in ("offered_balls", "deck_balls", "catalog_balls"):
        balls = state.get(collection_name, [])
        if not isinstance(balls, list):
            continue
        for ball in balls:
            if not isinstance(ball, dict):
                continue
            definition_id = str(ball.get("definition_id", ""))
            guidance = BALL_AIM_GUIDANCE.get(definition_id)
            if guidance is None:
                continue
            ball["aim_guidance"] = dict(guidance)
            resource = _resource_observation(definition_id, state)
            if resource is not None:
                ball["aim_guidance"]["current_resource"] = resource
