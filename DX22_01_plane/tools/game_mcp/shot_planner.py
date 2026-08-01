from __future__ import annotations

import copy
import json
import math
import threading
from pathlib import Path
from typing import Any

from bridge_store import GameBridgeError


VALID_PLAYER_LEVELS = ("beginner", "intermediate", "advanced")
VALID_SHOT_TYPES = ("direct", "bank")


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
        "ショット後はボールが停止してfire_shotが再び利用可能になるまで"
        "状態を確認してください。"
        "get_game_stateのmcp_controlに現在のplayer_levelと行動方針が"
        "含まれるため、毎回それに従ってください。"
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


def plan_targeted_shot(
    state: dict[str, Any],
    target_id: str,
    power: float,
    shot_type: str,
    wall_index: int,
    profile: dict[str, Any],
) -> dict[str, Any]:
    ensure_enemy_target_ids(state)
    if shot_type not in VALID_SHOT_TYPES:
        raise GameBridgeError(
            "shot_typeはdirectまたはbankを指定してください。"
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

    if shot_type == "direct":
        direction = _normalized_direction(
            player_position,
            target_position,
        )
        return {
            "arguments": {
                "direction_x": direction[0],
                "direction_z": direction[1],
                "power": shot_power,
                "target_id": target_id,
                "shot_type": "direct",
            },
            "shot_plan": {
                "target_id": target_id,
                "enemy_id": target.get("enemy_id"),
                "shot_type": "direct",
                "aim_point": _point_json(target_position),
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
            target_position,
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
            "power": shot_power,
            "target_id": target_id,
            "shot_type": "bank",
            "wall_index": best["wall_index"],
        },
        "shot_plan": {
            "target_id": target_id,
            "enemy_id": target.get("enemy_id"),
            "shot_type": "bank",
            "wall_index": best["wall_index"],
            "wall_contact_point": _point_json(
                best["contact_point"]
            ),
            "aim_point_after_reflection": _point_json(
                target_position
            ),
            "estimated_path_length": best["path_length"],
        },
    }


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
            return enemy

    legacy_matches = [
        enemy
        for enemy in enemies
        if (
            isinstance(enemy, dict)
            and enemy.get("enemy_id") == target_id
            and not enemy.get("defeated", False)
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
