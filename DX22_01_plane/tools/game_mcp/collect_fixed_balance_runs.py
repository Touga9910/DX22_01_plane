"""ローカルゲームMCPサーバーを通じて、固定条件のバランスランを収集する。"""

from __future__ import annotations

import argparse
import asyncio
import json
from pathlib import Path
from typing import Any

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

from build_decision import build_decision_snapshot
from build_profiles import load_build_profiles, profile_settings_hash


DEFAULT_BUILD_PROFILES_PATH = (
    Path(__file__).resolve().parent / "build_profiles.json"
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8765/mcp")
    parser.add_argument(
        "--profile",
        required=True,
        choices=("beginner", "intermediate", "advanced"),
    )
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--maximum-steps", type=int, default=1200)
    parser.add_argument(
        "--run-seed",
        type=int,
        action="append",
        default=[],
        help=(
            "Force a run seed. Repeat to define a deterministic sequence; "
            "the sequence is cycled when --runs is larger."
        ),
    )
    parser.add_argument(
        "--validation-variant",
        default="",
        help="Force one configured balance-validation variant ID.",
    )
    parser.add_argument(
        "--build-profile",
        default="standard",
        help="Build policy: standard, heavy, pierce, bounce, or anchor.",
    )
    parser.add_argument(
        "--build-profiles",
        type=Path,
        default=DEFAULT_BUILD_PROFILES_PATH,
    )
    parser.add_argument(
        "--resume-current",
        action="store_true",
        help=(
            "Resume the current in-progress run for the first requested run; "
            "subsequent runs still start from result as usual."
        ),
    )
    return parser.parse_args()


def tool_result(result: Any) -> dict[str, Any]:
    structured = getattr(result, "structuredContent", None)
    if not isinstance(structured, dict):
        return {}
    value = structured.get("result", structured)
    return value if isinstance(value, dict) else {}


async def call(
    session: ClientSession,
    name: str,
    arguments: dict[str, Any] | None = None,
) -> dict[str, Any]:
    result = await session.call_tool(name, arguments or {})
    return tool_result(result)


async def get_state(session: ClientSession) -> dict[str, Any]:
    return await call(session, "get_game_state")


def select_route(
    state: dict[str, Any],
    build_policy: dict[str, Any],
) -> tuple[int, list[str]]:
    stage_choice = state.get("stage_choice", {})
    configured = build_policy.get("reward_priority", [])
    wanted = list(
        configured
        if isinstance(configured, list) and configured
        else stage_choice.get(
            "recommended_wanted_rewards",
            [
                "ball_upgrade",
                "new_ball",
                "money",
                "hp_recovery",
                "relic",
            ],
        )
    )
    route_options = state.get("route_options", [])
    route_index = int(route_options[0].get("route_index", 0))
    matches = stage_choice.get("reward_route_matches", {})
    for reward in wanted:
        indices = matches.get(reward, [])
        if indices:
            route_index = int(indices[0])
            break
    return route_index, wanted


def offered_ball_score(
    offer: dict[str, Any],
    build_policy: dict[str, Any],
    living_enemy_count: int,
) -> float:
    status = offer.get("status", {})
    weights = build_policy.get("ball_score_weights", {})
    if not isinstance(weights, dict):
        weights = {}
    attack = float(status.get("attack", 0))
    defense = float(status.get("defense", 0))
    mass = float(status.get("mass", 0))
    restitution = float(status.get("restitution", 0))
    pierce = bool(status.get("pierce", False))
    anchor = bool(status.get("anchor", False))
    definition_id = str(offer.get("definition_id", ""))
    definition_bonus = weights.get("definition_bonus", {})
    if not isinstance(definition_bonus, dict):
        definition_bonus = {}
    return (
        attack * float(weights.get("attack", 100.0))
        + defense * float(weights.get("defense", 2.0))
        + mass * float(weights.get("mass", 2.0))
        + restitution * float(weights.get("restitution", 5.0))
        + (
            float(weights.get("pierce_bonus", 0.0))
            if pierce and living_enemy_count > 1
            else 0.0
        )
        + (float(weights.get("anchor_bonus", 0.0)) if anchor else 0.0)
        + float(definition_bonus.get(definition_id, 0.0))
    )


def fallback_target(
    state: dict[str, Any],
    living: list[dict[str, Any]],
    profile: str,
) -> str:
    if profile == "beginner":
        position = state.get("player", {}).get("position", {"x": 0, "z": 0})
        living.sort(
            key=lambda enemy: (
                (float(enemy["position"]["x"]) - float(position["x"])) ** 2
                + (float(enemy["position"]["z"]) - float(position["z"])) ** 2
            )
        )
    else:
        living.sort(
            key=lambda enemy: (
                -int(enemy.get("attack", 0)),
                float(enemy.get("hp_ratio", 1.0)),
            )
        )
    return str(living[0]["target_id"])


def _priority_rank(
    definition_id: str,
    priorities: list[str],
) -> int:
    try:
        return priorities.index(definition_id)
    except ValueError:
        return len(priorities)


def upgrade_candidate(
    state: dict[str, Any],
    build_policy: dict[str, Any],
) -> dict[str, Any] | None:
    candidates = [
        ball for ball in state.get("deck_balls", [])
        if ball.get("can_upgrade", False)
    ]
    priorities = list(build_policy.get("upgrade_priority", []))
    candidates.sort(
        key=lambda ball: (
            _priority_rank(
                str(ball.get("definition_id", "")),
                priorities,
            ),
            int(ball.get("upgrade_level", 0)),
            -int(ball.get("status", {}).get("attack", 0)),
        )
    )
    return candidates[0] if candidates else None


def catalog_candidate(
    state: dict[str, Any],
    build_policy: dict[str, Any],
) -> dict[str, Any] | None:
    catalog = [
        ball
        for ball in state.get("catalog_balls", [])
        if isinstance(ball, dict)
    ]
    priorities = list(build_policy.get("new_ball_priority", []))
    catalog.sort(
        key=lambda ball: _priority_rank(
            str(ball.get("definition_id", "")),
            priorities,
        )
    )
    return catalog[0] if catalog else None


def choose_shot_type(
    state: dict[str, Any],
    build_policy: dict[str, Any],
    goal: str,
    shots_fired: int,
) -> str:
    control = state.get("mcp_control", {})
    if not bool(control.get("allow_bank_shots", False)) or goal != "damage":
        return "direct"
    shot_policy = build_policy.get("shot_policy", {})
    if not isinstance(shot_policy, dict):
        shot_policy = {}
    mode = str(shot_policy.get("bank_mode", "with_relic"))
    bank_owned = any(
        relic.get("name") == "Bank Shot" and relic.get("owned")
        for relic in state.get("relics", [])
    )
    if mode == "never":
        return "direct"
    if mode == "always":
        return "bank"
    if mode == "with_relic":
        return "bank" if bank_owned else "direct"
    interval = max(1, int(shot_policy.get("bank_every_n_shots", 3)))
    return "bank" if shots_fired % interval == interval - 1 else "direct"


async def play_current_run(
    session: ClientSession,
    profile: str,
    build_profile_id: str,
    build_policy: dict[str, Any],
    build_profiles: dict[str, dict[str, Any]],
    build_profile_hash: str,
    maximum_steps: int,
    run_number: int,
) -> dict[str, Any]:
    shots_fired = 0
    actions_taken = 0
    transient_errors = 0
    last_cleared = -1
    last_active_build_id = build_profile_id

    for _ in range(maximum_steps):
        state = await get_state(session)
        if not state:
            transient_errors += 1
            await asyncio.sleep(0.2)
            continue
        actions = set(state.get("available_actions", []))
        scene = str(state.get("scene", ""))
        decision = state.get("build_decision")
        if not isinstance(decision, dict):
            decision = build_decision_snapshot(
                state,
                build_profiles,
                build_profile_id,
            )
        active_build_id = str(
            decision.get("active_build_id", build_profile_id)
        )
        active_build_policy = build_profiles.get(
            active_build_id,
            build_policy,
        )
        last_active_build_id = active_build_id
        validation = state.get("balance_validation", {})
        cleared = int(validation.get("cleared_stage_count", 0))
        if cleared != last_cleared:
            last_cleared = cleared
            if cleared > 0 and cleared % 5 == 0:
                print(
                    json.dumps(
                        {
                            "event": "progress",
                            "run_number": run_number,
                            "profile": profile,
                            "build_profile": build_profile_id,
                            "variant": validation.get("variant_id"),
                            "seed_index": validation.get("seed_suite_index"),
                            "cleared": cleared,
                            "hp": state.get("player", {}).get("current_hp"),
                        },
                        ensure_ascii=False,
                    ),
                    flush=True,
                )

        if scene == "result":
            return {
                "completed": True,
                "profile": profile,
                "build_profile": build_profile_id,
                "active_build_profile": last_active_build_id,
                "build_profile_settings_hash": build_profile_hash,
                "variant": validation.get("variant_id"),
                "seed_index": validation.get("seed_suite_index"),
                "run_seed": validation.get("run_seed"),
                "cleared_stages": cleared,
                "progress": state.get("player", {}).get("progress"),
                "hp": state.get("player", {}).get("current_hp"),
                "money": state.get("player", {}).get("money"),
                "shots": shots_fired,
                "actions": actions_taken,
                "transient_errors": transient_errors,
            }

        try:
            if "choose_destination" in actions:
                route_index, wanted = select_route(
                    state,
                    active_build_policy,
                )
                await call(
                    session,
                    "choose_destination",
                    {
                        "route_index": route_index,
                        "wanted_rewards": wanted,
                    },
                )
                actions_taken += 1
                continue

            if (
                scene == "battle"
                and "fire_shot" in actions
                and state.get("game_state") == "aiming_direction"
            ):
                living = [
                    enemy for enemy in state.get("enemies", [])
                    if not enemy.get("defeated", False)
                    and not enemy.get("pocketed", False)
                ]
                if not living:
                    await asyncio.sleep(0.15)
                    continue
                offers = state.get("offered_balls", [])
                if "select_ball" in actions and offers:
                    recommended = (
                        decision.get("offered_ball_choices", {})
                        .get("recommended")
                    )
                    selected = next(
                        (
                            offer
                            for offer in offers
                            if isinstance(recommended, dict)
                            and int(offer.get("index", -1))
                            == int(recommended.get("index", -2))
                        ),
                        None,
                    )
                    if selected is None:
                        selected = max(
                            offers,
                            key=lambda offer: offered_ball_score(
                                offer,
                                active_build_policy,
                                len(living),
                            ),
                        )
                    selected_score = (
                        recommended.get("score", "fallback")
                        if isinstance(recommended, dict)
                        else "fallback"
                    )
                    ball_selection_reason = (
                        f"dynamic:{active_build_id}:"
                        "highest_explainable_build_score:"
                        f"{selected.get('definition_id', 'unknown')}:"
                        f"score={selected_score}"
                    )
                    if not selected.get("selected", False):
                        await call(
                            session,
                            "select_ball",
                            {
                                "offer_index": int(selected["index"]),
                                "decision_reason": ball_selection_reason,
                            },
                        )
                else:
                    ball_selection_reason = (
                        f"dynamic:{active_build_id}:only_available_ball"
                    )

                tactics = state.get("shot_tactics", {})
                target_id = tactics.get("recommended_target_id")
                if not target_id:
                    target_id = fallback_target(state, living, profile)
                power = float(tactics.get(
                    "evaluation_power",
                    sum(state.get("mcp_control", {}).get(
                        "recommended_power",
                        {"min": 3.0, "max": 6.0},
                    ).values()) / 2.0,
                ))
                goal = str(tactics.get("recommended_goal", "damage"))
                shot_type = choose_shot_type(
                    state,
                    active_build_policy,
                    goal,
                    shots_fired,
                )
                arguments: dict[str, Any] = {
                    "target_id": target_id,
                    "power": power,
                    "power_mode": "manual",
                    "shot_type": shot_type,
                    "shot_goal": "auto",
                    "ball_selection_reason": ball_selection_reason,
                }
                if shot_type == "bank":
                    arguments["wall_index"] = -1
                shot = await call(session, "fire_shot", arguments)
                if shot.get("ok", False):
                    shots_fired += 1
                    actions_taken += 1
                else:
                    transient_errors += 1
                continue

            if "choose_reward" in actions:
                reward_choice = (
                    decision.get("clear_reward", {})
                    .get("recommended")
                )
                if not isinstance(reward_choice, dict):
                    reward_choice = {
                        "reward": "extra_money",
                        "catalog_index": -1,
                        "instance_id": 0,
                        "score": 0.0,
                    }
                reward_arguments: dict[str, Any] = {
                    "reward": str(reward_choice["reward"]),
                    "decision_reason": (
                        f"dynamic:{active_build_id}:"
                        f"score={reward_choice.get('score', 0)}:"
                        f"{reward_choice.get('reason', 'fallback')}"
                    ),
                }
                if reward_choice["reward"] == "new_ball":
                    reward_arguments["catalog_index"] = int(
                        reward_choice.get("catalog_index", -1)
                    )
                elif reward_choice["reward"] == "upgrade_ball":
                    reward_arguments["instance_id"] = int(
                        reward_choice.get("instance_id", 0)
                    )
                await call(
                    session,
                    "choose_reward",
                    reward_arguments,
                )
                actions_taken += 1
                continue

            if "continue_after_reward" in actions:
                await call(session, "continue_after_reward")
                actions_taken += 1
                continue

            if scene == "rest_site":
                player = state.get("player", {})
                hp_ratio = float(player.get("current_hp", 0)) / max(
                    float(player.get("max_hp", 1)),
                    1.0,
                )
                if "heal" in actions and hp_ratio < 0.4:
                    await call(session, "heal")
                    actions_taken += 1
                    continue
                candidate = (
                    decision.get("upgrade_choices", {})
                    .get("recommended")
                )
                if not isinstance(candidate, dict):
                    candidate = upgrade_candidate(
                        state,
                        active_build_policy,
                    )
                if "upgrade_ball" in actions and candidate:
                    await call(
                        session,
                        "upgrade_ball",
                        {
                            "instance_id": int(candidate["instance_id"]),
                            "decision_reason": (
                                f"dynamic:{active_build_id}:"
                                "rest_priority_upgrade:"
                                f"{candidate.get('definition_id', 'unknown')}"
                            ),
                        },
                    )
                    actions_taken += 1
                    continue
                if "heal" in actions:
                    await call(session, "heal")
                    actions_taken += 1
                    continue
                if "continue_to_battle" in actions:
                    await call(session, "continue_to_battle")
                    actions_taken += 1
                    continue

            if scene == "shop":
                if "buy_relic" in actions:
                    choice = (
                        decision.get("relic_choices", {})
                        .get("recommended")
                    )
                    if isinstance(choice, dict):
                        await call(
                            session,
                            "buy_relic",
                            {"relic_index": int(choice["index"])},
                        )
                        actions_taken += 1
                        continue
                if "continue_to_battle" in actions:
                    await call(session, "continue_to_battle")
                    actions_taken += 1
                    continue

            await asyncio.sleep(0.15)
        except Exception:
            transient_errors += 1
            await asyncio.sleep(0.3)

    state = await get_state(session)
    return {
        "completed": False,
        "reason": "maximum_steps",
        "profile": profile,
        "build_profile": build_profile_id,
        "active_build_profile": last_active_build_id,
        "build_profile_settings_hash": build_profile_hash,
        "scene": state.get("scene"),
        "game_state": state.get("game_state"),
        "validation": state.get("balance_validation"),
        "player": state.get("player"),
        "shots": shots_fired,
        "actions": actions_taken,
        "transient_errors": transient_errors,
    }


async def collect(args: argparse.Namespace) -> None:
    summaries: list[dict[str, Any]] = []
    build_policy = args.loaded_build_profiles[args.build_profile]
    build_profile_hash = profile_settings_hash(build_policy)
    async with streamable_http_client(args.url) as streams:
        read_stream, write_stream, _ = streams
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()
            await call(
                session,
                "set_player_level",
                {"player_level": args.profile},
            )
            initial_state = await get_state(session)
            if args.resume_current:
                active_build_state = (
                    initial_state.get("mcp_control", {})
                    .get("build_profile", {})
                )
                active_build = active_build_state.get("id")
                if active_build != args.build_profile:
                    raise RuntimeError(
                        "--resume-current build profile mismatch: "
                        f"active={active_build}, requested={args.build_profile}"
                    )
                active_hash = active_build_state.get("settings_hash")
                if active_hash != build_profile_hash:
                    raise RuntimeError(
                        "--resume-current build settings mismatch: "
                        f"active={active_hash}, local={build_profile_hash}"
                    )
            else:
                selection = await call(
                    session,
                    "set_build_profile",
                    {"build_profile": args.build_profile},
                )
                selected_build = (
                    selection.get("mcp_control", {})
                    .get("build_profile", {})
                )
                if selected_build.get("id") != args.build_profile:
                    raise RuntimeError(
                        "MCP server did not activate requested build profile"
                    )
                if selected_build.get("settings_hash") != build_profile_hash:
                    raise RuntimeError(
                        "MCP server and collector use different build "
                        "profile settings"
                    )
            for run_number in range(1, args.runs + 1):
                state = await get_state(session)
                resume_this_run = args.resume_current and run_number == 1
                if resume_this_run:
                    if state.get("scene") in {"title", "result"}:
                        raise RuntimeError(
                            "--resume-current requires an in-progress run: "
                            f"scene={state.get('scene')}"
                        )
                else:
                    if state.get("scene") not in {"title", "result"}:
                        raise RuntimeError(
                            "Collector requires title/result before each new run: "
                            f"scene={state.get('scene')}"
                        )
                    start_arguments: dict[str, Any] = {}
                    if args.run_seed:
                        start_arguments["run_seed"] = args.run_seed[
                            (run_number - 1) % len(args.run_seed)
                        ]
                    if args.validation_variant:
                        start_arguments["validation_variant"] = (
                            args.validation_variant
                        )
                    start_result = await call(
                        session,
                        "start_new_run",
                        start_arguments,
                    )
                    if not bool(start_result.get("ok", False)):
                        raise RuntimeError(
                            "start_new_run failed: "
                            f"{start_result.get('message', 'unknown error')}"
                        )
                summary = await play_current_run(
                    session,
                    args.profile,
                    args.build_profile,
                    build_policy,
                    args.loaded_build_profiles,
                    build_profile_hash,
                    args.maximum_steps,
                    run_number,
                )
                summaries.append(summary)
                print(
                    json.dumps(
                        {"event": "run_complete", **summary},
                        ensure_ascii=False,
                    ),
                    flush=True,
                )
                if not summary.get("completed", False):
                    break
    print(
        json.dumps(
            {
                "event": "collection_complete",
                "requested_runs": args.runs,
                "completed_runs": sum(
                    bool(summary.get("completed")) for summary in summaries
                ),
                "profile": args.profile,
                "build_profile": args.build_profile,
                "build_profile_settings_hash": build_profile_hash,
                "runs": summaries,
            },
            ensure_ascii=False,
        ),
        flush=True,
    )


def main() -> None:
    args = parse_arguments()
    if args.runs <= 0:
        raise SystemExit("--runs must be positive")
    if any(seed < 0 or seed > 0xFFFFFFFF for seed in args.run_seed):
        raise SystemExit("--run-seed must be between 0 and 4294967295")
    profiles, _ = load_build_profiles(args.build_profiles.resolve())
    if args.build_profile not in profiles:
        raise SystemExit(
            "--build-profile must be standard, heavy, pierce, bounce, "
            "or anchor"
        )
    args.loaded_build_profiles = profiles
    asyncio.run(collect(args))


if __name__ == "__main__":
    main()
