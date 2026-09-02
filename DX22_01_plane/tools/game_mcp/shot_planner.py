from __future__ import annotations

import copy
import json
import math
import random
import threading
from pathlib import Path
from typing import Any

from bridge_store import GameBridgeError


VALID_PLAYER_LEVELS = ("beginner", "intermediate", "advanced")
VALID_SHOT_TYPES = ("direct", "bank")
VALID_SHOT_GOALS = ("auto", "damage", "pocket")
_HUMAN_ERROR_RANDOM = random.SystemRandom()


def load_player_profiles(path: Path) -> dict[str, dict[str, Any]]:
    try:
        with path.open("r", encoding="utf-8") as file:
            document = json.load(file)
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"プレイヤーレベル設定を読み込めません: {path}"
        ) from error

    levels = document.get("levels")
    if not isinstance(levels, dict):
        raise RuntimeError(
            "プレイヤーレベル設定にlevelsオブジェクトが必要です。"
        )

    profiles: dict[str, dict[str, Any]] = {}
    for level in VALID_PLAYER_LEVELS:
        profile = levels.get(level)
        if not isinstance(profile, dict):
            raise RuntimeError(
                f"プレイヤーレベル設定に{level}がありません。"
            )
        instructions = profile.get("instructions")
        if (
            not isinstance(instructions, list)
            or not instructions
            or not all(
                isinstance(instruction, str) and instruction
                for instruction in instructions
            )
        ):
            raise RuntimeError(
                f"{level}.instructionsは空でない文字列配列にしてください。"
            )
        profiles[level] = copy.deepcopy(profile)
    return profiles


class PlayerProfileController:
    def __init__(
        self,
        profiles: dict[str, dict[str, Any]],
        initial_level: str,
    ) -> None:
        self._profiles = profiles
        self._lock = threading.Lock()
        self._level = ""
        self.set_level(initial_level)

    @property
    def level(self) -> str:
        with self._lock:
            return self._level

    def set_level(self, level: str) -> dict[str, Any]:
        if level not in self._profiles:
            raise GameBridgeError(
                "player_levelはbeginner、intermediate、advanced"
                "のいずれかを指定してください。"
            )
        with self._lock:
            self._level = level
            return self._snapshot_unlocked()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return self._snapshot_unlocked()

    def _snapshot_unlocked(self) -> dict[str, Any]:
        profile = copy.deepcopy(self._profiles[self._level])
        return {
            "player_level": self._level,
            **profile,
        }


def build_server_instructions(profile: dict[str, Any]) -> str:
    level_instructions = "".join(
        f"{index + 1}. {instruction}"
        for index, instruction in enumerate(profile["instructions"])
    )
    return (
        "最初にget_game_stateを呼び、available_actionsに含まれる"
        "操作だけを使用してください。"
        "ショットでは必ず生存中の敵をenemies[].target_idで"
        "指定してください。enemy_idは敵の種類名であり、"
        "同じ種類の敵が複数いる場合に個体を識別できないため、"
        "標的指定には使用しないでください。"
        "空間上の任意方向を指定したり、敵がいない方向へ撃ってはいけません。"
        "戦闘中はtable.pockets、pocket_rules、"
        "enemies[].pocket_finisher_eligibleを比較してください。"
        "get_game_stateのshot_tacticsで敵ごとの通常攻撃と"
        "ポケット経路の比較を確認し、通常ランでは"
        "fire_shotのshot_goal=autoを使って最新状態で再評価させます。"
        "damageまたはpocketを直接指定するのは行動固定の"
        "検証時だけにしてください。フィニッシュ条件内の敵は"
        "ポケット撃破を優先候補にし、条件外でも次の敵攻撃が"
        "危険ならポケットへ入れてそのターンの攻撃を防ぐ"
        "選択肢を検討してください。プレイヤーボールも落ちる経路は"
        "最大HPの4%ダメージを受けるため、残HPと利得を比較してください。"
        "ショット後はボールが停止してfire_shotが再び利用可能になるまで"
        "状態を確認してください。"
        "ステージ選択ではplayer、deck_balls、relics、"
        "stage_choice.need_signalsを比較し、money、new_ball、"
        "ball_upgrade、hp_recovery、relicの5つを現在欲しい順に"
        "並べてchoose_destinationのwanted_rewardsに全て指定してください。"
        "サーバーも実行直前のneed_signalsから順位を再計算するため、"
        "以前の固定順位をそのまま使い続けないでください。"
        "第1希望に合うノードがない場合はサーバーが次順位を試します。"
        "HPが25以下で休憩所が提示された場合は休憩所を最優先し、"
        "購入またはボール削除ができないショップは選ばないでください。"
        "ショップでは入店後の最新状態を確認し、"
        "relic_policyに従って低HPなら回復・防御系、"
        "平均攻撃力が不足しているなら攻撃系レリックを"
        "優先してください。"
        "get_game_stateのmcp_controlに現在のplayer_levelと行動方針が"
        "含まれるため、毎回それに従ってください。"
        "ショットにはサーバー側でプレイヤーレベル別の"
        "距離適応パワーと、少量の照準・パワー誤差が自動的に"
        "加わります。通常はfire_shotのpower_mode=autoを使用し、"
        "manualはパワー固定の検証時だけにしてください。"
        "dynamic_balanceには自動難易度の現在レベル、次戦の敵補正、"
        "直近の評価理由が含まれます。自動評価はゲーム側が行うため、"
        "ユーザーから依頼されていない限り戦闘ごとに手動変更しないでください。"
        "ユーザーが明示していない新規ラン開始やボール削除は"
        "行わないでください。"
        f"起動時のプレイヤーレベルは{profile['label']}です。"
        f"{level_instructions}"
    )


def ensure_enemy_target_ids(
    state: dict[str, Any],
) -> dict[str, Any]:
    enemies = state.get("enemies")
    if not isinstance(enemies, list):
        return state
    for index, enemy in enumerate(enemies):
        if isinstance(enemy, dict):
            enemy["target_id"] = f"enemy:{index}"
    return state


def resolve_target_id_argument(
    target_id: str,
    target_enemy_id: str,
) -> str:
    preferred = target_id.strip()
    legacy = target_enemy_id.strip()
    if preferred and legacy and preferred != legacy:
        raise GameBridgeError(
            "target_idとtarget_enemy_idに異なる値を"
            "同時指定しないでください。"
        )
    resolved = preferred or legacy
    if not resolved:
        raise GameBridgeError(
            "enemies[].target_idをtarget_idに指定してください。"
            "旧ツール定義では同じ値をtarget_enemy_idへ指定できます。"
        )
    return resolved


def evaluate_tactical_shot_options(
    state: dict[str, Any],
    target_id: str,
    power: float,
    profile: dict[str, Any],
    pocket_index: int = -1,
) -> dict[str, Any]:
    """直接ダメージと、実行可能なすべてのポケット経路を比較する。

    第2の物理エンジンを作るのではなく、意図的に概算として扱う。
    ゲームがすでに公開している、フィニッシュ可否、防止できる敵攻撃、
    接近形状、移動距離、接触後のプレイヤー経路がポケットと重なる危険性を採点する。
    """
    ensure_enemy_target_ids(state)
    target = _find_live_enemy(state, target_id)
    player = state.get("player", {})
    if not isinstance(player, dict):
        player = {}
    player_position = _xz_position(
        player.get("position"),
        "player.position",
    )
    target_position = _xz_position(
        target.get("position"),
        "target.position",
    )
    player_radius = _nonnegative_finite_number(
        player.get("radius", 0.0),
        "player.radius",
    )
    target_radius = _nonnegative_finite_number(
        target.get("radius", 0.0),
        "target.radius",
    )
    shot_power = _finite_number(power, "power")
    policy = profile.get("pocket_tactics", {})
    if not isinstance(policy, dict):
        policy = {}
    minimum_alignment = float(policy.get("minimum_alignment", 0.45))
    minimum_advantage = float(
        policy.get("minimum_score_advantage", 1.0)
    )
    self_risk_weight = float(policy.get("self_pocket_risk_weight", 2.5))
    control_attack_weight = float(
        policy.get("control_attack_weight", 3.0)
    )
    finisher_bonus = float(policy.get("finisher_bonus", 6.0))

    player_hp = max(0.0, float(player.get("current_hp", 0.0)))
    player_max_hp = max(1.0, float(player.get("max_hp", 1.0)))
    player_attack = max(1.0, float(player.get("attack", 1.0)))
    enemy_hp = max(0.0, float(target.get("hp", 0.0)))
    enemy_defense = max(0.0, float(target.get("defense", 0.0)))
    enemy_attack = max(0.0, float(target.get("attack", 0.0)))
    expected_damage = max(1.0, player_attack - enemy_defense)
    damage_defeats = expected_damage >= enemy_hp
    total_incoming_attack = sum(
        max(0.0, float(enemy.get("attack", 0.0)))
        for enemy in state.get("enemies", [])
        if isinstance(enemy, dict)
        and not enemy.get("defeated", False)
        and not enemy.get("pocketed", False)
        and enemy.get("can_attack_this_turn", True)
    )
    damage_score = min(enemy_hp, expected_damage)
    if damage_defeats:
        damage_score += enemy_attack * control_attack_weight

    pocket_damage = max(
        1.0,
        float(
            state.get("pocket_rules", {})
            .get("player", {})
            .get("damage_amount", math.ceil(player_max_hp * 0.04))
        ),
    )
    hp_urgency = min(
        3.0,
        total_incoming_attack / max(1.0, player_hp),
    )
    low_hp_risk_multiplier = 1.0 + max(
        0.0,
        0.5 - player_hp / player_max_hp,
    ) * 4.0

    pockets = state.get("table", {}).get("pockets", [])
    if not isinstance(pockets, list):
        pockets = []
    requested_pockets = [
        pocket
        for pocket in pockets
        if isinstance(pocket, dict)
        and (
            pocket_index < 0
            or int(pocket.get("index", -1)) == pocket_index
        )
    ]
    if pocket_index >= 0 and not requested_pockets:
        raise GameBridgeError(
            "pocket_indexがtable.pocketsの範囲外です。"
        )

    incoming_direction = _normalized_direction(
        player_position,
        target_position,
    )
    contact_distance = max(
        0.1,
        player_radius + target_radius - 0.05,
    )
    pocket_options: list[dict[str, Any]] = []
    for pocket in requested_pockets:
        pocket_position = _xz_position(
            pocket.get("position"),
            "table.pockets[].position",
        )
        push_direction = _normalized_direction(
            target_position,
            pocket_position,
        )
        alignment = max(0.0, _dot(incoming_direction, push_direction))
        enemy_travel_distance = _distance(
            target_position,
            pocket_position,
        )
        estimated_reach = shot_power * 11.0 + 8.0
        reachable = enemy_travel_distance <= estimated_reach
        geometry_quality = alignment * min(
            1.0,
            estimated_reach / max(1.0, enemy_travel_distance),
        )
        feasible = reachable and alignment >= minimum_alignment
        contact_point = _subtract(
            target_position,
            _scale(push_direction, contact_distance),
        )
        tangent = _subtract(
            incoming_direction,
            _scale(push_direction, _dot(incoming_direction, push_direction)),
        )
        tangent_length = math.hypot(tangent[0], tangent[1])
        continuation_end = contact_point
        if tangent_length > 0.0001:
            tangent_direction = (
                tangent[0] / tangent_length,
                tangent[1] / tangent_length,
            )
            continuation_end = _add(
                contact_point,
                _scale(
                    tangent_direction,
                    shot_power * 4.0 * tangent_length,
                ),
            )
        self_pocket_risk = max(
            _path_pocket_risk(
                player_position,
                contact_point,
                player_radius,
                pockets,
            ),
            _path_pocket_risk(
                contact_point,
                continuation_end,
                player_radius,
                pockets,
            ),
        )
        is_finisher = bool(
            target.get("pocket_finisher_eligible", False)
        )
        if is_finisher:
            tactical_benefit = (
                enemy_hp
                + enemy_attack * control_attack_weight
                + finisher_bonus
            )
            outcome = "finisher"
        else:
            tactical_benefit = enemy_attack * control_attack_weight * (
                1.0 + hp_urgency
            )
            outcome = "skip_attack_and_queue_return"
        risk_penalty = (
            self_pocket_risk
            * pocket_damage
            * self_risk_weight
            * low_hp_risk_multiplier
        )
        distance_penalty = max(
            0.0,
            enemy_travel_distance - shot_power * 6.0,
        ) * 0.05
        score = (
            tactical_benefit * geometry_quality
            - risk_penalty
            - distance_penalty
        )
        pocket_options.append({
            "pocket_index": int(pocket.get("index", -1)),
            "outcome": outcome,
            "feasible": feasible,
            "score": round(score, 4),
            "geometry_quality": round(geometry_quality, 4),
            "alignment": round(alignment, 4),
            "enemy_travel_distance": round(enemy_travel_distance, 4),
            "estimated_reach": round(estimated_reach, 4),
            "prevented_attack": enemy_attack,
            "self_pocket_risk": round(self_pocket_risk, 4),
            "self_pocket_damage": pocket_damage,
        })

    feasible_options = [
        option for option in pocket_options if option["feasible"]
    ]
    best_pocket = max(
        feasible_options,
        key=lambda option: option["score"],
        default=None,
    )
    choose_pocket = (
        best_pocket is not None
        and best_pocket["score"] >= damage_score + minimum_advantage
    )
    if choose_pocket:
        reason = (
            "pocket_finisher_has_higher_value"
            if best_pocket["outcome"] == "finisher"
            else "pocket_control_prevents_dangerous_attack"
        )
    elif best_pocket is None:
        reason = "no_feasible_pocket_route"
    elif best_pocket["self_pocket_risk"] >= 0.5:
        reason = "self_pocket_risk_is_too_high"
    else:
        reason = "damage_has_equal_or_higher_value"
    return {
        "recommended_goal": "pocket" if choose_pocket else "damage",
        "recommended_pocket_index": (
            best_pocket["pocket_index"] if best_pocket is not None else -1
        ),
        "reason": reason,
        "damage_option": {
            "score": round(damage_score, 4),
            "expected_damage": round(expected_damage, 4),
            "expected_defeat": damage_defeats,
        },
        "best_pocket_option": best_pocket,
        "pocket_options": pocket_options,
        "total_incoming_attack": total_incoming_attack,
        "player_hp": player_hp,
        "player_pocket_damage": pocket_damage,
    }


def build_tactical_shot_context(
    state: dict[str, Any],
    profile: dict[str, Any],
) -> dict[str, Any]:
    recommended_power = profile.get("recommended_power", {})
    if not isinstance(recommended_power, dict):
        recommended_power = {}
    minimum_power = float(recommended_power.get("min", 3.0))
    maximum_power = float(recommended_power.get("max", 7.0))
    evaluation_power = (minimum_power + maximum_power) * 0.5
    evaluations: list[dict[str, Any]] = []
    ensure_enemy_target_ids(state)
    enemies = state.get("enemies", [])
    if not isinstance(enemies, list):
        enemies = []
    for enemy in enemies:
        if (
            not isinstance(enemy, dict)
            or enemy.get("defeated", False)
            or enemy.get("pocketed", False)
        ):
            continue
        target_id = str(enemy.get("target_id", ""))
        if not target_id:
            continue
        try:
            evaluation = evaluate_tactical_shot_options(
                state,
                target_id,
                evaluation_power,
                profile,
            )
        except GameBridgeError:
            continue
        selected_score = float(
            evaluation["damage_option"]["score"]
        )
        if evaluation["recommended_goal"] == "pocket":
            best_pocket = evaluation.get("best_pocket_option")
            if isinstance(best_pocket, dict):
                selected_score = float(best_pocket.get("score", 0.0))
        evaluations.append({
            "target_id": target_id,
            "enemy_id": enemy.get("enemy_id"),
            "recommended_goal": evaluation["recommended_goal"],
            "recommended_pocket_index": evaluation[
                "recommended_pocket_index"
            ],
            "reason": evaluation["reason"],
            "score": round(selected_score, 4),
            "damage_option": evaluation["damage_option"],
            "best_pocket_option": evaluation["best_pocket_option"],
        })
    evaluations.sort(key=lambda value: value["score"], reverse=True)
    return {
        "evaluation_power": evaluation_power,
        "recommended_target_id": (
            evaluations[0]["target_id"] if evaluations else None
        ),
        "recommended_goal": (
            evaluations[0]["recommended_goal"] if evaluations else None
        ),
        "recommendations": evaluations,
        "usage": (
            "fire_shotでshot_goal=autoを使うと、実行時の"
            "パワーと最新状態でdamage/pocketを再評価します。"
        ),
    }


def recommend_shot_power(
    state: dict[str, Any],
    target_id: str,
    shot_type: str,
    profile: dict[str, Any],
) -> dict[str, Any]:
    ensure_enemy_target_ids(state)
    if shot_type not in VALID_SHOT_TYPES:
        raise GameBridgeError(
            "shot_typeはdirectまたはbankを指定してください。"
        )

    recommended = profile.get("recommended_power", {})
    if not isinstance(recommended, dict):
        recommended = {}
    minimum_power = _finite_number(
        recommended.get("min", 3.0),
        "mcp_control.recommended_power.min",
    )
    maximum_power = _finite_number(
        recommended.get("max", 7.0),
        "mcp_control.recommended_power.max",
    )
    if (
        not 1.0 <= minimum_power <= 8.0
        or not 1.0 <= maximum_power <= 8.0
        or minimum_power > maximum_power
    ):
        raise GameBridgeError(
            "recommended_powerは1以上8以下かつmin<=maxに"
            "してください。"
        )

    player_position = _xz_position(
        state.get("player", {}).get("position"),
        "player.position",
    )
    target = _find_live_enemy(state, target_id)
    target_position = _xz_position(
        target.get("position"),
        "target.position",
    )
    direct_distance = _distance(player_position, target_position)
    table = state.get("table", {})
    if not isinstance(table, dict):
        table = {}
    field_width = max(
        1.0,
        _finite_number(
            table.get("field_width", 137.0),
            "table.field_width",
        ),
    )
    field_depth = max(
        1.0,
        _finite_number(
            table.get("field_depth", 72.0),
            "table.field_depth",
        ),
    )
    reference_distance = max(
        20.0,
        math.hypot(field_width, field_depth) * 0.5,
    )
    path_multiplier = 1.25 if shot_type == "bank" else 1.0
    estimated_path_distance = direct_distance * path_multiplier
    distance_ratio = min(
        1.0,
        max(0.0, estimated_path_distance / reference_distance),
    )
    power = minimum_power + (
        maximum_power - minimum_power
    ) * distance_ratio
    return {
        "recommended_power": round(power, 3),
        "minimum_power": minimum_power,
        "maximum_power": maximum_power,
        "direct_distance": round(direct_distance, 3),
        "estimated_path_distance": round(estimated_path_distance, 3),
        "reference_distance": round(reference_distance, 3),
        "distance_ratio": round(distance_ratio, 4),
        "reason": "distance_adaptive_power",
    }


def plan_targeted_shot(
    state: dict[str, Any],
    target_id: str,
    power: float,
    shot_type: str,
    wall_index: int,
    profile: dict[str, Any],
    random_source: random.Random | None = None,
    shot_goal: str = "auto",
    pocket_index: int = -1,
) -> dict[str, Any]:
    ensure_enemy_target_ids(state)
    if shot_type not in VALID_SHOT_TYPES:
        raise GameBridgeError(
            "shot_typeはdirectまたはbankを指定してください。"
        )
    if shot_goal not in VALID_SHOT_GOALS:
        raise GameBridgeError(
            "shot_goalはauto、damage、pocketのいずれかを"
            "指定してください。"
        )
    if shot_goal == "pocket" and shot_type != "direct":
        raise GameBridgeError(
            "ポケット狙いの初期実装はshot_type=directのみ対応します。"
        )
    if shot_type == "bank" and not profile.get(
        "allow_bank_shots",
        False,
    ):
        raise GameBridgeError(
            f"{profile.get('label', '現在')}レベルでは"
            "壁反射ショットを使用できません。"
        )

    shot_power = _finite_number(power, "power")
    if not 1.0 <= shot_power <= 8.0:
        raise GameBridgeError(
            "powerは1以上8以下で指定してください。"
        )

    player_position = _xz_position(
        state.get("player", {}).get("position"),
        "player.position",
    )
    target = _find_live_enemy(state, target_id)
    target_position = _xz_position(
        target.get("position"),
        "target.position",
    )
    target_radius = _nonnegative_finite_number(
        target.get("radius", 0.0),
        "target.radius",
    )
    tactical_evaluation = evaluate_tactical_shot_options(
        state,
        target_id,
        shot_power,
        profile,
        pocket_index,
    )
    effective_shot_goal = (
        tactical_evaluation["recommended_goal"]
        if shot_goal == "auto"
        else shot_goal
    )
    effective_shot_type = (
        "direct"
        if effective_shot_goal == "pocket"
        else shot_type
    )
    ideal_target_position = target_position
    selected_pocket: dict[str, Any] | None = None
    intended_outcome: str | None = None
    if effective_shot_goal == "pocket":
        selected_pocket_index = int(
            tactical_evaluation["recommended_pocket_index"]
        )
        if selected_pocket_index < 0:
            selected_pocket_index = pocket_index
        selected_pocket = _select_pocket(
            state,
            target_position,
            selected_pocket_index,
        )
        pocket_position = _xz_position(
            selected_pocket.get("position"),
            "table.pockets[].position",
        )
        push_direction = _normalized_direction(
            target_position,
            pocket_position,
        )
        player_radius = _nonnegative_finite_number(
            state.get("player", {}).get("radius", 0.0),
            "player.radius",
        )
        contact_distance = max(
            0.1,
            player_radius + target_radius - 0.05,
        )
        ideal_target_position = _subtract(
            target_position,
            _scale(push_direction, contact_distance),
        )
        intended_outcome = (
            "finisher"
            if bool(target.get("pocket_finisher_eligible", False))
            else "skip_attack_and_queue_return"
        )
    aim_error_ratio, power_error_ratio = _human_error_limits(profile)
    rng = random_source or _HUMAN_ERROR_RANDOM
    actual_target_position, lateral_aim_error, aim_error_limit = (
        _apply_aim_error(
            player_position,
            ideal_target_position,
            target_radius,
            aim_error_ratio,
            rng,
        )
    )
    applied_power_error_ratio = rng.triangular(
        -power_error_ratio,
        power_error_ratio,
        0.0,
    )
    actual_power = min(
        8.0,
        max(1.0, shot_power * (1.0 + applied_power_error_ratio)),
    )
    error_report = {
        "ideal_aim_point": _point_json(ideal_target_position),
        "actual_aim_point": _point_json(actual_target_position),
        "lateral_aim_error": lateral_aim_error,
        "aim_error_limit": aim_error_limit,
        "aim_error_radius_ratio": aim_error_ratio,
        "requested_power": shot_power,
        "actual_power": actual_power,
        "applied_power_error_ratio": applied_power_error_ratio,
        "power_error_limit_ratio": power_error_ratio,
    }

    if effective_shot_type == "direct":
        direction = _normalized_direction(
            player_position,
            actual_target_position,
        )
        return {
            "arguments": {
                "direction_x": direction[0],
                "direction_z": direction[1],
                "power": actual_power,
                "target_id": target_id,
                "shot_type": "direct",
                "shot_goal": effective_shot_goal,
            },
            "shot_plan": {
                "target_id": target_id,
                "enemy_id": target.get("enemy_id"),
                "shot_type": "direct",
                "shot_goal": effective_shot_goal,
                "requested_shot_goal": shot_goal,
                "tactical_evaluation": tactical_evaluation,
                "aim_point": _point_json(actual_target_position),
                "human_error": error_report,
                **(
                    {
                        "pocket_index": selected_pocket.get("index"),
                        "pocket_position": selected_pocket.get("position"),
                        "intended_outcome": intended_outcome,
                    }
                    if selected_pocket is not None
                    else {}
                ),
            },
        }

    walls = state.get("table", {}).get("walls")
    if not isinstance(walls, list) or not walls:
        raise GameBridgeError(
            "壁反射に必要なtable.wallsがゲーム状態にありません。"
        )

    candidates: list[dict[str, Any]] = []
    indexes = (
        range(len(walls))
        if wall_index < 0
        else (wall_index,)
    )
    for candidate_index in indexes:
        if not 0 <= candidate_index < len(walls):
            raise GameBridgeError(
                "wall_indexがtable.wallsの範囲外です。"
            )
        candidate = _plan_bank_shot(
            player_position,
            actual_target_position,
            walls[candidate_index],
            candidate_index,
        )
        if candidate is not None:
            candidates.append(candidate)

    if not candidates:
        requested_wall = (
            "指定した壁"
            if wall_index >= 0
            else "いずれの壁"
        )
        raise GameBridgeError(
            f"{requested_wall}でも有効な1回反射経路を作れません。"
            "directを使用するか別のwall_indexを指定してください。"
        )

    best = min(
        candidates,
        key=lambda candidate: candidate["path_length"],
    )
    direction = _normalized_direction(
        player_position,
        best["contact_point"],
    )
    return {
        "arguments": {
            "direction_x": direction[0],
            "direction_z": direction[1],
            "power": actual_power,
            "target_id": target_id,
            "shot_type": "bank",
            "shot_goal": effective_shot_goal,
            "wall_index": best["wall_index"],
        },
        "shot_plan": {
            "target_id": target_id,
            "enemy_id": target.get("enemy_id"),
            "shot_type": "bank",
            "shot_goal": effective_shot_goal,
            "requested_shot_goal": shot_goal,
            "tactical_evaluation": tactical_evaluation,
            "wall_index": best["wall_index"],
            "wall_contact_point": _point_json(
                best["contact_point"]
            ),
            "aim_point_after_reflection": _point_json(
                actual_target_position
            ),
            "estimated_path_length": best["path_length"],
            "human_error": error_report,
        },
    }


def _human_error_limits(profile: dict[str, Any]) -> tuple[float, float]:
    settings = profile.get("human_error", {})
    if not isinstance(settings, dict):
        raise GameBridgeError(
            "mcp_control.human_errorはオブジェクトで指定してください。"
        )
    aim_error_ratio = _finite_number(
        settings.get("aim_radius_ratio", 0.0),
        "mcp_control.human_error.aim_radius_ratio",
    )
    power_error_ratio = _finite_number(
        settings.get("power_ratio", 0.0),
        "mcp_control.human_error.power_ratio",
    )
    if not 0.0 <= aim_error_ratio <= 1.0:
        raise GameBridgeError(
            "aim_radius_ratioは0以上1以下で指定してください。"
        )
    if not 0.0 <= power_error_ratio <= 1.0:
        raise GameBridgeError(
            "power_ratioは0以上1以下で指定してください。"
        )
    return aim_error_ratio, power_error_ratio


def _apply_aim_error(
    player_position: tuple[float, float],
    ideal_target_position: tuple[float, float],
    target_radius: float,
    aim_error_ratio: float,
    random_source: random.Random,
) -> tuple[tuple[float, float], float, float]:
    error_limit = target_radius * aim_error_ratio
    if error_limit <= 0.0:
        return ideal_target_position, 0.0, 0.0

    ideal_direction = _normalized_direction(
        player_position,
        ideal_target_position,
    )
    perpendicular = (ideal_direction[1], -ideal_direction[0])
    lateral_error = random_source.triangular(
        -error_limit,
        error_limit,
        0.0,
    )
    actual_target_position = _add(
        ideal_target_position,
        _scale(perpendicular, lateral_error),
    )
    return actual_target_position, lateral_error, error_limit


def _find_live_enemy(
    state: dict[str, Any],
    target_id: str,
) -> dict[str, Any]:
    enemies = state.get("enemies")
    if not isinstance(enemies, list):
        raise GameBridgeError(
            "ゲーム状態にenemies配列がありません。"
        )
    for enemy in enemies:
        if (
            isinstance(enemy, dict)
            and enemy.get("target_id") == target_id
        ):
            if enemy.get("defeated", False):
                raise GameBridgeError(
                    "撃破済みの敵は狙えません。"
                )
            if enemy.get("pocketed", False):
                raise GameBridgeError(
                    "ポケット復帰待ちの敵は標的にできません。"
                )
            return enemy

    legacy_matches = [
        enemy
        for enemy in enemies
        if (
            isinstance(enemy, dict)
            and enemy.get("enemy_id") == target_id
            and not enemy.get("defeated", False)
            and not enemy.get("pocketed", False)
        )
    ]
    if len(legacy_matches) == 1:
        return legacy_matches[0]
    if len(legacy_matches) > 1:
        raise GameBridgeError(
            "enemy_idが重複しているため標的を一意に決められません。"
            "get_game_stateを再取得し、敵個体のtarget_idを"
            "指定してください。"
        )
    raise GameBridgeError(
        "target_idに一致する生存中の敵がいません。"
    )


def _select_pocket(
    state: dict[str, Any],
    target_position: tuple[float, float],
    pocket_index: int,
) -> dict[str, Any]:
    pockets = state.get("table", {}).get("pockets")
    if not isinstance(pockets, list) or not pockets:
        raise GameBridgeError(
            "ポケット狙いに必要なtable.pocketsがゲーム状態にありません。"
        )
    candidates = [pocket for pocket in pockets if isinstance(pocket, dict)]
    if pocket_index >= 0:
        for pocket in candidates:
            if int(pocket.get("index", -1)) == pocket_index:
                return pocket
        raise GameBridgeError(
            "pocket_indexがtable.pocketsの範囲外です。"
        )
    return min(
        candidates,
        key=lambda pocket: _distance(
            target_position,
            _xz_position(
                pocket.get("position"),
                "table.pockets[].position",
            ),
        ),
    )


def _path_pocket_risk(
    start: tuple[float, float],
    end: tuple[float, float],
    player_radius: float,
    pockets: list[Any],
) -> float:
    risk = 0.0
    for pocket in pockets:
        if not isinstance(pocket, dict):
            continue
        try:
            center = _xz_position(
                pocket.get("position"),
                "table.pockets[].position",
            )
            pocket_radius = _nonnegative_finite_number(
                pocket.get("radius", 0.0),
                "table.pockets[].radius",
            )
        except GameBridgeError:
            continue
        trigger_radius = max(0.1, player_radius + pocket_radius)
        distance = _distance_point_to_segment(center, start, end)
        if distance <= trigger_radius:
            return 1.0
        warning_radius = trigger_radius * 1.75
        if distance < warning_radius:
            risk = max(
                risk,
                1.0 - (
                    distance - trigger_radius
                ) / (warning_radius - trigger_radius),
            )
    return risk


def _distance_point_to_segment(
    point: tuple[float, float],
    start: tuple[float, float],
    end: tuple[float, float],
) -> float:
    segment = _subtract(end, start)
    length_squared = _dot(segment, segment)
    if length_squared <= 0.00000001:
        return _distance(point, start)
    projection = max(
        0.0,
        min(
            1.0,
            _dot(_subtract(point, start), segment) / length_squared,
        ),
    )
    closest = _add(start, _scale(segment, projection))
    return _distance(point, closest)


def _plan_bank_shot(
    player: tuple[float, float],
    target: tuple[float, float],
    wall: Any,
    wall_index: int,
) -> dict[str, Any] | None:
    if not isinstance(wall, dict):
        return None
    try:
        start = _xz_position(wall.get("start"), "wall.start")
        end = _xz_position(wall.get("end"), "wall.end")
    except GameBridgeError:
        return None

    wall_vector = _subtract(end, start)
    wall_length_squared = _dot(wall_vector, wall_vector)
    if wall_length_squared <= 0.00000001:
        return None

    target_from_wall = _subtract(target, start)
    projection_scale = (
        _dot(target_from_wall, wall_vector)
        / wall_length_squared
    )
    projection = _add(
        start,
        _scale(wall_vector, projection_scale),
    )
    reflected_target = _subtract(
        _scale(projection, 2.0),
        target,
    )

    ray = _subtract(reflected_target, player)
    denominator = _cross(ray, wall_vector)
    if abs(denominator) <= 0.00000001:
        return None

    from_player_to_wall = _subtract(start, player)
    ray_scale = (
        _cross(from_player_to_wall, wall_vector)
        / denominator
    )
    wall_scale = _cross(from_player_to_wall, ray) / denominator
    if not 0.0001 < ray_scale < 0.9999:
        return None

    wall_length = math.sqrt(wall_length_squared)
    endpoint_margin = min(1.0, wall_length * 0.1)
    distance_from_start = wall_scale * wall_length
    if not (
        endpoint_margin
        <= distance_from_start
        <= wall_length - endpoint_margin
    ):
        return None

    contact_point = _add(player, _scale(ray, ray_scale))
    path_length = (
        _distance(player, contact_point)
        + _distance(contact_point, target)
    )
    return {
        "wall_index": wall_index,
        "contact_point": contact_point,
        "path_length": path_length,
    }


def _xz_position(
    value: Any,
    field_name: str,
) -> tuple[float, float]:
    if not isinstance(value, dict):
        raise GameBridgeError(
            f"{field_name}がゲーム状態にありません。"
        )
    return (
        _finite_number(value.get("x"), f"{field_name}.x"),
        _finite_number(value.get("z"), f"{field_name}.z"),
    )


def _finite_number(value: Any, field_name: str) -> float:
    try:
        converted = float(value)
    except (TypeError, ValueError) as error:
        raise GameBridgeError(
            f"{field_name}には有限の数値が必要です。"
        ) from error
    if not math.isfinite(converted):
        raise GameBridgeError(
            f"{field_name}には有限の数値が必要です。"
        )
    return converted


def _nonnegative_finite_number(value: Any, field_name: str) -> float:
    converted = _finite_number(value, field_name)
    if converted < 0.0:
        raise GameBridgeError(
            f"{field_name}は0以上で指定してください。"
        )
    return converted


def _normalized_direction(
    start: tuple[float, float],
    end: tuple[float, float],
) -> tuple[float, float]:
    direction = _subtract(end, start)
    length = math.hypot(direction[0], direction[1])
    if length <= 0.0001:
        raise GameBridgeError(
            "プレイヤーと標的が近すぎるため照準を計算できません。"
        )
    return (direction[0] / length, direction[1] / length)


def _point_json(point: tuple[float, float]) -> dict[str, float]:
    return {"x": point[0], "z": point[1]}


def _add(
    left: tuple[float, float],
    right: tuple[float, float],
) -> tuple[float, float]:
    return (left[0] + right[0], left[1] + right[1])


def _subtract(
    left: tuple[float, float],
    right: tuple[float, float],
) -> tuple[float, float]:
    return (left[0] - right[0], left[1] - right[1])


def _scale(
    vector: tuple[float, float],
    amount: float,
) -> tuple[float, float]:
    return (vector[0] * amount, vector[1] * amount)


def _dot(
    left: tuple[float, float],
    right: tuple[float, float],
) -> float:
    return left[0] * right[0] + left[1] * right[1]


def _cross(
    left: tuple[float, float],
    right: tuple[float, float],
) -> float:
    return left[0] * right[1] - left[1] * right[0]


def _distance(
    left: tuple[float, float],
    right: tuple[float, float],
) -> float:
    return math.hypot(left[0] - right[0], left[1] - right[1])
