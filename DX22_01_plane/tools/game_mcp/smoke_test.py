from __future__ import annotations

import argparse
import asyncio
import json

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--url",
        default="http://127.0.0.1:8765/mcp",
    )
    parser.add_argument(
        "--read-game-state",
        action="store_true",
    )
    parser.add_argument(
        "--show-tool-schema",
        action="store_true",
    )
    parser.add_argument(
        "--start-new-run",
        action="store_true",
        help=(
            "Call the state-changing start_new_run tool. "
            "Use only with a disposable test run."
        ),
    )
    parser.add_argument(
        "--buy-relic-index",
        type=int,
        default=None,
        help=(
            "Call buy_relic with the requested catalog index. "
            "Use only while the game is in the shop."
        ),
    )
    parser.add_argument(
        "--probe-collision-relic",
        action="store_true",
        help=(
            "Fire at the first living enemy and poll MCP state to "
            "capture collision attack bonus growth and reset."
        ),
    )
    parser.add_argument(
        "--probe-target-id",
        default=None,
        help=(
            "Optional enemy target_id used by --probe-collision-relic. "
            "Defaults to the first living enemy."
        ),
    )
    return parser.parse_args()


async def inspect_server(
    url: str,
    read_game_state: bool,
    start_new_run: bool,
    show_tool_schema: bool,
    buy_relic_index: int | None,
    probe_collision_relic: bool,
    probe_target_id: str | None,
) -> None:
    async with streamable_http_client(url) as streams:
        read_stream, write_stream, _ = streams
        async with ClientSession(
            read_stream,
            write_stream,
        ) as session:
            initialization = await session.initialize()
            tools = await session.list_tools()
            print(
                "server="
                f"{initialization.serverInfo.name} "
                f"{initialization.serverInfo.version}"
            )
            print(
                "tools="
                + ",".join(tool.name for tool in tools.tools)
            )
            if show_tool_schema:
                for tool in tools.tools:
                    print(
                        f"tool_schema[{tool.name}]="
                        + json.dumps(
                            tool.inputSchema,
                            ensure_ascii=False,
                        )
                    )
            if read_game_state:
                result = await session.call_tool(
                    "get_game_state",
                    {},
                )
                print(
                    "game_state="
                    + json.dumps(
                        result.structuredContent,
                        ensure_ascii=False,
                    )
                )
            if start_new_run:
                result = await session.call_tool(
                    "start_new_run",
                    {},
                )
                print(
                    "start_new_run="
                    + json.dumps(
                        result.structuredContent,
                        ensure_ascii=False,
                    )
                )
            if buy_relic_index is not None:
                result = await session.call_tool(
                    "buy_relic",
                    {"relic_index": buy_relic_index},
                )
                print(
                    "buy_relic="
                    + json.dumps(
                        result.structuredContent,
                        ensure_ascii=False,
                    )
                )
            if probe_collision_relic:
                state_result = await session.call_tool(
                    "get_game_state",
                    {},
                )
                initial_state = state_result.structuredContent["result"]
                living_enemies = [
                    enemy
                    for enemy in initial_state["enemies"]
                    if not enemy["defeated"]
                ]
                if not living_enemies:
                    raise RuntimeError(
                        "Collision probe requires a living enemy."
                    )
                target_enemy = next(
                    (
                        enemy
                        for enemy in living_enemies
                        if enemy["target_id"] == probe_target_id
                    ),
                    None,
                )
                if probe_target_id is not None and target_enemy is None:
                    raise RuntimeError(
                        f"Collision probe target is not living: "
                        f"{probe_target_id}"
                    )
                if target_enemy is None:
                    target_enemy = living_enemies[0]

                initial_attack = initial_state["player"]["attack"]
                initial_bonus = initial_state["relic_effects"][
                    "current_shot_collision_attack_bonus"
                ]
                await session.call_tool(
                    "fire_shot",
                    {
                        "target_id": target_enemy["target_id"],
                        "power": 7.0,
                        "shot_type": "direct",
                    },
                )

                maximum_attack = initial_attack
                maximum_bonus = initial_bonus
                maximum_player_enemy_collisions = 0
                maximum_enemy_enemy_collisions = 0
                final_state = initial_state
                for _ in range(120):
                    await asyncio.sleep(0.05)
                    state_result = await session.call_tool(
                        "get_game_state",
                        {},
                    )
                    final_state = state_result.structuredContent["result"]
                    player = final_state.get("player", {})
                    effects = final_state.get("relic_effects", {})
                    maximum_attack = max(
                        maximum_attack,
                        player.get("attack", initial_attack),
                    )
                    maximum_bonus = max(
                        maximum_bonus,
                        effects.get(
                            "current_shot_collision_attack_bonus",
                            initial_bonus,
                        ),
                    )
                    maximum_player_enemy_collisions = max(
                        maximum_player_enemy_collisions,
                        effects.get(
                            "current_shot_player_enemy_collisions",
                            0,
                        ),
                    )
                    maximum_enemy_enemy_collisions = max(
                        maximum_enemy_enemy_collisions,
                        effects.get(
                            "current_shot_enemy_enemy_collisions",
                            0,
                        ),
                    )
                    if (
                        maximum_bonus > initial_bonus
                        and final_state.get("game_state")
                        == "aiming_direction"
                    ):
                        break

                print(
                    "collision_relic_probe="
                    + json.dumps(
                        {
                            "initial_attack": initial_attack,
                            "initial_bonus": initial_bonus,
                            "maximum_attack": maximum_attack,
                            "maximum_bonus": maximum_bonus,
                            "maximum_player_enemy_collisions":
                                maximum_player_enemy_collisions,
                            "maximum_enemy_enemy_collisions":
                                maximum_enemy_enemy_collisions,
                            "final_attack": final_state["player"].get(
                                "attack"
                            ),
                            "final_bonus": final_state[
                                "relic_effects"
                            ][
                                "current_shot_collision_attack_bonus"
                            ],
                            "final_game_state": final_state[
                                "game_state"
                            ],
                        },
                        ensure_ascii=False,
                    )
                )


def main() -> None:
    args = parse_arguments()
    asyncio.run(
        inspect_server(
            args.url,
            args.read_game_state,
            args.start_new_run,
            args.show_tool_schema,
            args.buy_relic_index,
            args.probe_collision_relic,
            args.probe_target_id,
        )
    )


if __name__ == "__main__":
    main()
