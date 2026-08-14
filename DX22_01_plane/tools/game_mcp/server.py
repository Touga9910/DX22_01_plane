from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
from typing import Any, Literal

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from pydantic import BaseModel, Field

from bridge_store import GameBridgeError, GameBridgeStore
from shot_planner import (
    PlayerProfileController,
    build_tactical_shot_context,
    build_server_instructions,
    ensure_enemy_target_ids,
    load_player_profiles,
    plan_targeted_shot,
    resolve_target_id_argument,
)


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BRIDGE_DIRECTORY = PROJECT_ROOT / "runtime" / "game_mcp"
DEFAULT_PLAYER_PROFILES_PATH = (
    Path(__file__).resolve().parent / "player_profiles.json"
)
DEFAULT_LOW_HP_RELIC_PRIORITY = (
    "Emergency Repair Kit",
    "Guard Core",
)
DEFAULT_ATTACK_RELIC_PRIORITY = (
    "Power Core",
    "Impact Accelerator",
    "Bank Shot",
)
VALID_WANTED_REWARDS = (
    "money",
    "new_ball",
    "ball_upgrade",
    "hp_recovery",
    "relic",
)
NEED_SIGNAL_PRIORITY = (
    "hp_recovery",
    "relic",
    "ball_upgrade",
    "new_ball",
    "money",
)


def _rest_heal_available(state: dict[str, Any]) -> bool:
    rest_heal = state.get("rest_heal", {})
    if not isinstance(rest_heal, dict):
        return True
    return bool(rest_heal.get("available", True))


def _shop_has_actionable_purchase(state: dict[str, Any]) -> bool:
    player = state.get("player", {})
    money = int(player.get("money", 0)) if isinstance(player, dict) else 0
    relics = state.get("relics", [])
    if isinstance(relics, list):
        for relic in relics:
            if not isinstance(relic, dict):
                continue
            if (
                not bool(relic.get("owned", False))
                and int(relic.get("price", 0)) <= money
            ):
                return True
    deck_rule = state.get("deck_rule", {})
    return bool(
        isinstance(deck_rule, dict)
        and deck_rule.get("can_remove", False)
    )


def resolve_mcp_route_choice(
    state: dict[str, Any],
    requested_route_index: int,
    profile: dict[str, Any],
) -> dict[str, Any]:
    route_options = state.get("route_options", [])
    if not isinstance(route_options, list):
        route_options = []
    options = [
        option
        for option in route_options
        if isinstance(option, dict)
        and isinstance(option.get("route_index"), int)
    ]
    requested = next(
        (
            option
            for option in options
            if option["route_index"] == requested_route_index
        ),
        None,
    )
    if requested is None:
        raise GameBridgeError(
            "route_indexは現在のroute_optionsから選んでください。"
        )

    policy = profile.get("route_policy", {})
    if not isinstance(policy, dict):
        policy = {}
    low_hp_threshold = max(
        0,
        int(policy.get("low_hp_rest_threshold", 25)),
    )
    force_low_hp_rest = bool(
        policy.get("force_low_hp_rest", True)
    )
    avoid_unactionable_shop = bool(
        policy.get("avoid_unactionable_shop", True)
    )
    player = state.get("player", {})
    current_hp = (
        int(player.get("current_hp", 0))
        if isinstance(player, dict)
        else 0
    )
    maximum_hp = (
        int(player.get("max_hp", current_hp))
        if isinstance(player, dict)
        else current_hp
    )
    rest_options = [
        option
        for option in options
        if option.get("destination") == "rest"
    ]
    rest_heal_available = _rest_heal_available(state)
    battle_options = [
        option
        for option in options
        if option.get("destination") == "battle"
    ]
    effective = requested
    reason = "requested_route_allowed"

    if (
        force_low_hp_rest
        and current_hp <= low_hp_threshold
        and current_hp < maximum_hp
        and rest_heal_available
        and rest_options
    ):
        effective = rest_options[0]
        reason = "low_hp_rest_priority"
    elif (
        avoid_unactionable_shop
        and requested.get("destination") == "shop"
        and not _shop_has_actionable_purchase(state)
    ):
        if (
            current_hp < maximum_hp
            and rest_heal_available
            and rest_options
        ):
            effective = rest_options[0]
            reason = "unactionable_shop_redirected_to_rest"
        elif battle_options:
            effective = battle_options[0]
            reason = "unactionable_shop_redirected_to_battle"
        else:
            reason = "unactionable_shop_only_available_route"

    return {
        "requested_route_index": requested_route_index,
        "effective_route_index": int(effective["route_index"]),
        "effective_destination": str(
            effective.get("destination", "unknown")
        ),
        "overridden": (
            int(effective["route_index"]) != requested_route_index
        ),
        "reason": reason,
        "current_hp": current_hp,
        "low_hp_rest_threshold": low_hp_threshold,
        "shop_actionable": _shop_has_actionable_purchase(state),
        "rest_heal_available": rest_heal_available,
    }


def _relic_priority_names(
    policy: dict[str, Any],
    key: str,
    default: tuple[str, ...],
) -> tuple[str, ...]:
    configured = policy.get(key)
    if not isinstance(configured, list):
        return default
    names = tuple(
        name
        for name in configured
        if isinstance(name, str) and name
    )
    return names or default


def _average_deck_attack(state: dict[str, Any]) -> float | None:
    attacks: list[float] = []
    deck_balls = state.get("deck_balls", [])
    if isinstance(deck_balls, list):
        for ball in deck_balls:
            if not isinstance(ball, dict):
                continue
            status = ball.get("status", {})
            if not isinstance(status, dict):
                continue
            attack = status.get("attack")
            if (
                isinstance(attack, (int, float))
                and not isinstance(attack, bool)
                and math.isfinite(float(attack))
            ):
                attacks.append(float(attack))
    if not attacks:
        return None

    relic_effects = state.get("relic_effects", {})
    attack_bonus = 0.0
    if isinstance(relic_effects, dict):
        configured_bonus = relic_effects.get(
            "all_ball_attack_bonus",
            0,
        )
        if (
            isinstance(configured_bonus, (int, float))
            and not isinstance(configured_bonus, bool)
            and math.isfinite(float(configured_bonus))
        ):
            attack_bonus = float(configured_bonus)
    return sum(attacks) / len(attacks) + attack_bonus


def resolve_mcp_relic_choice(
    state: dict[str, Any],
    requested_relic_index: int,
    profile: dict[str, Any],
) -> dict[str, Any]:
    relics = state.get("relics", [])
    if not isinstance(relics, list):
        relics = []
    catalog = [
        relic
        for relic in relics
        if isinstance(relic, dict)
        and isinstance(relic.get("index"), int)
    ]
    requested = next(
        (
            relic
            for relic in catalog
            if relic["index"] == requested_relic_index
        ),
        None,
    )
    if requested is None:
        raise GameBridgeError(
            "relic_indexは現在のrelicsから選んでください。"
        )

    player = state.get("player", {})
    if not isinstance(player, dict):
        player = {}
    money = max(0, int(player.get("money", 0)))
    current_hp = max(0, int(player.get("current_hp", 0)))
    maximum_hp = max(0, int(player.get("max_hp", current_hp)))
    hp_ratio = (
        current_hp / maximum_hp
        if maximum_hp > 0
        else 0.0
    )

    policy = profile.get("relic_policy", {})
    if not isinstance(policy, dict):
        policy = {}
    low_hp_ratio = min(
        1.0,
        max(0.0, float(policy.get("low_hp_ratio", 0.5))),
    )
    minimum_average_attack = max(
        0.0,
        float(policy.get("minimum_average_attack", 2.0)),
    )
    low_hp_priority = _relic_priority_names(
        policy,
        "low_hp_priority",
        DEFAULT_LOW_HP_RELIC_PRIORITY,
    )
    attack_priority = _relic_priority_names(
        policy,
        "attack_priority",
        DEFAULT_ATTACK_RELIC_PRIORITY,
    )
    affordable = [
        relic
        for relic in catalog
        if not bool(relic.get("owned", False))
        and int(relic.get("price", 0)) <= money
    ]

    def first_available(
        priority_names: tuple[str, ...],
    ) -> dict[str, Any] | None:
        for name in priority_names:
            for relic in affordable:
                if relic.get("name") == name:
                    return relic
        return None

    average_attack = _average_deck_attack(state)
    effective = requested
    reason = "requested_relic_allowed"
    if (
        current_hp < maximum_hp
        and hp_ratio <= low_hp_ratio
        and (survival_relic := first_available(low_hp_priority))
        is not None
    ):
        effective = survival_relic
        reason = "low_hp_survival_priority"
    elif (
        average_attack is not None
        and average_attack < minimum_average_attack
        and (attack_relic := first_available(attack_priority))
        is not None
    ):
        effective = attack_relic
        reason = "low_attack_priority"

    return {
        "requested_relic_index": requested_relic_index,
        "requested_relic_name": str(requested.get("name", "")),
        "effective_relic_index": int(effective["index"]),
        "effective_relic_name": str(effective.get("name", "")),
        "overridden": (
            int(effective["index"]) != requested_relic_index
        ),
        "reason": reason,
        "current_hp": current_hp,
        "maximum_hp": maximum_hp,
        "hp_ratio": hp_ratio,
        "low_hp_ratio": low_hp_ratio,
        "average_deck_attack": average_attack,
        "minimum_average_attack": minimum_average_attack,
    }


def _has_upgradeable_ball(state: dict[str, Any]) -> bool:
    deck_balls = state.get("deck_balls", [])
    return bool(
        isinstance(deck_balls, list)
        and any(
            isinstance(ball, dict)
            and bool(ball.get("can_upgrade", False))
            for ball in deck_balls
        )
    )


def _shop_has_affordable_relic(state: dict[str, Any]) -> bool:
    player = state.get("player", {})
    money = int(player.get("money", 0)) if isinstance(player, dict) else 0
    relics = state.get("relics", [])
    return bool(
        isinstance(relics, list)
        and any(
            isinstance(relic, dict)
            and not bool(relic.get("owned", False))
            and int(relic.get("price", 0)) <= money
            for relic in relics
        )
    )


def build_stage_choice_context(
    state: dict[str, Any],
    profile: dict[str, Any],
) -> dict[str, Any]:
    route_options = state.get("route_options", [])
    if not isinstance(route_options, list):
        route_options = []
    routes = [
        option
        for option in route_options
        if isinstance(option, dict)
        and isinstance(option.get("route_index"), int)
    ]
    battle_routes = [
        int(route["route_index"])
        for route in routes
        if route.get("destination") == "battle"
    ]
    rest_routes = [
        int(route["route_index"])
        for route in routes
        if route.get("destination") == "rest"
    ]
    shop_routes = [
        int(route["route_index"])
        for route in routes
        if route.get("destination") == "shop"
    ]

    player = state.get("player", {})
    if not isinstance(player, dict):
        player = {}
    current_hp = max(0, int(player.get("current_hp", 0)))
    maximum_hp = max(0, int(player.get("max_hp", current_hp)))
    hp_ratio = (
        current_hp / maximum_hp
        if maximum_hp > 0
        else 0.0
    )
    money = max(0, int(player.get("money", 0)))
    deck_balls = state.get("deck_balls", [])
    deck_size = len(deck_balls) if isinstance(deck_balls, list) else 0
    upgradeable = _has_upgradeable_ball(state)
    affordable_relic = _shop_has_affordable_relic(state)
    catalog_balls = state.get("catalog_balls", [])
    new_ball_available = bool(
        isinstance(catalog_balls, list) and catalog_balls
    )

    settings = profile.get("stage_choice_policy", {})
    if not isinstance(settings, dict):
        settings = {}
    low_hp_ratio = min(
        1.0,
        max(0.0, float(settings.get("low_hp_ratio", 0.5))),
    )
    money_reserve = max(
        0,
        int(settings.get("money_reserve", 20)),
    )
    target_deck_size = max(
        1,
        int(settings.get("target_deck_size", 8)),
    )
    minimum_average_attack = max(
        0.0,
        float(settings.get("minimum_average_attack", 2.0)),
    )
    average_attack = _average_deck_attack(state)
    rest_heal_available = _rest_heal_available(state)

    reward_route_matches = {
        "money": battle_routes,
        "new_ball": (
            battle_routes if new_ball_available else []
        ),
        "ball_upgrade": (
            [*rest_routes, *battle_routes]
            if upgradeable
            else []
        ),
        "hp_recovery": (
            rest_routes
            if current_hp < maximum_hp and rest_heal_available
            else []
        ),
        "relic": shop_routes if affordable_relic else [],
    }
    need_signals = {
        "money": money < money_reserve,
        "new_ball": (
            new_ball_available and deck_size < target_deck_size
        ),
        "ball_upgrade": (
            upgradeable
            and average_attack is not None
            and average_attack < minimum_average_attack
        ),
        "hp_recovery": (
            current_hp < maximum_hp
            and rest_heal_available
            and hp_ratio <= low_hp_ratio
        ),
        "relic": affordable_relic,
    }
    return {
        "wanted_reward_values": list(VALID_WANTED_REWARDS),
        "recommended_wanted_rewards": (
            build_dynamic_wanted_reward_order(
                list(VALID_WANTED_REWARDS),
                need_signals,
            )
        ),
        "reward_route_matches": reward_route_matches,
        "need_signals": need_signals,
        "status": {
            "current_hp": current_hp,
            "maximum_hp": maximum_hp,
            "hp_ratio": hp_ratio,
            "money": money,
            "deck_size": deck_size,
            "average_deck_attack": average_attack,
            "has_upgradeable_ball": upgradeable,
            "has_affordable_relic": affordable_relic,
            "rest_heal_available": rest_heal_available,
        },
        "thresholds": {
            "low_hp_ratio": low_hp_ratio,
            "money_reserve": money_reserve,
            "target_deck_size": target_deck_size,
            "minimum_average_attack": minimum_average_attack,
        },
    }


def build_dynamic_wanted_reward_order(
    requested_order: list[str],
    need_signals: dict[str, Any],
) -> list[str]:
    needed = [
        reward
        for reward in NEED_SIGNAL_PRIORITY
        if bool(need_signals.get(reward, False))
    ]
    return [
        *needed,
        *(
            reward
            for reward in requested_order
            if reward not in needed
        ),
    ]


def resolve_mcp_stage_choice(
    state: dict[str, Any],
    wanted_rewards: list[str],
    profile: dict[str, Any],
    requested_route_index: int = -1,
) -> dict[str, Any]:
    if (
        len(wanted_rewards) != len(VALID_WANTED_REWARDS)
        or len(set(wanted_rewards)) != len(VALID_WANTED_REWARDS)
        or set(wanted_rewards) != set(VALID_WANTED_REWARDS)
    ):
        raise GameBridgeError(
            "wanted_rewardsはmoney、new_ball、ball_upgrade、"
            "hp_recovery、relicを重複なしで各1回、"
            "欲しい順に指定してください。"
        )

    context = build_stage_choice_context(state, profile)
    reward_route_matches = context["reward_route_matches"]
    effective_wanted_rewards = build_dynamic_wanted_reward_order(
        wanted_rewards,
        context["need_signals"],
    )
    selected_reward = ""
    priority_route_index = -1
    skipped_rewards: list[str] = []
    for reward in effective_wanted_rewards:
        matching_routes = reward_route_matches.get(reward, [])
        if not matching_routes:
            skipped_rewards.append(reward)
            continue
        selected_reward = reward
        priority_route_index = (
            requested_route_index
            if requested_route_index in matching_routes
            else int(matching_routes[0])
        )
        break

    route_options = state.get("route_options", [])
    if not isinstance(route_options, list):
        route_options = []
    offered_indices = [
        int(option["route_index"])
        for option in route_options
        if isinstance(option, dict)
        and isinstance(option.get("route_index"), int)
    ]
    if priority_route_index < 0:
        if requested_route_index in offered_indices:
            priority_route_index = requested_route_index
        elif offered_indices:
            priority_route_index = offered_indices[0]
        else:
            raise GameBridgeError(
                "現在選択できるroute_optionsがありません。"
            )

    route_policy = resolve_mcp_route_choice(
        state,
        priority_route_index,
        profile,
    )
    route_policy.update(
        {
            "wanted_rewards": list(wanted_rewards),
            "effective_wanted_rewards": effective_wanted_rewards,
            "matched_wanted_reward": selected_reward or None,
            "skipped_wanted_rewards": skipped_rewards,
            "priority_route_index": priority_route_index,
            "ai_requested_route_index": requested_route_index,
            "stage_choice_context": context,
        }
    )
    if route_policy["reason"] == "requested_route_allowed":
        route_policy["reason"] = (
            "wanted_reward_route_selected"
            if selected_reward
            else "fallback_route_selected"
        )
    return route_policy


class GameToolResult(BaseModel):
    result: dict[str, Any]


class StageEnemyPlacement(BaseModel):
    enemy_id: str = Field(
        min_length=1,
        max_length=64,
        description=(
            "stage_layout_control.allowed_enemy_idsに含まれる敵ID。"
        ),
    )
    x: float = Field(
        description="テーブル中心を0とした敵のX座標。",
    )
    z: float = Field(
        description="テーブル中心を0とした敵のZ座標。",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Expose bounded DX22 game controls through an MCP "
            "Streamable HTTP endpoint."
        )
    )
    parser.add_argument(
        "--host",
        default=os.environ.get("GAME_MCP_HOST", "127.0.0.1"),
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("GAME_MCP_PORT", "8765")),
    )
    parser.add_argument(
        "--bridge-directory",
        type=Path,
        default=Path(
            os.environ.get(
                "GAME_MCP_BRIDGE_DIRECTORY",
                str(DEFAULT_BRIDGE_DIRECTORY),
            )
        ),
    )
    parser.add_argument(
        "--command-timeout-seconds",
        type=float,
        default=float(
            os.environ.get(
                "GAME_MCP_COMMAND_TIMEOUT_SECONDS",
                "15",
            )
        ),
    )
    parser.add_argument(
        "--maximum-state-age-seconds",
        type=float,
        default=float(
            os.environ.get(
                "GAME_MCP_MAXIMUM_STATE_AGE_SECONDS",
                "10",
            )
        ),
    )
    parser.add_argument(
        "--player-level",
        choices=("beginner", "intermediate", "advanced"),
        default=os.environ.get(
            "GAME_MCP_PLAYER_LEVEL",
            "intermediate",
        ),
        help=(
            "AI player skill: beginner, intermediate, or advanced. "
            "Defaults to GAME_MCP_PLAYER_LEVEL or intermediate."
        ),
    )
    parser.add_argument(
        "--player-profiles",
        type=Path,
        default=Path(
            os.environ.get(
                "GAME_MCP_PLAYER_PROFILES",
                str(DEFAULT_PLAYER_PROFILES_PATH),
            )
        ),
        help="Path to the player-level behavior profiles JSON.",
    )
    return parser.parse_args()


def create_server(
    store: GameBridgeStore,
    host: str,
    port: int,
    player_profiles: dict[str, dict[str, Any]],
    initial_player_level: str,
) -> FastMCP:
    profile_controller = PlayerProfileController(
        player_profiles,
        initial_player_level,
    )
    mcp = FastMCP(
        "dx22-game-control",
        instructions=build_server_instructions(
            profile_controller.snapshot()
        ),
        host=host,
        port=port,
        streamable_http_path="/mcp",
    )

    read_only = ToolAnnotations(
        readOnlyHint=True,
        destructiveHint=False,
        openWorldHint=False,
    )
    local_write = ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=False,
        openWorldHint=False,
    )
    destructive_write = ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=True,
        openWorldHint=False,
    )

    @mcp.tool(
        title="ゲーム状態を取得",
        description=(
            "現在のシーン、プレイヤー、敵、ボール候補と、"
            "現在実行可能な操作を取得します。mcp_controlには現在の"
            "プレイヤーレベルと行動方針が含まれます。"
            "操作の前に必ず使用します。"
        ),
        annotations=read_only,
        structured_output=True,
    )
    def get_game_state() -> GameToolResult:
        state = ensure_enemy_target_ids(store.read_state())
        profile = profile_controller.snapshot()
        state["mcp_control"] = profile
        state["shot_tactics"] = build_tactical_shot_context(
            state,
            profile,
        )
        state["stage_choice"] = build_stage_choice_context(
            state,
            profile,
        )
        return GameToolResult(result=state)

    @mcp.tool(
        title="プレイヤーレベルを設定",
        description=(
            "AIの操作レベルを変更します。beginnerは直射のみ、"
            "intermediateは状況に応じて1回反射、advancedは"
            "直射と1回反射を積極的に比較します。"
            "ユーザーがレベル変更を指示したときに使用します。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def set_player_level(
        player_level: Literal[
            "beginner",
            "intermediate",
            "advanced",
        ],
    ) -> GameToolResult:
        profile = profile_controller.set_level(player_level)
        return GameToolResult(
            result={
                "ok": True,
                "message": (
                    f"プレイヤーレベルを{profile['label']}に"
                    "変更しました。"
                ),
                "mcp_control": profile,
            }
        )

    @mcp.tool(
        title="動的バランス調整を設定",
        description=(
            "プレイ結果に応じた敵ステータスの自動調整をON/OFFします。"
            "各戦闘の残HP、ショット数、空振り率、勝敗から難易度レベルを"
            "1段階ずつ更新し、次の戦闘で生成される敵のHPと攻撃力へ"
            "安全な範囲で反映します。reset_level=trueで基準レベルへ戻せます。"
            "levelを指定すると許容範囲内へ丸めて開始レベルを直接設定します。"
            "戦闘中の敵ステータスは突然変更しません。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def set_dynamic_balance(
        enabled: bool,
        reset_level: bool = False,
        level: int | None = None,
    ) -> GameToolResult:
        arguments: dict[str, Any] = {
            "enabled": enabled,
            "reset_level": reset_level,
        }
        if level is not None:
            arguments["level"] = level
        return GameToolResult(
            result=store.submit_command(
                "set_dynamic_balance",
                arguments,
            )
        )

    @mcp.tool(
        title="次のステージ配置を設定",
        description=(
            "次の戦闘で生成するステージの敵種類・数・位置を"
            "一回だけ上書きします。enemy_idはget_game_stateの"
            "stage_layout_control.allowed_enemy_idsから選び、"
            "各敵のx/z座標を指定します。1～12体、盤面外、"
            "プレイヤー初期位置や敵同士との重なりはゲーム側で拒否されます。"
            "現在進行中の戦闘には影響しません。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def set_next_stage_layout(
        enemies: list[StageEnemyPlacement],
        stage_type: Literal[
            "normal",
            "midboss",
            "boss",
        ] = "normal",
        difficulty: int = 1,
        par: int = 4,
        layout_id: str = "mcp_custom",
    ) -> GameToolResult:
        if not 1 <= len(enemies) <= 12:
            raise GameBridgeError(
                "enemiesは1～12体で指定してください。"
            )
        if not 1 <= difficulty <= 99:
            raise GameBridgeError(
                "difficultyは1～99で指定してください。"
            )
        if not 1 <= par <= 99:
            raise GameBridgeError(
                "parは1～99で指定してください。"
            )
        if (
            not 1 <= len(layout_id) <= 64
            or any(
                not (
                    character.isalnum()
                    or character in "_.-"
                )
                for character in layout_id
            )
        ):
            raise GameBridgeError(
                "layout_idは英数字と_・-・.を使った1～64文字で"
                "指定してください。"
            )
        for enemy in enemies:
            if not math.isfinite(enemy.x) or not math.isfinite(enemy.z):
                raise GameBridgeError(
                    "敵のx/z座標には有限値を指定してください。"
                )

        return GameToolResult(
            result=store.submit_command(
                "set_next_stage_layout",
                {
                    "layout_id": layout_id,
                    "stage_type": stage_type,
                    "difficulty": difficulty,
                    "par": par,
                    "enemies": [
                        {
                            "enemy_id": enemy.enemy_id,
                            "x": enemy.x,
                            "z": enemy.z,
                        }
                        for enemy in enemies
                    ],
                },
            )
        )

    @mcp.tool(
        title="次のステージ配置を解除",
        description=(
            "set_next_stage_layoutで予約した一回限りの"
            "ステージ上書きを解除し、通常のステージ抽選へ戻します。"
            "現在進行中の戦闘には影響しません。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def clear_next_stage_layout() -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "clear_next_stage_layout"
            )
        )

    @mcp.tool(
        title="新しいランを開始",
        description=(
            "タイトルまたはリザルト画面から新しいランを開始します。"
            "現在の進行を置き換えるため、ユーザーが明示した場合だけ使用します。"
        ),
        annotations=destructive_write,
        structured_output=True,
    )
    def start_new_run() -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "start_new_run",
                {
                    "controller_profile": (
                        profile_controller.level
                    ),
                },
            )
        )

    @mcp.tool(
        title="次の行き先を選択",
        description=(
            "ステージ選択画面で、get_game_stateの"
            "route_optionsに提示された3ノードから1つを選びます。"
            "wanted_rewardsにmoney、new_ball、ball_upgrade、"
            "hp_recovery、relicの5つを、現在欲しい順で"
            "それぞれ1回ずつ指定します。サーバーは先頭から"
            "現在のroute_optionsと照合し、対応ステージがない、"
            "またはそこで希望報酬を得られない場合は次順位へ"
            "フォールバックします。route_indexは同種ノードが"
            "複数ある場合の希望位置で、省略できます。"
            "受け取った順位は現在のneed_signalsで再評価し、"
            "必要な項目をHP回復、レリック、ボール強化、"
            "新ボール、Moneyの優先度で前へ移動します。"
            "HP25以下で休憩所がある場合は休憩を優先し、"
            "購入もボール削除もできないショップは選択しません。"
            "安全ポリシーで選択が補正された場合は、応答の"
            "route_policyに要求値・実行値・理由が入ります。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def choose_destination(
        wanted_rewards: list[
            Literal[
                "money",
                "new_ball",
                "ball_upgrade",
                "hp_recovery",
                "relic",
            ]
        ],
        route_index: int = -1,
    ) -> GameToolResult:
        state = store.read_state()
        route_policy = resolve_mcp_stage_choice(
            state,
            list(wanted_rewards),
            profile_controller.snapshot(),
            route_index,
        )
        result = store.submit_command(
            "choose_destination",
            {
                "route_index": route_policy[
                    "effective_route_index"
                ],
                "route_policy": route_policy,
            },
        )
        result["route_policy"] = route_policy
        return GameToolResult(result=result)

    @mcp.tool(
        title="使用するボールを選択",
        description=(
            "戦闘の照準待ち中に、offered_ballsのindexで"
            "次のショットに使うボールを選択します。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def select_ball(offer_index: int) -> GameToolResult:
        if offer_index < 0:
            raise GameBridgeError(
                "offer_indexは0以上で指定してください。"
            )
        return GameToolResult(
            result=store.submit_command(
                "select_ball",
                {"offer_index": offer_index},
            )
        )

    @mcp.tool(
        title="ショットを実行",
        description=(
            "戦闘の照準待ち中に、enemies[].target_idと1～8のパワーを"
            "指定して撃ちます。target_idは敵個体ごとに一意です。"
            "旧クライアントがtarget_enemy_idだけを公開している場合は、"
            "その引数へ同じtarget_idの値を指定できます。"
            "標的指定は必須で、サーバーが敵の"
            "現在位置から照準方向を計算するため、何もない方向へは"
            "撃てません。shot_type=directは直射、bankはtable.wallsを"
            "使った1回反射です。bankでwall_index=-1なら有効な壁から"
            "最短経路を自動選択します。初心者はdirectだけを使用できます。"
            "最終的な照準とパワーには、mcp_control.human_errorの"
            "設定に基づくプレイヤーレベル別の誤差が加わります。"
            "実際に適用された値は応答のshot_plan.human_errorで確認できます。"
            "shot_goal=autoは通常攻撃と全ポケット経路を比較し、"
            "フィニッシュ、防げる敵攻撃、軌道成立度、自ボールの"
            "ポケットダメージを評価して自動選択します。"
            "damageは敵中心へ通常攻撃、pocketは敵を"
            "table.pocketsの指定位置へ押す接触点を計算します。"
            "pocket_index=-1なら対象に最も近いポケットを選びます。"
            "通常ランではautoを使い、damage/pocketの固定は"
            "特定行動の検証時だけにしてください。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def fire_shot(
        power: float,
        target_id: str = "",
        target_enemy_id: str = "",
        shot_type: Literal["direct", "bank"] = "direct",
        wall_index: int = -1,
        shot_goal: Literal["auto", "damage", "pocket"] = "auto",
        pocket_index: int = -1,
    ) -> GameToolResult:
        state = ensure_enemy_target_ids(store.read_state())
        resolved_target_id = resolve_target_id_argument(
            target_id,
            target_enemy_id,
        )
        shot_plan = plan_targeted_shot(
            state,
            resolved_target_id,
            power,
            shot_type,
            wall_index,
            profile_controller.snapshot(),
            shot_goal=shot_goal,
            pocket_index=pocket_index,
        )
        shot_plan["arguments"]["telemetry"] = {
            "controller_profile": profile_controller.level,
            **shot_plan["shot_plan"],
        }
        result = store.submit_command(
            "fire_shot",
            shot_plan["arguments"],
        )
        result["shot_plan"] = shot_plan["shot_plan"]
        return GameToolResult(result=result)

    @mcp.tool(
        title="プレイヤーを回復",
        description=(
            "休憩所でプレイヤーHPを最大HPの25%回復します。"
            "回復量は切り上げ、最大HPを超える分は切り捨てます。"
            "HPが減っている場合だけ成功します。"
            "HPが40%未満なら最優先で使い、強化可能なボールがなくHPが減っている場合は、"
            "休憩ボーナスを捨てないためのフォールバックとして使います。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def heal() -> GameToolResult:
        return GameToolResult(
            result=store.submit_command("heal")
        )

    @mcp.tool(
        title="ボールを強化",
        description=(
            "休憩所でdeck_ballsのinstance_idを指定し、"
            "そのボールを1段階強化します。deck_ballsのcan_upgradeが"
            "trueのボールを指定できます。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def upgrade_ball(instance_id: int) -> GameToolResult:
        if instance_id <= 0:
            raise GameBridgeError(
                "instance_idは正の整数で指定してください。"
            )
        return GameToolResult(
            result=store.submit_command(
                "upgrade_ball",
                {"instance_id": instance_id},
            )
        )

    @mcp.tool(
        title="ボールを削除",
        description=(
            "ショップでdeck_ballsのinstance_idを指定し、"
            "15 Moneyを支払ってデッキから削除します。"
            "任意のボールを指定できますが、デッキは最低5個必要です。"
            "元に戻しにくいため、"
            "ユーザーの確認後に使用します。"
        ),
        annotations=destructive_write,
        structured_output=True,
    )
    def remove_ball(instance_id: int) -> GameToolResult:
        if instance_id <= 0:
            raise GameBridgeError(
                "instance_idは正の整数で指定してください。"
            )
        return GameToolResult(
            result=store.submit_command(
                "remove_ball",
                {"instance_id": instance_id},
            )
        )

    @mcp.tool(
        title="レリックを購入",
        description=(
            "ショップでrelicsのindexを指定し、表示価格を支払って"
            "未所持のレリックを購入します。購入前にget_game_stateで"
            "Money、価格、ownedを確認します。"
            "購入直前に最新のHPとdeck_ballsの平均attackを再確認し、"
            "低HPでは回復・防御系、攻撃不足では攻撃系の"
            "購入可能なレリックへ自動補正します。"
            "判定結果はrelic_policyに含まれます。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def buy_relic(relic_index: int) -> GameToolResult:
        if relic_index < 0:
            raise GameBridgeError(
                "relic_indexは0以上で指定してください。"
            )
        state = store.read_state()
        relic_policy = resolve_mcp_relic_choice(
            state,
            relic_index,
            profile_controller.snapshot(),
        )
        result = store.submit_command(
            "buy_relic",
            {
                "relic_index": relic_policy[
                    "effective_relic_index"
                ],
                "relic_policy": relic_policy,
            },
        )
        result["relic_policy"] = relic_policy
        return GameToolResult(result=result)

    @mcp.tool(
        title="戦闘へ進む",
        description=(
            "休憩所またはショップから次の戦闘へ進みます。"
            "休憩所では、利用可能な回復または強化を1つ選んだ後にだけ使用します。"
            "利用可能な休憩ボーナスを未取得のまま進むことはできません。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def continue_to_battle(
        stage_type: Literal[
            "normal",
            "midboss",
            "boss",
        ] = "normal",
        override_stage_schedule: bool = False,
    ) -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "continue_to_battle",
                {
                    "stage_type": stage_type,
                    "override_stage_schedule": override_stage_schedule,
                },
            )
        )

    @mcp.tool(
        title="クリア報酬を選択",
        description=(
            "クリア報酬画面で新規ボール、既存ボール強化、"
            "追加Moneyのいずれかを選択します。新規ボールには"
            "catalog_index、強化にはinstance_idが必要です。"
            "強化ではdeck_ballsのcan_upgradeがtrueのボールを指定します。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def choose_reward(
        reward: Literal[
            "new_ball",
            "upgrade_ball",
            "extra_money",
        ],
        catalog_index: int = -1,
        instance_id: int = 0,
    ) -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "choose_reward",
                {
                    "reward": reward,
                    "catalog_index": catalog_index,
                    "instance_id": instance_id,
                },
            )
        )

    @mcp.tool(
        title="報酬選択後に進む",
        description=(
            "クリア報酬を選択した後、ステージ選択画面へ進みます。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def continue_after_reward() -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "continue_after_reward"
            )
        )

    return mcp


def main() -> None:
    args = parse_arguments()
    player_profiles = load_player_profiles(
        args.player_profiles.resolve()
    )
    store = GameBridgeStore(
        args.bridge_directory,
        args.command_timeout_seconds,
        args.maximum_state_age_seconds,
    )
    server = create_server(
        store,
        args.host,
        args.port,
        player_profiles,
        args.player_level,
    )
    print(
        "DX22 Game MCP server: "
        f"http://{args.host}:{args.port}/mcp"
    )
    print(f"Bridge directory: {store.bridge_directory}")
    print(
        "Player level: "
        f"{args.player_level} "
        f"(profiles: {args.player_profiles.resolve()})"
    )
    server.run(transport="streamable-http")


if __name__ == "__main__":
    main()
