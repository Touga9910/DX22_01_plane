"""実装済みの球種ごとの狙い方と、現在利用できる連鎖資源を公開する。"""

from __future__ import annotations

from typing import Any


# 記載対象は現在のランタイム実装だけとし、未実装の旧仕様は含めない。
# 座標、命中可否、成功率の判定はショット予測側に任せる。
BALL_AIM_GUIDANCE: dict[str, dict[str, str]] = {
    "player_standard": {
        "role": "汎用の直接攻撃",
        "activation": "固有能力の発動条件はない。",
        "effect": "基礎攻撃力による安定した直接ダメージ。",
        "target_pattern": "確実に命中、撃破、またはポケットへ押し込める敵。",
        "shot_path": "障害物の少ない直射と必要最小限のパワーを優先する。",
        "follow_up": "専用資源を使う球に明確な利益がない場面の安定手とする。",
        "avoid": "別の球でフィニッシャーや資源生成が成立する場面で、攻撃力だけで選ばない。",
    },
    "player_heavy": {
        "role": "重量スタック生成と押し出し",
        "activation": "ショット中の敵ごとの移動距離40ごとに1、1ショット最大10を生成する。",
        "effect": "重量スタックを蓄積する。Charging中の敵を、敵直径×enemy_action_rulesの現在倍率以上移動させるとChargeを妨害する。",
        "target_pattern": "移動方向に空間がある敵、後方に別の敵がいる敵、Charging中の敵。",
        "shot_path": "長距離移動する角度、または別の敵へ押し込むオフセンターを狙う。",
        "follow_up": "生成した重量スタックを連鎖衝撃球や衝突強化球で使う。",
        "avoid": "すぐ壁に遮られ、移動距離が40に届かない向き。",
    },
    "player_chain_impact": {
        "role": "重量スタック消費・範囲決着",
        "activation": "ショット最初の連鎖衝撃で重量スタックを最大5消費する。",
        "effect": "消費1ごとに追加ダメージ1。接触点の近い敵最大2体へ範囲攻撃。",
        "target_pattern": "能力半径内に価値の高い敵が1～2体いる中心敵。",
        "shot_path": "最初の直接命中を密集地点へ置く。",
        "follow_up": "重量スタックが多いほど最初の発動に集中して使う。",
        "avoid": "重量スタックがあるときの孤立敵への最初の接触。",
    },
    "player_collision_boost": {
        "role": "次ショットの敵同士衝突強化",
        "activation": "ショット開始時に重量スタック3以上があると3を消費し、次ショットへ予約する。",
        "effect": "次のショット中の敵同士衝突ダメージを1.25/1.5/1.75倍にする。現在ショットには予約分は適用しない。",
        "target_pattern": "次の手番で敵同士衝突を作れる盤面。",
        "shot_path": "現在の安全な命中を確保しつつ、主目的を次ショットの準備に置く。",
        "follow_up": "次の球で敵を別の敵へ押し込む。",
        "avoid": "重量スタック3未満、または次に敵同士衝突を起こせない状況。",
    },
    "player_lightweight": {
        "role": "重量微量生成と次回ノックバック準備",
        "activation": "ショット中に接触した異なる敵ごとに1回発動する。",
        "effect": "敵ごとに重量スタック1を生成し、その敵の次ショット最初の物理衝突の押し出しを強化する。+1以降は自球の速度損失も軽減する。",
        "target_pattern": "複数接触できる敵列、または次ショットで強く押したい敵。",
        "shot_path": "同じ敵の再接触より、異なる敵への連続接触を狙う。",
        "follow_up": "次ショットで準備済みの敵を別の敵や壁へ押す。",
        "avoid": "準備した敵をそのショットで撃破し、次回効果を失う狙い。",
    },
    "player_pierce": {
        "role": "貫通スタックと射線記録の生成",
        "activation": "1ショットで異なる敵を貫通するたびにスタック1と射線記録を生成する。",
        "effect": "貫通スタックは最大15、射線記録は新しい3本を保持し、後続のミニオン発射地点になる。",
        "target_pattern": "一直線上に複数の敵が並ぶ配置。",
        "shot_path": "手前の敵の中心を通し、速度を残して奥の敵へ続ける。",
        "follow_up": "スタックと射線記録を射線展開球またはミニオン展開球で使う。",
        "avoid": "敵1体だけの射線、または貫通後の残速で次の敵に届かない強さ。",
    },
    "player_refractive_pierce": {
        "role": "貫通生成・接触中心線屈折",
        "activation": "敵を貫通するたびに進行方向をその接触の中心線方向へ変える。",
        "effect": "方向変化後も貫通スタックと射線記録を生成する。",
        "target_pattern": "最初の敵を屈折点とし、中心線の先に次の敵がいる配置。",
        "shot_path": "各接触後に再度進行方向が変わることを見越す。",
        "follow_up": "屈折した射線記録もミニオン発射に使われる。",
        "avoid": "最初の中心線が壁や空白へ向かう接触。",
    },
    "player_trace_driver": {
        "role": "貫通スタック消費・ミニオン展開",
        "activation": "ショット開始時に貫通スタックを最大3/6/9消費する。",
        "effect": "消費数1/4/7で、新しい射線記録から順にミニオン1/2/3体を発射する。自球は貫通しない。",
        "target_pattern": "現在の新しい射線記録の先に敵がいる盤面。",
        "shot_path": "ミニオンは開始時に既存記録から自動発射される。自球は通常の最善経路を狙う。",
        "follow_up": "残ったスタックと新しい貫通で次の展開につなげる。",
        "avoid": "射線記録0本や敵へ向かない記録での消費。自球が記録をなぞって強化される機能は未実装。",
    },
    "player_pierce_finisher": {
        "role": "複数対象貫通決着",
        "activation": "そのショットで貫通済みのユニーク敵が1体以上いる後続接触で発動する。",
        "effect": "基本追加ダメージに、2体目以降のユニーク貫通数に応じた追加ダメージを得る。射線記録は消費しない。",
        "target_pattern": "異なる敵を複数体、同一ショットで貫通できる列。",
        "shot_path": "手前から奥へ安定して敵中心を通す。",
        "follow_up": "ユニーク敵数を増やし、後半の命中の追加ダメージを上げる。",
        "avoid": "単体敵、または速度不足で二体目に届かない射線。",
    },
    "player_minion_finisher": {
        "role": "貫通スタック全消費・ミニオン決着",
        "activation": "ショット開始時に現在の貫通スタックをすべて消費する。",
        "effect": "1/4/7スタックで最大3ミニオン、10以上でフィニッシャー強化。発射後の自球も貫通する。",
        "target_pattern": "最大3本の有効な射線記録と高い貫通スタックがある盤面。",
        "shot_path": "射線記録からのミニオンと、自球の貫通列を別の敵に通す。",
        "follow_up": "自球の貫通で次のスタックと射線記録を再生成する。",
        "avoid": "射線記録不足、低スタック、またはミニオン射線に敵がいない状況。",
    },
    "player_cushion_charge": {
        "role": "共有反射スタック生成",
        "activation": "有効な壁反射ごとに、強化段階の生成量を上限回数まで生成する。",
        "effect": "壁区画別ではない、盤面全体共有の反射スタックを増やす。",
        "target_pattern": "1回以上反射しながら最終的に敵へ届く経路。",
        "shot_path": "有効な反射回数を増やしつつ、最終接触を失わない。",
        "follow_up": "共有スタックをバウンド球または連続跳弾球で使う。",
        "avoid": "壁に当たらない直射。特定壁区画へスタックを保存する機能は未実装。",
    },
    "player_bounce": {
        "role": "共有反射スタック消費",
        "activation": "共有スタックがあるとき、壁反射ごとに1を消費し強化命中を1回予約する。",
        "effect": "反射後の速度を倍率で維持し、予約数だけ異なる敵へ追加ダメージを与える。",
        "target_pattern": "壁反射後に一体以上の敵へ届く経路。",
        "shot_path": "スタックを消費する反射を先に行い、その後で敵に命中させる。",
        "follow_up": "強化回数を同じ敵の再接触ではなく別の敵に配分する。",
        "avoid": "スタック0で強化を期待すること、または反射後に敵へ届かない経路。",
    },
    "player_ricochet_finisher": {
        "role": "共有反射スタック一括消費・ピンボール決着",
        "activation": "共有スタック6以上のとき、最初の敵接触で6を消費する。",
        "effect": "接触敵をピンボール対象にして自球は停止する。対象の壁・敵衝突数で追加ダメージ段階が上がる。",
        "target_pattern": "押し出し後に壁や複数敵へ繰り返し衝突できる敵。",
        "shot_path": "最初の敵を密集地帯または壁方向へ押し出す接触角を狙う。",
        "follow_up": "ピンボール対象の衝突数を増やす。",
        "avoid": "スタック6未満と孤立敵。先に複数壁でスタックを消費する能力ではない。",
    },
    "player_reflection_setup": {
        "role": "次ショットの反射スタック準備",
        "activation": "この球のショット終了時に準備量1/2/3を次ショットへ予約する。",
        "effect": "次ショットの最初の有効な壁反射時に、共有反射スタックを追加生成する。",
        "target_pattern": "次に壁反射を行う球を使える見込みがある盤面。",
        "shot_path": "現在は通常の最善経路を狙い、能力は終了時に予約する。",
        "follow_up": "次ショットで壁に当て、予約を共有スタックへ変換する。",
        "avoid": "次に反射経路を作れない、または即時に危険敵を処理すべき状況。",
    },
    "player_anchor": {
        "role": "敵錨スタック生成と接触停止",
        "activation": "敵またはブレイク球の接触時に停止する。終了時に接触対象が生存していると敵錨を付与する。",
        "effect": "基礎2に、プレイヤー錨スタック数と強化段階による追加量を加えた敵錨を付与する。",
        "target_pattern": "一度の接触で倒れず、後続の回収球が当てやすい敵。",
        "shot_path": "到達できる最小限のパワーで直接接触し、接触後の即停止を利用する。",
        "follow_up": "錨回収球または停止時シールド球で同じ敵の錨を回収する。",
        "avoid": "接触で対象を撃破し、終了時の錨付与が無効になるショット。",
    },
    "player_stop_shield": {
        "role": "敵錨回収・防御",
        "activation": "錨スタックを持つ敵へ接触すると、その敵から最大5を消費する。",
        "effect": "消費数に応じてシールドを得る。合計5消費ごとにプレイヤー錨も1生成する。",
        "target_pattern": "錨を多く持つ敵、特に次の敵攻撃ダメージが大きい場面。",
        "shot_path": "錨付きの対象へ確実に接触する。この球はアンカー停止せず、錨も新規付与しない。",
        "follow_up": "5:1変換で生成したプレイヤー錨をアンカー球や錨回収球の強化に使う。",
        "avoid": "錨を持たない敵への接触、またはシールド上限後の不要な追加消費。",
    },
    "player_anchor_finisher": {
        "role": "敵錨全回収・範囲決着",
        "activation": "ショット中の最初の錨付き敵への接触で、その敵の錨をすべて消費する。",
        "effect": "消費1ごとにプレイヤー錨数で2～4の追加ダメージ。4/5以上消費で周囲へ直接ボーナスの半分の範囲ダメージ。",
        "target_pattern": "錨が多く、能力半径内に他の敵が密集した対象。",
        "shot_path": "別の錨付き敵に先に触れず、目的対象へ直接命中させる。",
        "follow_up": "回収の5:1変換で得たプレイヤー錨を次の錨付与と決着倍率に使う。",
        "avoid": "錨が少ない孤立敵、または目的外の錨付き敵が経路を遮るショット。",
    },
    "player_origin_anchor": {
        "role": "始点復帰・敵錨準備",
        "activation": "ショット開始位置を保存し、次球準備時にその位置へ戻る。最初に接触した生存敵へ錨1を付与する。",
        "effect": "ノックバック伝達0で敵を押さず、始点復帰する。+2は次のAnchorカテゴリ球の敵錨付与量に1を予約する。",
        "target_pattern": "最初の接触後も生存する敵と、安全に保ちたいショット始点。",
        "shot_path": "押し出し0を考慮し、直接接触を確実にする。",
        "follow_up": "+2の予約は、敵錨を付与するアンカー球または始点固定球につなげる。",
        "avoid": "次に停止時シールド球や錨回収球を使い、錨付与ボーナスだけを消費すること。",
    },
}


def _nonnegative_int(value: Any) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def _resource_observation(
    definition_id: str,
    state: dict[str, Any],
) -> dict[str, Any] | None:
    synergies = state.get("ball_synergies", {})
    synergies = synergies if isinstance(synergies, dict) else {}
    table = state.get("table", {})
    table = table if isinstance(table, dict) else {}

    if definition_id in {
        "player_heavy", "player_chain_impact", "player_collision_boost",
        "player_lightweight",
    }:
        count = _nonnegative_int(
            synergies.get("heavy_stacks", synergies.get("heavy_collision_count"))
        )
        next_effects = synergies.get("next_shot_effects", {})
        next_effects = next_effects if isinstance(next_effects, dict) else {}
        return {
            "kind": "heavy_stacks",
            "count": count,
            "available": count > 0,
            "current_enemy_collision_multiplier": float(
                synergies.get("heavy_active_enemy_collision_multiplier", 1.0)
            ),
            "next_enemy_collision_multiplier": float(
                next_effects.get("heavy_enemy_collision_multiplier", 1.0)
            ),
        }

    if definition_id in {
        "player_pierce", "player_refractive_pierce", "player_trace_driver",
        "player_pierce_finisher", "player_minion_finisher",
    }:
        traces = synergies.get("pierce_traces", [])
        trace_count = len(traces) if isinstance(traces, list) else 0
        count = _nonnegative_int(synergies.get("pierce_stacks", trace_count))
        return {
            "kind": "pierce_stacks", "count": count,
            "trace_count": trace_count, "available": count > 0,
        }

    if definition_id in {
        "player_cushion_charge", "player_bounce",
        "player_ricochet_finisher", "player_reflection_setup",
    }:
        reflection = synergies.get("reflection", {})
        reflection = reflection if isinstance(reflection, dict) else {}
        if "shared_stack_count" in reflection:
            count = _nonnegative_int(reflection.get("shared_stack_count"))
        elif "reflection_stacks" in table:
            count = _nonnegative_int(table.get("reflection_stacks"))
        else:
            cushions = table.get("cushions", [])
            count = sum(
                _nonnegative_int(cushion.get("stack_count"))
                for cushion in cushions if isinstance(cushion, dict)
            ) if isinstance(cushions, list) else 0
        return {
            "kind": "shared_reflection_stacks", "count": count,
            "available": count > 0,
            "setup_pending": _nonnegative_int(reflection.get(
                "pending_setup_bonus", table.get("reflection_setup_pending")
            )),
            "setup_active": _nonnegative_int(reflection.get(
                "active_setup_bonus"
            )),
            "pinball_target_id": reflection.get("pinball_target_id"),
        }

    if definition_id in {
        "player_anchor", "player_stop_shield", "player_anchor_finisher",
        "player_origin_anchor",
    }:
        player_count = _nonnegative_int(synergies.get("player_anchor_stacks"))
        enemy_stacks = synergies.get("enemy_anchor_stacks", [])
        targets = [
            {
                "target_id": str(enemy.get("target_id", "")),
                "enemy_id": str(enemy.get("enemy_id", "")),
                "stacks": _nonnegative_int(enemy.get("stacks")),
            }
            for enemy in enemy_stacks
            if isinstance(enemy, dict) and _nonnegative_int(enemy.get("stacks")) > 0
        ] if isinstance(enemy_stacks, list) else []
        enemy_count = sum(target["stacks"] for target in targets)
        return {
            "kind": "anchor_stacks", "count": player_count + enemy_count,
            "player_count": player_count, "enemy_count": enemy_count,
            "enemy_targets": targets,
            "conversion_progress": _nonnegative_int(
                synergies.get("anchor_conversion_progress")
            ),
            "next_grant_bonus": _nonnegative_int(
                synergies.get("anchor_next_grant_bonus")
            ),
            "origin_return_pending": bool(
                synergies.get("origin_return_pending", False)
            ),
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
