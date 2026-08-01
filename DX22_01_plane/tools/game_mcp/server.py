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
        state["mcp_control"] = profile_controller.snapshot()
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
            result=store.submit_command("start_new_run")
        )

    @mcp.tool(
        title="次の行き先を選択",
        description=(
            "ステージ選択画面で、戦闘・休憩所・ショップの"
            "いずれかへ移動します。"
        ),
        annotations=local_write,
        structured_output=True,
    )
    def choose_destination(
        destination: Literal["battle", "rest", "shop"],
        stage_type: Literal[
            "normal",
            "midboss",
            "boss",
        ] = "normal",
    ) -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "choose_destination",
                {
                    "destination": destination,
                    "stage_type": stage_type,
                },
            )
        )

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
        )
        result = store.submit_command(
            "fire_shot",
            shot_plan["arguments"],
        )
        result["shot_plan"] = shot_plan["shot_plan"]
        return GameToolResult(result=result)

    @mcp.tool(
        title="プレイヤーを回復",
        description=(
            "休憩所でプレイヤーHPを全回復します。"
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
        ),
        annotations=local_write,
        structured_output=True,
    )
    def buy_relic(relic_index: int) -> GameToolResult:
        if relic_index < 0:
            raise GameBridgeError(
                "relic_indexは0以上で指定してください。"
            )
        return GameToolResult(
            result=store.submit_command(
                "buy_relic",
                {"relic_index": relic_index},
            )
        )

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
    ) -> GameToolResult:
        return GameToolResult(
            result=store.submit_command(
                "continue_to_battle",
                {"stage_type": stage_type},
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
