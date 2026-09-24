"""ローカルゲームMCPサーバーを通じて、固定条件のバランスランを収集する。"""

from __future__ import annotations

import argparse
import asyncio
import csv
import hashlib
import json
import os
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Awaitable, Callable

from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

from build_decision import (
    build_decision_snapshot,
    build_progress_snapshot,
    evaluate_relic_choices,
)
from build_profiles import load_build_profiles, profile_settings_hash


DEFAULT_BUILD_PROFILES_PATH = (
    Path(__file__).resolve().parent / "build_profiles.json"
)
DEFAULT_BALANCE_LOG_DIRECTORY = Path(__file__).resolve().parents[2] / "logs" / "balance"
COMPARISON_BUILDS = ("standard", "heavy", "pierce", "bounce", "anchor")
COMPARISON_SEEDS = tuple(range(1001, 1021))
COMPARISON_REFERENCE_COHORT = "cohort_20260921_mcp_intermediate_standard"
MCP_CONNECTION_MAX_ATTEMPTS = 5
MCP_CONNECTION_RETRY_BASE_SECONDS = 0.25
BUILD_TARGET_BALLS = {
    "standard": ("player_standard",),
    "heavy": ("player_heavy", "player_chain_impact"),
    "pierce": (
        "player_pierce",
        "player_trace_driver",
        "player_pierce_finisher",
        "player_refractive_pierce",
    ),
    "bounce": (
        "player_bounce",
        "player_cushion_charge",
        "player_ricochet_finisher",
    ),
    "anchor": (
        "player_anchor",
        "player_stop_shield",
        "player_anchor_finisher",
    ),
}
BUILD_CORE_BALLS = {
    "standard": ("player_standard",),
    "heavy": ("player_heavy", "player_chain_impact"),
    "pierce": (
        "player_pierce",
        "player_trace_driver",
        "player_pierce_finisher",
    ),
    "bounce": (
        "player_bounce",
        "player_cushion_charge",
        "player_ricochet_finisher",
    ),
    "anchor": (
        "player_anchor",
        "player_stop_shield",
        "player_anchor_finisher",
    ),
}
BUILD_MECHANIC_EVENTS = {
    "standard": (),
    "heavy": (
        "heavy_collision_recorded",
        "heavy_finisher_triggered",
        "chain_impact_triggered",
    ),
    "pierce": (
        "pierce_trace_generated",
        "pierce_trace_used",
        "pierce_finisher_hit",
    ),
    "bounce": (
        "cushion_stacks_generated",
        "cushion_stacks_consumed",
        "ricochet_finisher_triggered",
        "bank_shot_triggered",
    ),
    "anchor": (
        "anchor_stacks_generated",
        "anchor_enemy_transfer",
        "stop_shield_granted",
        "stop_shield_absorbed",
        "anchor_finisher_triggered",
    ),
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8765/mcp")
    parser.add_argument(
        "--profile",
        default="intermediate",
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
    parser.add_argument(
        "--comparison-suite",
        action="store_true",
        help=(
            "Collect the fixed 100-run intermediate comparison: seeds "
            "1001-1020 for standard/heavy/pierce/bounce/anchor."
        ),
    )
    parser.add_argument(
        "--output-directory",
        type=Path,
        help=(
            "Comparison output directory. Existing all_runs.jsonl is resumed "
            "without repeating accepted seed/build pairs."
        ),
    )
    parser.add_argument(
        "--comparison-seed",
        type=int,
        action="append",
        default=[],
        help="Comparison seed. Repeat to override the default 1001-1020 suite.",
    )
    parser.add_argument(
        "--comparison-build",
        action="append",
        choices=COMPARISON_BUILDS,
        default=[],
        help="Comparison build. Repeat to override the default five builds.",
    )
    parser.add_argument(
        "--valid-runs-per-build",
        type=int,
        default=20,
        help=(
            "Required accepted runs for each comparison build. For paired "
            "comparison this must equal the number of comparison seeds."
        ),
    )
    parser.add_argument(
        "--balance-log-directory",
        type=Path,
        default=DEFAULT_BALANCE_LOG_DIRECTORY,
        help="Directory where the game writes run_*.json balance logs.",
    )
    parser.add_argument(
        "--run-log-timeout",
        type=float,
        default=15.0,
        help="Seconds to wait for a completed run log to be finalized.",
    )
    return parser.parse_args()


def tool_result(result: Any) -> dict[str, Any]:
    if getattr(result, "isError", False):
        message = " ".join(
            item.text for item in getattr(result, "content", [])
            if hasattr(item, "text")
        )
        return {"ok": False, "reason": "mcp_tool_error", "retryable": True,
                "message": message}
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
    try:
        result = await session.call_tool(name, arguments or {})
    except Exception as error:
        if name == "get_game_state":
            raise
        parsed = {
            "ok": False,
            "reason": "mcp_tool_error",
            "retryable": True,
            "message": repr(error),
        }
        try:
            confirmation = await session.call_tool("get_game_state", {})
            parsed["state_after_error"] = tool_result(confirmation)
        except Exception as confirmation_error:
            parsed["state_after_error"] = {}
            parsed["state_confirmation_error"] = repr(confirmation_error)
            if (
                is_transient_connection_error(error)
                or is_transient_connection_error(confirmation_error)
            ):
                raise confirmation_error from error
        return parsed
    parsed = tool_result(result)
    if name != "get_game_state" and parsed.get("reason") == "mcp_tool_error":
        # A timed-out write may still have reached the game.  Never resend it
        # blindly: observe authoritative state first and let the caller decide
        # whether the requested transition already happened.
        confirmation = await session.call_tool("get_game_state", {})
        parsed["state_after_error"] = tool_result(confirmation)
    return parsed


async def get_state(session: ClientSession) -> dict[str, Any]:
    return await call(session, "get_game_state")


def response_player_level(response: dict[str, Any]) -> str:
    """Read the current and legacy response envelopes without weakening checks."""
    candidates = (
        _as_dict(response.get("mcp_control")).get("player_level"),
        response.get("player_level"),
        _as_dict(_as_dict(response.get("result")).get("mcp_control")).get(
            "player_level"
        ),
    )
    return next(
        (str(value) for value in candidates if isinstance(value, str) and value),
        "",
    )


async def set_and_confirm_player_level(
    session: ClientSession,
    expected_level: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Set the level, then confirm it from a fresh authoritative state."""
    initial_state = await get_state(session)
    if response_player_level(initial_state) == expected_level:
        return {"ok": True, "already_configured": True}, initial_state
    set_result = await call(
        session, "set_player_level", {"player_level": expected_level}
    )
    if not set_result.get("ok", False):
        recovered_level = response_player_level(
            _as_dict(set_result.get("state_after_error"))
        )
        if recovered_level != expected_level:
            raise RuntimeError(
                "set_player_level failed before comparison: "
                f"requested={expected_level}, response={set_result}"
            )

    confirmed_state = await get_state(session)
    confirmed_level = response_player_level(confirmed_state)
    if confirmed_level != expected_level:
        raise RuntimeError(
            "MCP server did not confirm requested player level: "
            f"requested={expected_level}, confirmed={confirmed_level or 'missing'}"
        )
    return set_result, confirmed_state


def is_terminal_summary(summary: dict[str, Any]) -> bool:
    """A loss ends a run normally and must not stop a collection batch."""
    return bool(summary.get("completed")) or (
        summary.get("reason") == "run_ended_before_boss_defeat"
    )


def shot_debug(event: str, **details: Any) -> None:
    if os.environ.get("GAME_MCP_SHOT_DEBUG", "").lower() in ("1", "true", "yes"):
        print("[MCP Shot] " + json.dumps({"event": event, **details}, ensure_ascii=False), flush=True)


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


def midboss_relic_candidate(
    state: dict[str, Any],
    build_profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str,
) -> dict[str, Any] | None:
    # Keep owned relics as build evidence; only offered, unowned relics
    # are eligible for this free reward, regardless of their shop prices.
    reward_state = dict(state)
    reward_state["relics"] = [
        dict(relic, price=0)
        for relic in state.get("relics", [])
        if isinstance(relic, dict)
        and (relic.get("owned", False) or relic.get("midboss_offered", False))
    ]
    return evaluate_relic_choices(
        reward_state, build_profiles, preferred_profile_id,
    ).get("recommended")


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
    state_observer: Callable[[dict[str, Any]], None] | None = None,
) -> dict[str, Any]:
    shots_fired = 0
    actions_taken = 0
    transient_errors = 0
    last_cleared = -1
    last_active_build_id = build_profile_id
    initial_deck_state: list[dict[str, Any]] | None = None
    reward_selection_history: list[dict[str, Any]] = []

    for _ in range(maximum_steps):
        state = await get_state(session)
        if state_observer is not None:
            state_observer(state)
        if not state:
            transient_errors += 1
            await asyncio.sleep(0.2)
            continue
        if initial_deck_state is None:
            initial_deck_state = _normalized_deck(
                state.get("deck_balls", [])
            )
        actions = set(state.get("available_actions", []))
        scene = str(state.get("scene", ""))
        decision = state.get("build_decision")
        if not isinstance(decision, dict):
            decision = build_decision_snapshot(
                state,
                build_profiles,
                build_profile_id,
            )
        # The requested comparison build remains the objective for the whole
        # run. Starting Standard copies are not allowed to switch that goal.
        active_build_id = build_profile_id
        active_build_policy = build_policy
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
            run_progress = state.get("run_progress", {})
            boss_defeated = bool(run_progress.get("final_boss_defeated", False))
            return {
                "completed": boss_defeated,
                "reason": "final_boss_defeated" if boss_defeated else "run_ended_before_boss_defeat",
                "profile": profile,
                "build_profile": build_profile_id,
                "active_build_profile": last_active_build_id,
                "build_profile_settings_hash": build_profile_hash,
                "variant": validation.get("variant_id"),
                "seed_index": validation.get("seed_suite_index"),
                "run_seed": validation.get("run_seed"),
                "cleared_stages": cleared,
                "run_progress": run_progress,
                "progress": state.get("player", {}).get("progress"),
                "hp": state.get("player", {}).get("current_hp"),
                "money": state.get("player", {}).get("money"),
                "shots": shots_fired,
                "actions": actions_taken,
                "transient_errors": transient_errors,
                "initial_deck_state": initial_deck_state or [],
                "final_deck_state": _normalized_deck(
                    state.get("deck_balls", [])
                ),
                "reward_selection_history": reward_selection_history,
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

            if state.get("boss_state") and "fire_boss_shot" in actions:
                evaluation = state.get("boss_shot_choices") or await call(session, "evaluate_boss_shots")
                choice = evaluation.get("recommended")
                if not choice:
                    return {"completed": False, "reason": "boss_planning_unavailable", "shots": shots_fired}
                result = await call(session, "fire_boss_shot", {
                    "candidate_id": choice["candidate_id"], "state_key": evaluation["state_key"],
                })
                if result.get("ok"):
                    shots_fired += 1
                    actions_taken += 1
                else:
                    transient_errors += 1
                await asyncio.sleep(0.15)
                continue

            if (
                scene == "battle"
                and "fire_shot" in actions
                and state.get("game_state") == "aiming_direction"
            ):
                next_action = state.get("recommended_action")
                if not isinstance(next_action, dict):
                    transient_errors += 1
                    await asyncio.sleep(0.15)
                    continue
                selected = next((offer for offer in state.get("offered_balls", [])
                                 if offer.get("selected")), None)
                shot_debug("autoplay_decision", sequence=state.get("sequence"),
                           selected_ball=selected.get("definition_id") if selected else None,
                           selected_instance_id=selected.get("instance_id") if selected else None,
                           before_candidates=state.get("shot_tactics", {}).get("recommendations", []),
                           next_action=next_action)
                if next_action["action"] == "none":
                    return {"completed": False, "reason": "no_reachable_shot",
                            "shots": shots_fired, "scene": scene,
                            "selected_ball": selected.get("definition_id") if selected else None}
                if next_action["action"] == "select_ball":
                    selection = await call(session, "select_ball", {
                        "offer_index": int(next_action["ball_index"]),
                        "decision_reason": f"dynamic:{active_build_id}:reachable_fallback",
                    })
                    if selection.get("ok"):
                        actions_taken += 1
                        refreshed = await get_state(session)
                        shot_debug("selection_recomputed", previous_sequence=state.get("sequence"),
                                   current_sequence=refreshed.get("sequence"),
                                   after_candidates=refreshed.get("shot_tactics", {}).get("recommendations", []),
                                   recommended_action=refreshed.get("recommended_action"))
                    else:
                        transient_errors += 1
                        await asyncio.sleep(0.15)
                    continue
                if next_action["action"] != "fire_shot":
                    transient_errors += 1
                    await asyncio.sleep(0.15)
                    continue
                arguments: dict[str, Any] = {
                    "target_id": next_action["target_id"],
                    "power_mode": "auto",
                    "shot_type": next_action["shot_type"],
                    "shot_goal": next_action["shot_goal"],
                    "wall_index": next_action.get("wall_index", -1),
                    "pocket_index": next_action.get("pocket_index", -1),
                    "ball_selection_reason": f"dynamic:{active_build_id}:selected_reachable_ball",
                }
                shot = await call(session, "fire_shot", arguments)
                if shot.get("ok", False):
                    shots_fired += 1
                    actions_taken += 1
                else:
                    transient_errors += 1
                    shot_debug("autoplay_retry", reason=shot.get("reason"),
                               requested=arguments, fallback=shot.get("next_action"))
                    if shot.get("reason") == "no_reachable_shot":
                        return {"completed": False, "reason": "no_reachable_shot",
                                "shots": shots_fired, "scene": scene}
                    await asyncio.sleep(0.15)
                continue

            if "choose_relic" in actions:
                relic_choice = midboss_relic_candidate(
                    state, build_profiles, active_build_id,
                )
                if relic_choice is None:
                    raise ValueError("No unowned midboss reward is offered.")
                result = await call(
                    session,
                    "choose_relic",
                    {
                        "relic_index": int(relic_choice["index"]),
                        "decision_reason": (
                            f"dynamic:{active_build_id}:free_midboss_reward:"
                            f"score={relic_choice['score']}:"
                            f"{relic_choice['canonical_name']}"
                        ),
                    },
                )
                if result.get("ok", False):
                    actions_taken += 1
                else:
                    transient_errors += 1
                    await asyncio.sleep(0.3)
                continue

            if "choose_reward" in actions:
                reward_decision = decision.get("clear_reward", {})
                if not isinstance(reward_decision, dict):
                    reward_decision = {}
                reward_choice = reward_decision.get("recommended")
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
                progress_before = build_progress_snapshot(
                    state,
                    build_profiles,
                    build_profile_id,
                )
                history_entry: dict[str, Any] = {
                    "ordinal": len(reward_selection_history) + 1,
                    "area": state.get("player", {}).get("progress"),
                    "progress": state.get("player", {}).get("progress"),
                    "construction_phase": progress_before["phase"],
                    "build_completion_before": progress_before,
                    "missing_key_parts": progress_before["missing_key_parts"],
                    "new_ball_candidates": reward_decision.get(
                        "new_ball_choices", []
                    ),
                    "upgrade_candidates": reward_decision.get(
                        "upgrade_choices", []
                    ),
                    "reward_options": reward_decision.get("options", []),
                    "selected": dict(reward_choice),
                    "selected_reward": reward_choice.get("reward"),
                    "selected_definition_id": reward_choice.get(
                        "definition_id"
                    ),
                    "selected_score": reward_choice.get("score"),
                    "selected_score_breakdown": reward_choice.get(
                        "breakdown", {}
                    ),
                    "choice_reason": reward_choice.get("reason", "fallback"),
                    "chosen_build_id": build_profile_id,
                }
                reward_arguments["decision_details"] = history_entry
                if reward_choice["reward"] == "new_ball":
                    offer_index = int(reward_choice.get("offer_index", -1))
                    if offer_index >= 0:
                        reward_arguments["offer_index"] = offer_index
                    else:
                        reward_arguments["catalog_index"] = int(
                            reward_choice.get("catalog_index", -1)
                        )
                elif reward_choice["reward"] == "upgrade_ball":
                    reward_arguments["instance_id"] = int(
                        reward_choice.get("instance_id", 0)
                    )
                reward_result = await call(
                    session,
                    "choose_reward",
                    reward_arguments,
                )
                if reward_result.get("ok", False):
                    actions_taken += 1
                    state_after_reward = await get_state(session)
                    if state_observer is not None:
                        state_observer(state_after_reward)
                    history_entry["build_completion_after"] = (
                        build_progress_snapshot(
                            state_after_reward,
                            build_profiles,
                            build_profile_id,
                        )
                    )
                    history_entry["deck_after"] = _normalized_deck(
                        state_after_reward.get("deck_balls", [])
                    )
                    history_entry["accepted"] = True
                else:
                    # The next loop starts with get_game_state; do not resend
                    # a state-changing operation immediately after an error.
                    history_entry["accepted"] = False
                    history_entry["error"] = reward_result
                    transient_errors += 1
                reward_selection_history.append(history_entry)
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
        except Exception as error:
            if is_transient_connection_error(error):
                raise
            transient_errors += 1
            shot_debug("autoplay_exception", scene=scene,
                       game_state=state.get("game_state"),
                       error=repr(error))
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
        "initial_deck_state": initial_deck_state or [],
        "final_deck_state": _normalized_deck(state.get("deck_balls", [])),
        "reward_selection_history": reward_selection_history,
    }


def _as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


class McpRetryExhausted(ConnectionError):
    """A bounded MCP reconnect sequence could not restore communication."""

    def __init__(self, operation: str, attempts: int, error: BaseException) -> None:
        super().__init__(
            f"{operation} failed after {attempts} MCP connection attempts: {error!r}"
        )
        self.operation = operation
        self.attempts = attempts
        self.last_error = error


def _exception_leaves(error: BaseException) -> list[BaseException]:
    nested = getattr(error, "exceptions", None)
    if isinstance(nested, tuple):
        leaves: list[BaseException] = []
        for child in nested:
            if isinstance(child, BaseException):
                leaves.extend(_exception_leaves(child))
        return leaves
    return [error]


def is_transient_connection_error(error: BaseException) -> bool:
    """Recognize transport failures, including those wrapped by TaskGroup."""
    leaves = _exception_leaves(error)
    transient_names = {
        "ConnectError",
        "ConnectTimeout",
        "ReadTimeout",
        "WriteTimeout",
        "PoolTimeout",
        "ReadError",
        "WriteError",
        "CloseError",
        "RemoteProtocolError",
        "EndOfStream",
        "BrokenResourceError",
        "ClosedResourceError",
    }
    saw_transient = False
    for leaf in leaves:
        if isinstance(leaf, (ConnectionError, TimeoutError, asyncio.TimeoutError)):
            saw_transient = True
            continue
        if type(leaf).__name__ in transient_names:
            saw_transient = True
            continue
        # Task-group cancellation is a consequence of a sibling transport
        # failure, not a separate collector/programming error.
        if type(leaf).__name__ in {"CancelledError", "Cancelled"}:
            continue
        return False
    return saw_transient


def exception_details(error: BaseException) -> dict[str, str]:
    leaves = _exception_leaves(error)
    representative = next(
        (leaf for leaf in leaves if type(leaf).__name__ not in {"CancelledError", "Cancelled"}),
        error,
    )
    return {
        "exception_type": type(representative).__name__,
        "exception_message": str(representative),
    }


async def _run_mcp_session_operation(
    url: str,
    operation: Callable[[ClientSession], Awaitable[Any]],
) -> Any:
    async with streamable_http_client(url) as streams:
        read_stream, write_stream, _ = streams
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()
            return await operation(session)


async def run_mcp_operation_with_retries(
    url: str,
    operation_name: str,
    operation: Callable[[ClientSession], Awaitable[Any]],
    *,
    max_attempts: int = MCP_CONNECTION_MAX_ATTEMPTS,
    base_delay: float = MCP_CONNECTION_RETRY_BASE_SECONDS,
    on_retry: Callable[[dict[str, Any]], None] | None = None,
) -> Any:
    """Run one state-aware operation in fresh sessions with bounded backoff."""
    attempts = max(1, max_attempts)
    for attempt in range(1, attempts + 1):
        try:
            return await _run_mcp_session_operation(url, operation)
        except BaseException as error:
            if not is_transient_connection_error(error):
                raise
            details: dict[str, Any] = {
                **exception_details(error),
                "operation": operation_name,
                "retry_count": attempt,
                "max_attempts": attempts,
                "timestamp": _utc_now(),
            }
            if on_retry is not None:
                on_retry(details)
            if attempt >= attempts:
                raise McpRetryExhausted(operation_name, attempts, error) from error
            await asyncio.sleep(base_delay * (2 ** (attempt - 1)))
    raise AssertionError("unreachable MCP retry loop")


def _as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def _utc_now() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _mean(values: list[int | float]) -> float | None:
    return round(statistics.fmean(values), 4) if values else None


def _median(values: list[int | float]) -> float | None:
    return round(float(statistics.median(values)), 4) if values else None


def _rate(numerator: int | float, denominator: int | float) -> float:
    return round(numerator / denominator, 4) if denominator else 0.0


def configuration_signature(run: dict[str, Any]) -> str:
    """Hash build identity and the logger's normalized configuration manifest."""
    files = []
    for item in _as_list(_as_dict(run.get("configuration")).get("files")):
        entry = _as_dict(item)
        files.append({
            "path": str(entry.get("path", "")).replace("\\", "/").lower(),
            "exists": bool(entry.get("exists", False)),
            "fnv1a64": str(entry.get("fnv1a64", "")).lower(),
            "size_bytes": int(entry.get("size_bytes", 0)),
        })
    files.sort(key=lambda entry: entry["path"])
    payload = {
        "build": _as_dict(run.get("build")),
        "configuration": files,
        "schema_version": run.get("schema_version"),
    }
    encoded = json.dumps(
        payload, ensure_ascii=True, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _event_details(event: Any) -> dict[str, Any]:
    return _as_dict(_as_dict(event).get("details"))


def _event_progress(event: Any) -> int | None:
    value = _as_dict(event).get("stage_index")
    return int(value) if isinstance(value, (int, float)) else None


def _final_deck(run: dict[str, Any]) -> list[dict[str, Any]]:
    deck = _as_list(_as_dict(run.get("initial_player")).get("deck"))
    for stage in _as_list(run.get("stages")):
        candidate = _as_list(_as_dict(_as_dict(stage).get("stage_context")).get("deck"))
        if candidate:
            deck = candidate
    return [_as_dict(ball) for ball in deck]


def _normalized_deck(deck: list[Any]) -> list[dict[str, Any]]:
    normalized = []
    for ordinal, raw_ball in enumerate(deck):
        ball = _as_dict(raw_ball)
        definition_id = str(
            ball.get("definition_id", ball.get("id", ""))
        )
        if not definition_id:
            continue
        normalized.append(
            {
                "instance_id": int(ball.get("instance_id", ordinal)),
                "definition_id": definition_id,
                "upgrade_level": int(
                    ball.get("upgrade_level", ball.get("level", 0))
                ),
            }
        )
    return normalized


def _deck_by_definition(deck: list[dict[str, Any]]) -> dict[str, Any]:
    grouped: dict[str, dict[str, Any]] = {}
    for ball in deck:
        definition_id = str(ball.get("definition_id", ""))
        if not definition_id:
            continue
        item = grouped.setdefault(
            definition_id,
            {"count": 0, "total_upgrade_level": 0, "max_upgrade_level": 0},
        )
        level = int(ball.get("upgrade_level", 0))
        item["count"] += 1
        item["total_upgrade_level"] += level
        item["max_upgrade_level"] = max(item["max_upgrade_level"], level)
    return dict(sorted(grouped.items()))


def _reward_metrics(
    events: list[Any],
    build_profile: str,
) -> tuple[Counter[str], Counter[str], int, int, list[int]]:
    target_ids = set(BUILD_TARGET_BALLS[build_profile])
    choices: Counter[str] = Counter()
    acquired: Counter[str] = Counter()
    targeted_offer_count = 0
    targeted_offer_not_taken_count = 0
    acquisition_progress: list[int] = []
    pending_offer: dict[str, Any] | None = None
    pending_is_targeted = False

    for event in events:
        event_type = str(_as_dict(event).get("event_type", ""))
        details = _event_details(event)
        if event_type == "clear_reward_offered":
            pending_offer = details
            new_ids = {
                str(_as_dict(item).get("ball_id", ""))
                for item in _as_list(details.get("new_ball_candidates"))
            }
            upgrade_ids = {
                str(_as_dict(item).get("ball_id", ""))
                for item in _as_list(details.get("upgrade_candidates"))
            }
            pending_is_targeted = bool(target_ids & (new_ids | upgrade_ids))
            targeted_offer_count += int(pending_is_targeted)
            continue
        if event_type != "clear_reward_choice":
            continue

        reward = str(details.get("reward", "unknown"))
        choices[reward] += 1
        selected_target = False
        chosen_ball_id = str(details.get("ball_id", ""))
        if chosen_ball_id:
            selected_target = chosen_ball_id in target_ids
            if reward == "new_ball":
                acquired[chosen_ball_id] += 1
                progress = _event_progress(event)
                if progress is not None:
                    acquisition_progress.append(progress)
        if pending_offer is not None and reward in {"new_ball", "upgrade_ball"}:
            candidate_key = (
                "new_ball_candidates" if reward == "new_ball" else "upgrade_candidates"
            )
            candidates = _as_list(pending_offer.get(candidate_key))
            selected_index = int(details.get("selected_index", -1))
            if not chosen_ball_id and 0 <= selected_index < len(candidates):
                ball_id = str(_as_dict(candidates[selected_index]).get("ball_id", ""))
                selected_target = ball_id in target_ids
                if reward == "new_ball" and ball_id:
                    acquired[ball_id] += 1
                    progress = _event_progress(event)
                    if progress is not None:
                        acquisition_progress.append(progress)
        if pending_is_targeted and not selected_target:
            targeted_offer_not_taken_count += 1
        pending_offer = None
        pending_is_targeted = False

    return (
        choices,
        acquired,
        targeted_offer_count,
        targeted_offer_not_taken_count,
        acquisition_progress,
    )


def extract_run_record(
    run: dict[str, Any],
    summary: dict[str, Any],
    log_path: Path | str,
) -> dict[str, Any]:
    """Convert one finalized game log into the comparison's stable schema."""
    context = _as_dict(run.get("run_context"))
    controller = _as_dict(run.get("controller"))
    validation = _as_dict(context.get("validation"))
    randomness = _as_dict(context.get("randomness"))
    result = _as_dict(run.get("run_result"))
    events = _as_list(run.get("events"))
    stages = [_as_dict(stage) for stage in _as_list(run.get("stages"))]
    build_profile = str(
        controller.get("build_profile", context.get("build_profile", ""))
    )
    if build_profile not in BUILD_TARGET_BALLS:
        build_profile = str(summary.get("build_profile", build_profile))

    shots = [
        _as_dict(shot)
        for stage in stages
        for shot in _as_list(stage.get("shots"))
    ]
    ball_usage = Counter(str(shot.get("ball_id", "unknown")) for shot in shots)
    damage_total = sum(int(shot.get("total_enemy_damage", 0)) for shot in shots)
    collision_count = sum(
        int(shot.get("total_damage_collision_count", 0)) for shot in shots
    )
    chain_count = sum(int(shot.get("enemy_enemy_hit_count", 0)) for shot in shots)

    enemy_move_values = [
        int(shot["enemy_move_count"])
        for shot in shots
        if isinstance(shot.get("enemy_move_count"), (int, float))
    ]
    enemy_distance_values = [
        float(shot["enemy_move_distance"])
        for shot in shots
        if isinstance(shot.get("enemy_move_distance"), (int, float))
    ]

    event_counts = Counter(
        str(_as_dict(event).get("event_type", "unknown")) for event in events
    )
    mechanic_counts = {
        name: event_counts[name]
        for name in BUILD_MECHANIC_EVENTS.get(build_profile, ())
    }
    if build_profile == "standard":
        mechanic_counts["standard_direct_damage_shot"] = sum(
            1
            for shot in shots
            if shot.get("ball_id") == "player_standard"
            and int(shot.get("player_enemy_damage", 0)) > 0
        )

    choices, acquired, targeted_offers, targeted_not_taken, acquired_progress = (
        _reward_metrics(events, build_profile)
    )
    upgrade_counts = Counter()
    purchased_relics: list[str] = []
    for event in events:
        event_type = str(_as_dict(event).get("event_type", ""))
        details = _event_details(event)
        if event_type == "ball_upgraded":
            upgrade_counts[str(details.get("ball_id", "unknown"))] += 1
        elif event_type == "relic_purchased":
            purchased_relics.append(str(details.get("relic_name", "unknown")))
        elif event_type == "shop_ball_purchased":
            ball_id = str(details.get("ball_id", ""))
            if ball_id:
                acquired[ball_id] += 1
                progress = _event_progress(event)
                if progress is not None and ball_id in BUILD_TARGET_BALLS[build_profile]:
                    acquired_progress.append(progress)

    initial_source = _as_list(summary.get("initial_deck_state"))
    if not initial_source:
        initial_source = _as_list(
            _as_dict(run.get("initial_player")).get("deck")
        )
    final_source = _as_list(summary.get("final_deck_state"))
    if not final_source:
        final_source = _final_deck(run)
    initial_deck = _normalized_deck(initial_source)
    final_deck = _normalized_deck(final_source)
    initial_ids = {str(ball.get("definition_id", "")) for ball in initial_deck}
    final_ids = {str(ball.get("definition_id", "")) for ball in final_deck}
    target_ids = set(BUILD_TARGET_BALLS[build_profile])
    core_ids = set(BUILD_CORE_BALLS[build_profile])
    support_ids = target_ids - core_ids
    missing_core_parts = sorted(core_ids - final_ids)
    missing_support_parts = sorted(support_ids - final_ids)
    owned_core_parts = sorted(core_ids & final_ids)
    owned_support_parts = sorted(support_ids & final_ids)
    build_complete = not missing_core_parts
    initial_build_complete = core_ids <= initial_ids
    total_build_parts = len(core_ids | support_ids)
    completion_rate = _rate(
        len((core_ids | support_ids) & final_ids),
        total_build_parts,
    )
    completion_progress: int | None = None
    if initial_build_complete:
        completion_progress = int(context.get("initial_progress", 1))
    elif build_complete and acquired_progress:
        completion_progress = max(acquired_progress)

    battle_hp = []
    for ordinal, stage in enumerate(stages, start=1):
        start = _as_dict(stage.get("stage_start"))
        stage_result = _as_dict(stage.get("stage_result"))
        start_hp = start.get("player_hp")
        end_hp = stage_result.get("player_hp")
        battle_hp.append({
            "battle": ordinal,
            "stage_index": stage.get("stage_index"),
            "stage_type": stage.get("stage_type"),
            "start_hp": int(start_hp) if isinstance(start_hp, (int, float)) else None,
            "end_hp": int(end_hp) if isinstance(end_hp, (int, float)) else None,
            "hp_consumed": (
                int(start_hp) - int(end_hp)
                if isinstance(start_hp, (int, float))
                and isinstance(end_hp, (int, float))
                else None
            ),
        })

    progress = _as_dict(summary.get("run_progress"))
    player = _as_dict(summary.get("player"))
    final_boss_defeated = bool(
        progress.get("final_boss_defeated", event_counts["final_boss_defeated"] > 0)
    )
    final_boss_reached = bool(
        progress.get(
            "final_boss_reached",
            final_boss_defeated
            or event_counts["boss_preparation_entered"] > 0
            or any(stage.get("stage_type") == "boss" for stage in stages),
        )
    )
    final_progress = player.get("progress", summary.get("progress"))
    if not isinstance(final_progress, (int, float)):
        final_progress = result.get("reached_stage_index", 0)
    final_hp = player.get("current_hp", summary.get("hp"))
    if not isinstance(final_hp, (int, float)):
        final_hp = result.get("player_hp", 0)
    run_result = str(result.get("result", "unknown"))
    normal_end = run_result in {"completed", "game_over"} and is_terminal_summary(summary)
    stage_dynamic_balance_off = all(
        not bool(
            _as_dict(_as_dict(stage.get("stage_context")).get("assist_mode")).get(
                "enabled", True
            )
        )
        and not bool(
            _as_dict(
                _as_dict(stage.get("stage_context")).get("dynamic_balance")
            ).get("enabled", True)
        )
        for stage in stages
    )

    total_shots = len(shots)
    target_shots = sum(ball_usage[ball_id] for ball_id in target_ids)
    dominant_count = max(ball_usage.values(), default=0)
    shot_share_by_definition = {
        ball_id: _rate(count, total_shots)
        for ball_id, count in sorted(ball_usage.items())
    }
    target_acquisition_count = sum(acquired[ball_id] for ball_id in target_ids)
    reward_selection_history = []
    for raw_history in _as_list(summary.get("reward_selection_history")):
        history = dict(_as_dict(raw_history))
        history.setdefault("area", history.get("progress"))
        reward_selection_history.append(history)
    return {
        "schema_version": 1,
        "run_id": str(run.get("run_id", Path(log_path).stem)),
        "source_log": str(log_path),
        "run_seed": int(randomness.get("run_seed", summary.get("run_seed", -1))),
        "player_level": str(
            controller.get("profile", context.get("controller_profile", ""))
        ),
        "build_profile": build_profile,
        "build_profile_settings_hash": str(
            controller.get(
                "build_profile_settings_hash",
                context.get("build_profile_settings_hash", ""),
            )
        ),
        "configuration_signature": configuration_signature(run),
        "normal_end": normal_end,
        "run_result": run_result,
        "final_progress": int(final_progress),
        "final_boss_reached": final_boss_reached,
        "final_boss_defeated": final_boss_defeated,
        "final_hp": int(final_hp),
        "total_shots": total_shots,
        "ball_usage_by_definition": dict(sorted(ball_usage.items())),
        "ball_shot_share_by_definition": shot_share_by_definition,
        "initial_deck": initial_deck,
        "initial_deck_by_definition": _deck_by_definition(initial_deck),
        "final_deck": final_deck,
        "final_deck_by_definition": _deck_by_definition(final_deck),
        "acquired_balls": dict(sorted(acquired.items())),
        "target_build_ball_acquisition_count": target_acquisition_count,
        "target_build_ball_acquired": {
            ball_id: acquired[ball_id]
            for ball_id in sorted(target_ids)
            if acquired[ball_id]
        },
        "ball_upgrade_counts": dict(sorted(upgrade_counts.items())),
        "reward_choices": dict(sorted(choices.items())),
        "money_reward_count": choices["extra_money"],
        "rest_visits": event_counts["rest_entered"]
        or sum(
            1
            for event in events
            if _as_dict(event).get("event_type") == "route_area_completed"
            and _event_details(event).get("area_type") == "rest"
        ),
        "heal_count": event_counts["rest_heal"],
        "rest_upgrade_count": sum(
            1
            for event in events
            if _as_dict(event).get("event_type") == "ball_upgraded"
            and _event_details(event).get("source_scene") == "rest"
        ),
        "shop_visits": event_counts["shop_entered"]
        or sum(
            1
            for event in events
            if _as_dict(event).get("event_type") == "route_area_completed"
            and _event_details(event).get("area_type") == "shop"
        ),
        "purchased_relics": purchased_relics,
        "player_pocket_count": event_counts["player_pocket_returned"],
        "defeat_reason": "" if final_boss_defeated else run_result,
        "battle_hp": battle_hp,
        "damage_total": damage_total,
        "average_damage": round(damage_total / total_shots, 4) if total_shots else 0.0,
        "collision_count": collision_count,
        "collisions_per_shot": round(collision_count / total_shots, 4) if total_shots else 0.0,
        "enemy_move_count": sum(enemy_move_values) if enemy_move_values else None,
        "enemy_move_distance": (
            round(sum(enemy_distance_values), 4) if enemy_distance_values else None
        ),
        "chain_count": chain_count,
        "build_mechanic_activation_counts": mechanic_counts,
        "build_complete": build_complete,
        "build_completion": {
            "build_complete": build_complete,
            "completion_rate": completion_rate,
            "core_completion_rate": _rate(len(owned_core_parts), len(core_ids)),
            "owned_core_parts": owned_core_parts,
            "missing_core_parts": missing_core_parts,
            "owned_support_parts": owned_support_parts,
            "missing_support_parts": missing_support_parts,
            "missing_key_parts": [*missing_core_parts, *missing_support_parts],
        },
        "build_completion_rate": completion_rate,
        "build_completion_progress": completion_progress,
        "build_reward_offer_count": targeted_offers,
        "build_reward_offer_not_taken_count": targeted_not_taken,
        "build_target_shot_share": _rate(target_shots, total_shots),
        "target_build_ball_shot_share": _rate(target_shots, total_shots),
        "dominant_ball_shot_share": _rate(dominant_count, total_shots),
        "build_reward_dependent": build_complete and not initial_build_complete,
        "reward_selection_history": reward_selection_history,
        "dynamic_balance_enabled_at_start": context.get(
            "dynamic_balance_enabled_at_start"
        ),
        "validation_variant": str(validation.get("variant_id", "")),
        "dynamic_balance_forced_off": bool(
            validation.get("dynamic_balance_forced_off", False)
        ),
        "stage_dynamic_balance_off": stage_dynamic_balance_off,
    }


def validate_comparison_record(
    record: dict[str, Any],
    expected_seed: int,
    expected_build: str,
    expected_hash: str,
    expected_configuration_signature: str | None,
) -> list[str]:
    failures = []
    checks = (
        (record.get("run_seed") == expected_seed, "run_seed"),
        (record.get("build_profile") == expected_build, "build_profile"),
        (
            record.get("build_profile_settings_hash") == expected_hash,
            "build_profile_settings_hash",
        ),
        (record.get("player_level") == "intermediate", "player_level"),
        (bool(record.get("normal_end")), "normal_end"),
        (record.get("validation_variant") == "fixed", "validation_variant"),
        (
            record.get("dynamic_balance_enabled_at_start") is False,
            "dynamic_balance_enabled_at_start",
        ),
        (bool(record.get("dynamic_balance_forced_off")), "dynamic_balance_forced_off"),
        (bool(record.get("stage_dynamic_balance_off")), "stage_dynamic_balance_off"),
    )
    failures.extend(name for passed, name in checks if not passed)
    if (
        expected_configuration_signature is not None
        and record.get("configuration_signature")
        != expected_configuration_signature
    ):
        failures.append("configuration_signature_changed")
    return failures


def aggregate_build_records(
    records: list[dict[str, Any]],
    builds: tuple[str, ...] = COMPARISON_BUILDS,
) -> dict[str, Any]:
    summaries: dict[str, Any] = {}
    for build in builds:
        selected = [record for record in records if record.get("build_profile") == build]
        count = len(selected)
        wins = sum(bool(record.get("final_boss_defeated")) for record in selected)
        reached = sum(bool(record.get("final_boss_reached")) for record in selected)
        hp_values = [int(record.get("final_hp", 0)) for record in selected]
        shots = [int(record.get("total_shots", 0)) for record in selected]
        loss_progress = Counter(
            str(record.get("final_progress", 0))
            for record in selected
            if not record.get("final_boss_defeated")
        )
        hp_by_battle: dict[int, list[int]] = defaultdict(list)
        for record in selected:
            for battle in _as_list(record.get("battle_hp")):
                item = _as_dict(battle)
                if isinstance(item.get("hp_consumed"), (int, float)):
                    hp_by_battle[int(item.get("battle", 0))].append(
                        int(item["hp_consumed"])
                    )
        mechanic_totals: Counter[str] = Counter()
        reward_choices: Counter[str] = Counter()
        ball_usage: Counter[str] = Counter()
        target_acquisitions: Counter[str] = Counter()
        target_acquisition_runs: Counter[str] = Counter()
        missing_key_parts: Counter[str] = Counter()
        deck_counts: Counter[str] = Counter()
        deck_upgrade_totals: Counter[str] = Counter()
        deck_presence: Counter[str] = Counter()
        for record in selected:
            mechanic_totals.update(_as_dict(record.get("build_mechanic_activation_counts")))
            reward_choices.update(_as_dict(record.get("reward_choices")))
            ball_usage.update(_as_dict(record.get("ball_usage_by_definition")))
            acquired_targets = _as_dict(
                record.get("target_build_ball_acquired")
            )
            target_acquisitions.update(acquired_targets)
            target_acquisition_runs.update(acquired_targets.keys())
            missing_key_parts.update(
                _as_list(
                    _as_dict(record.get("build_completion")).get(
                        "missing_key_parts"
                    )
                )
            )
            final_by_definition = _as_dict(
                record.get("final_deck_by_definition")
            )
            for definition_id, raw_item in final_by_definition.items():
                item = _as_dict(raw_item)
                deck_counts[definition_id] += int(item.get("count", 0))
                deck_upgrade_totals[definition_id] += int(
                    item.get("total_upgrade_level", 0)
                )
                deck_presence[definition_id] += 1
        total_shots = sum(shots)
        enemy_move_counts = [
            int(record["enemy_move_count"])
            for record in selected
            if isinstance(record.get("enemy_move_count"), (int, float))
        ]
        enemy_move_distances = [
            float(record["enemy_move_distance"])
            for record in selected
            if isinstance(record.get("enemy_move_distance"), (int, float))
        ]
        summaries[build] = {
            "sample_count": count,
            "win_rate": _rate(wins, count),
            "final_boss_reach_rate": _rate(reached, count),
            "defeat_rate_after_reaching_boss": _rate(reached - wins, reached),
            "boss_defeat_rate_after_reaching": _rate(wins, reached),
            "loss_progress_distribution": dict(sorted(loss_progress.items())),
            "final_hp_average": _mean(hp_values),
            "final_hp_median": _median(hp_values),
            "battle_hp_consumption_average": {
                str(index): _mean(values)
                for index, values in sorted(hp_by_battle.items())
            },
            "shots_average": _mean(shots),
            "shots_median": _median(shots),
            "damage_per_run_average": _mean([
                int(record.get("damage_total", 0)) for record in selected
            ]),
            "damage_per_shot": _rate(
                sum(int(record.get("damage_total", 0)) for record in selected),
                total_shots,
            ),
            "collisions_per_shot": _rate(
                sum(int(record.get("collision_count", 0)) for record in selected),
                total_shots,
            ),
            "chain_count_per_run": _rate(
                sum(int(record.get("chain_count", 0)) for record in selected), count
            ),
            "enemy_move_count_average_when_available": _mean(enemy_move_counts),
            "enemy_move_distance_average_when_available": _mean(
                enemy_move_distances
            ),
            "enemy_move_observed_run_count": len(enemy_move_counts),
            "build_completion_rate": _rate(
                sum(bool(record.get("build_complete")) for record in selected), count
            ),
            "build_completion_progress_average": _mean([
                int(record["build_completion_progress"])
                for record in selected
                if isinstance(record.get("build_completion_progress"), (int, float))
            ]),
            "mechanic_activation_totals": dict(sorted(mechanic_totals.items())),
            "mechanic_activations_per_run": {
                name: _rate(value, count)
                for name, value in sorted(mechanic_totals.items())
            },
            "mechanic_activations_per_shot": {
                name: _rate(value, total_shots)
                for name, value in sorted(mechanic_totals.items())
            },
            "target_ball_shot_share_average": _mean([
                float(record.get("build_target_shot_share", 0.0))
                for record in selected
            ]),
            "target_build_ball_shot_share": _mean([
                float(record.get("target_build_ball_shot_share", 0.0))
                for record in selected
            ]),
            "target_build_ball_acquisition_count": sum(
                target_acquisitions.values()
            ),
            "target_build_ball_acquisition_rate": _rate(
                sum(
                    bool(
                        _as_dict(
                            record.get("target_build_ball_acquired")
                        )
                    )
                    for record in selected
                ),
                count,
            ),
            "target_build_ball_acquisition_rate_per_run": _rate(
                sum(target_acquisitions.values()),
                count,
            ),
            "target_build_ball_acquisitions_by_definition": dict(
                sorted(target_acquisitions.items())
            ),
            "target_build_ball_acquisition_run_rate_by_definition": {
                definition_id: _rate(run_count, count)
                for definition_id, run_count in sorted(
                    target_acquisition_runs.items()
                )
            },
            "average_final_deck": {
                definition_id: {
                    "average_count": _rate(deck_counts[definition_id], count),
                    "average_upgrade_level_per_copy": _rate(
                        deck_upgrade_totals[definition_id],
                        deck_counts[definition_id],
                    ),
                    "run_presence_rate": _rate(
                        deck_presence[definition_id],
                        count,
                    ),
                }
                for definition_id in sorted(deck_counts)
            },
            "final_deck_by_seed": {
                str(record.get("run_seed")): {
                    "by_definition": record.get(
                        "final_deck_by_definition", {}
                    ),
                    "instances": record.get("final_deck", []),
                }
                for record in selected
            },
            "reward_selection_history": {
                str(record.get("run_seed")): record.get(
                    "reward_selection_history", []
                )
                for record in selected
            },
            "missing_key_parts_frequency": dict(
                sorted(missing_key_parts.items())
            ),
            "dominant_ball_shot_share_average": _mean([
                float(record.get("dominant_ball_shot_share", 0.0))
                for record in selected
            ]),
            "ball_usage_by_definition": dict(sorted(ball_usage.items())),
            "reward_choices": dict(sorted(reward_choices.items())),
            "build_reward_offer_count": sum(
                int(record.get("build_reward_offer_count", 0)) for record in selected
            ),
            "build_reward_offer_not_taken_count": sum(
                int(record.get("build_reward_offer_not_taken_count", 0))
                for record in selected
            ),
            "reward_dependency_rate": _rate(
                sum(bool(record.get("build_reward_dependent")) for record in selected),
                count,
            ),
        }
    return summaries


def paired_seed_metrics(
    records: list[dict[str, Any]],
    seeds: tuple[int, ...] = COMPARISON_SEEDS,
    builds: tuple[str, ...] = COMPARISON_BUILDS,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    indexed = {
        (int(record["run_seed"]), str(record["build_profile"])): record
        for record in records
    }
    rows = []
    changed = 0
    hp_differences = []
    shot_differences = []
    for seed in seeds:
        row: dict[str, Any] = {"run_seed": seed}
        seed_records = [indexed.get((seed, build)) for build in builds]
        complete = all(record is not None for record in seed_records)
        wins = []
        hp = []
        shots = []
        for build, record in zip(builds, seed_records):
            prefix = build
            row[f"{prefix}_run_result"] = record.get("run_result") if record else ""
            row[f"{prefix}_win"] = int(bool(record.get("final_boss_defeated"))) if record else ""
            row[f"{prefix}_boss_reached"] = int(bool(record.get("final_boss_reached"))) if record else ""
            row[f"{prefix}_final_progress"] = record.get("final_progress", "") if record else ""
            row[f"{prefix}_final_hp"] = record.get("final_hp", "") if record else ""
            row[f"{prefix}_total_shots"] = record.get("total_shots", "") if record else ""
            row[f"{prefix}_build_complete"] = int(bool(record.get("build_complete"))) if record else ""
            row[f"{prefix}_build_completion_rate"] = (
                record.get("build_completion_rate", "") if record else ""
            )
            row[f"{prefix}_target_acquisitions"] = (
                record.get("target_build_ball_acquisition_count", "")
                if record
                else ""
            )
            row[f"{prefix}_target_shot_share"] = (
                record.get("target_build_ball_shot_share", "")
                if record
                else ""
            )
            row[f"{prefix}_missing_key_parts"] = (
                json.dumps(
                    _as_dict(record.get("build_completion")).get(
                        "missing_key_parts", []
                    ),
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
                if record
                else ""
            )
            row[f"{prefix}_final_deck"] = (
                json.dumps(
                    record.get("final_deck_by_definition", {}),
                    ensure_ascii=False,
                    sort_keys=True,
                    separators=(",", ":"),
                )
                if record
                else ""
            )
            if record:
                wins.append(bool(record.get("final_boss_defeated")))
                hp.append(int(record.get("final_hp", 0)))
                shots.append(int(record.get("total_shots", 0)))
        win_changed = complete and len(set(wins)) > 1
        hp_difference = max(hp) - min(hp) if complete else ""
        shot_difference = max(shots) - min(shots) if complete else ""
        row["win_changed_across_builds"] = int(win_changed)
        row["final_hp_range"] = hp_difference
        row["total_shots_range"] = shot_difference
        if win_changed:
            changed += 1
        if complete:
            hp_differences.append(int(hp_difference))
            shot_differences.append(int(shot_difference))
        rows.append(row)
    return rows, {
        "complete_seed_count": len(hp_differences),
        "same_seed_outcome_changed_count": changed,
        "same_seed_final_hp_range_average": _mean(hp_differences),
        "same_seed_final_hp_range_median": _median(hp_differences),
        "same_seed_shot_range_average": _mean(shot_differences),
        "same_seed_shot_range_median": _median(shot_differences),
    }


def write_comparison_outputs(
    output_directory: Path,
    records: list[dict[str, Any]],
    exclusions: list[dict[str, Any]],
    seeds: tuple[int, ...] = COMPARISON_SEEDS,
    builds: tuple[str, ...] = COMPARISON_BUILDS,
) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / "all_runs.jsonl").touch(exist_ok=True)
    (output_directory / "errors_and_exclusions.jsonl").touch(exist_ok=True)
    build_summaries = aggregate_build_records(records, builds)
    paired_rows, paired_summary = paired_seed_metrics(records, seeds, builds)
    excluded_count = sum(is_exclusion_event(item) for item in exclusions)
    metadata = {
        "schema_version": 1,
        "generated_at": _utc_now(),
        "player_level": "intermediate",
        "dynamic_balance": "off",
        "validation_variant": "fixed",
        "comparison_method_reference": COMPARISON_REFERENCE_COHORT,
        "seeds": list(seeds),
        "builds": list(builds),
        "accepted_run_count": len(records),
        "excluded_run_count": excluded_count,
        "connection_retry_event_count": len(exclusions) - excluded_count,
        "builds_summary": build_summaries,
        "paired_seed_summary": paired_summary,
    }
    (output_directory / "build_summaries.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    csv_path = output_directory / "seed_build_comparison.csv"
    if paired_rows:
        with csv_path.open("w", encoding="utf-8-sig", newline="") as destination:
            writer = csv.DictWriter(destination, fieldnames=list(paired_rows[0]))
            writer.writeheader()
            writer.writerows(paired_rows)

    header = (
        "| Build | N | Win | Boss reach | Defeat after reach | Final HP avg/med "
        "| Shots avg/med | Build complete | Target shot share |\n"
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|\n"
    )
    lines = [
        "# Fixed seed build comparison",
        "",
        f"Generated: {_utc_now()}",
        "",
        "Conditions: intermediate player level, dynamic balance off, "
        f"seeds {list(seeds)} shared by builds {list(builds)}.",
        f"Integrity/filter method reference: `{COMPARISON_REFERENCE_COHORT}`.",
        "",
        header.rstrip(),
    ]
    for build in builds:
        item = build_summaries[build]
        lines.append(
            f"| {build} | {item['sample_count']} | {item['win_rate']:.1%} | "
            f"{item['final_boss_reach_rate']:.1%} | "
            f"{item['defeat_rate_after_reaching_boss']:.1%} | "
            f"{item['final_hp_average']} / {item['final_hp_median']} | "
            f"{item['shots_average']} / {item['shots_median']} | "
            f"{item['build_completion_rate']:.1%} | "
            f"{item['target_ball_shot_share_average']} |"
        )
    lines.extend(["", "## Build diagnostics", ""])
    for build in builds:
        item = build_summaries[build]
        lines.extend([
            f"### {build}",
            "",
            f"- Loss progress distribution: `{json.dumps(item['loss_progress_distribution'], ensure_ascii=False)}`",
            f"- HP consumption by battle (average): `{json.dumps(item['battle_hp_consumption_average'], ensure_ascii=False)}`",
            f"- Mechanic activations per run: `{json.dumps(item['mechanic_activations_per_run'], ensure_ascii=False)}`",
            f"- Dominant-ball shot share average: {item['dominant_ball_shot_share_average']}",
            f"- Reward dependency rate: {item['reward_dependency_rate']:.1%}",
            f"- Build offers / not taken: {item['build_reward_offer_count']} / {item['build_reward_offer_not_taken_count']}",
            f"- Target acquisitions total / runs with acquisition / average per run: {item['target_build_ball_acquisition_count']} / {item['target_build_ball_acquisition_rate']:.1%} / {item['target_build_ball_acquisition_rate_per_run']}",
            f"- Missing key parts frequency: `{json.dumps(item['missing_key_parts_frequency'], ensure_ascii=False)}`",
            f"- Average final deck: `{json.dumps(item['average_final_deck'], ensure_ascii=False)}`",
            f"- Damage per shot / collisions per shot / chains per run: {item['damage_per_shot']} / {item['collisions_per_shot']} / {item['chain_count_per_run']}",
            f"- Enemy movement observed runs: {item['enemy_move_observed_run_count']} (averages are null when the log schema cannot provide them)",
            "",
        ])
    lines.extend([
        "## Same-seed comparison",
        "",
        f"- Complete paired seeds: {paired_summary['complete_seed_count']} / {len(seeds)}",
        f"- Seeds whose win/loss changed by build: {paired_summary['same_seed_outcome_changed_count']}",
        f"- Final HP range, average / median: {paired_summary['same_seed_final_hp_range_average']} / {paired_summary['same_seed_final_hp_range_median']}",
        f"- Shot-count range, average / median: {paired_summary['same_seed_shot_range_average']} / {paired_summary['same_seed_shot_range_median']}",
        "",
        "## Interpretation guardrail",
        "",
        f"{len(seeds)} runs per build are not enough to declare a build definitively strong or weak. Read win rate together with same-seed changes, HP consumption by battle, loss progress, build completion rate, and mechanic activation/use rates in build_summaries.json.",
        "",
        f"Excluded attempts: {excluded_count}; connection retry events: "
        f"{len(exclusions) - excluded_count} (see errors_and_exclusions.jsonl).",
    ])
    (output_directory / "final_comparison_report.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        value = json.loads(line)
        if isinstance(value, dict):
            records.append(value)
    return records


def _append_jsonl(path: Path, value: dict[str, Any]) -> None:
    with path.open("a", encoding="utf-8") as destination:
        destination.write(json.dumps(value, ensure_ascii=False) + "\n")


def _write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def comparison_output_paths(output_directory: Path) -> dict[str, str]:
    return {
        "output_directory": str(output_directory),
        "all_runs_jsonl": str(output_directory / "all_runs.jsonl"),
        "build_summaries_json": str(output_directory / "build_summaries.json"),
        "seed_build_comparison_csv": str(
            output_directory / "seed_build_comparison.csv"
        ),
        "errors_and_exclusions_jsonl": str(
            output_directory / "errors_and_exclusions.jsonl"
        ),
        "final_comparison_report": str(
            output_directory / "final_comparison_report.md"
        ),
    }


def is_exclusion_event(value: dict[str, Any]) -> bool:
    """Legacy lines are exclusions; retry telemetry opts out explicitly."""
    return bool(value.get("excluded", True))


def state_diagnostic_snapshot(state: dict[str, Any]) -> dict[str, Any]:
    validation = _as_dict(state.get("balance_validation"))
    player = _as_dict(state.get("player"))
    build = _as_dict(_as_dict(state.get("mcp_control")).get("build_profile"))
    return {
        "scene": state.get("scene"),
        "battle_state": state.get("game_state"),
        "area_progress": player.get("progress"),
        "cleared_stage_count": validation.get("cleared_stage_count"),
        "observed_seed": validation.get("run_seed"),
        "observed_build": build.get("id"),
    }


def state_matches_comparison_run(
    state: dict[str, Any],
    seed: int,
    build: str,
) -> bool:
    snapshot = state_diagnostic_snapshot(state)
    return (
        snapshot["observed_seed"] == seed
        and snapshot["observed_build"] == build
        and state.get("scene") not in {"", "title", None}
    )


def _write_comparison_status(
    output_directory: Path,
    state: str,
    records: list[dict[str, Any]],
    exclusions: list[dict[str, Any]],
    builds: tuple[str, ...],
    current_build: str = "",
    current_seed: int | None = None,
    error: str = "",
    retry_count: int = 0,
    retries: int = 0,
    last_error: dict[str, Any] | None = None,
    diagnostic_state: dict[str, Any] | None = None,
) -> None:
    by_build = Counter(str(record.get("build_profile", "")) for record in records)
    exclusions_count = sum(is_exclusion_event(item) for item in exclusions)
    diagnostic = state_diagnostic_snapshot(diagnostic_state or {})
    last_error_value = last_error or {}
    _write_json_atomic(
        output_directory / "comparison_status.json",
        {
            "state": state,
            "running": state == "running",
            "completed": state == "completed",
            "failed": state == "failed",
            "current_build": current_build,
            "current_seed": current_seed,
            "retrying": state == "running" and retry_count > 0,
            "retry_count": retry_count,
            "retries": retries,
            "last_error": last_error_value,
            "exception_type": last_error_value.get(
                "exception_type", last_error_value.get("exception", "")
            ),
            "exception_message": last_error_value.get(
                "exception_message", last_error_value.get("message", "")
            ),
            "error_timestamp": last_error_value.get(
                "timestamp", last_error_value.get("recorded_at", "")
            ),
            "failed_build": current_build if state == "failed" else "",
            "failed_seed": current_seed if state == "failed" else None,
            **diagnostic,
            "completed_valid_runs": len(records),
            "completed_valid_runs_by_build": {
                build: by_build[build] for build in builds
            },
            "excluded_runs": exclusions_count,
            "error": error,
            "updated_at": _utc_now(),
            "output_paths": comparison_output_paths(output_directory),
        },
    )


async def _wait_for_run_log(
    directory: Path,
    seen_paths: set[Path],
    run_seed: int,
    build_profile: str,
    timeout: float,
) -> tuple[Path, dict[str, Any]] | None:
    deadline = asyncio.get_running_loop().time() + max(0.1, timeout)
    while asyncio.get_running_loop().time() < deadline:
        for path in sorted(directory.glob("run_*.json"), reverse=True):
            resolved = path.resolve()
            if resolved in seen_paths:
                continue
            try:
                run = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            context = _as_dict(_as_dict(run).get("run_context"))
            if (
                _as_dict(context.get("randomness")).get("run_seed") == run_seed
                and context.get("build_profile") == build_profile
            ):
                seen_paths.add(resolved)
                return resolved, _as_dict(run)
        await asyncio.sleep(0.25)
    return None


async def _select_build_before_run(
    session: ClientSession,
    build_profile: str,
    expected_hash: str,
) -> None:
    state = await get_state(session)
    if state.get("scene") not in {"title", "result"}:
        raise RuntimeError(
            "build_profile can only change before a run; "
            f"scene={state.get('scene')}"
        )
    result = await call(
        session, "set_build_profile", {"build_profile": build_profile}
    )
    selected = _as_dict(_as_dict(result.get("mcp_control")).get("build_profile"))
    if not result.get("ok", False) and result.get("reason") == "mcp_tool_error":
        selected = _as_dict(
            _as_dict(_as_dict(result.get("state_after_error")).get("mcp_control")).get(
                "build_profile"
            )
        )
    if selected.get("id") != build_profile or selected.get("settings_hash") != expected_hash:
        raise RuntimeError(
            "MCP server did not confirm requested build profile/hash: "
            f"requested={build_profile}/{expected_hash}, active={selected}"
        )


def _load_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def validate_resume_inputs(
    existing_request: dict[str, Any],
    records: list[dict[str, Any]],
    seeds: tuple[int, ...],
    builds: tuple[str, ...],
    hashes: dict[str, str],
) -> str | None:
    """Validate saved conditions and return their balance signature."""
    expected_request = {
        "seeds": list(seeds),
        "build_profiles": list(builds),
        "player_level_requested": "intermediate",
        "validation_variant": "fixed",
        "valid_runs_per_build": len(seeds),
        "dynamic_balance": False,
    }
    mismatches = {
        key: {"expected": expected, "saved": existing_request.get(key)}
        for key, expected in expected_request.items()
        if existing_request.get(key) != expected
    }
    if mismatches:
        raise RuntimeError(
            "Resume request does not match saved comparison conditions: "
            + json.dumps(mismatches, ensure_ascii=False, sort_keys=True)
        )
    saved_hashes = existing_request.get("build_profile_settings_hashes")
    if isinstance(saved_hashes, dict) and saved_hashes != hashes:
        raise RuntimeError(
            "Resume build profile settings hashes do not match: "
            + json.dumps(
                {"expected": hashes, "saved": saved_hashes},
                ensure_ascii=False,
                sort_keys=True,
            )
        )
    expected_signature = (
        str(records[0].get("configuration_signature")) if records else None
    )
    for record in records:
        seed = int(record.get("run_seed", -1))
        build = str(record.get("build_profile", ""))
        if build not in hashes:
            raise RuntimeError(f"Resume record has unexpected build profile: {build}")
        failures = validate_comparison_record(
            record,
            seed,
            build,
            hashes[build],
            expected_signature,
        )
        if failures:
            raise RuntimeError(
                "Resume record failed integrity validation: "
                f"seed={seed}, build={build}, failures={failures}"
            )
    return expected_signature


async def ensure_comparison_run_started(
    session: ClientSession,
    seed: int,
    build: str,
    expected_hash: str,
    validation_variant: str,
    state_observer: Callable[[dict[str, Any]], None] | None = None,
    operation_state: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Resume an already-started pair or start it after authoritative reads."""
    operation_state = operation_state if operation_state is not None else {}
    state = await get_state(session)
    if state_observer is not None:
        state_observer(state)
    scene = str(state.get("scene", ""))
    if state_matches_comparison_run(state, seed, build) and (
        scene != "result" or operation_state.get("start_requested", False)
    ):
        return {"resumed": True, "state": state}
    if scene not in {"title", "result"}:
        raise RuntimeError(
            "Cannot start requested comparison pair while a different run is active: "
            f"requested={build}/{seed}, observed={state_diagnostic_snapshot(state)}"
        )

    await _select_build_before_run(session, build, expected_hash)
    operation_state["start_requested"] = True
    result = await call(
        session,
        "start_new_run",
        {"run_seed": seed, "validation_variant": validation_variant or "fixed"},
    )
    if not result.get("ok", False):
        state_after = _as_dict(result.get("state_after_error"))
        if state_matches_comparison_run(state_after, seed, build):
            if state_observer is not None:
                state_observer(state_after)
            return {"resumed": True, "state": state_after}
        raise RuntimeError(f"start_new_run failed: {result}")

    confirmed = await get_state(session)
    if state_observer is not None:
        state_observer(confirmed)
    if not state_matches_comparison_run(confirmed, seed, build):
        raise RuntimeError(
            "MCP server did not confirm requested comparison run: "
            f"requested={build}/{seed}, observed={state_diagnostic_snapshot(confirmed)}"
        )
    return {"resumed": False, "state": confirmed}


async def collect_comparison_suite(args: argparse.Namespace) -> None:
    if args.profile != "intermediate":
        raise RuntimeError("--comparison-suite requires --profile intermediate")
    if args.resume_current:
        raise RuntimeError("--comparison-suite cannot use --resume-current")
    if args.validation_variant not in {"", "fixed"}:
        raise RuntimeError(
            "--comparison-suite requires --validation-variant fixed"
        )
    seeds = tuple(args.comparison_seed or COMPARISON_SEEDS)
    builds = tuple(args.comparison_build or COMPARISON_BUILDS)
    if len(set(seeds)) != len(seeds):
        raise RuntimeError("--comparison-seed values must be unique")
    if len(set(builds)) != len(builds):
        raise RuntimeError("--comparison-build values must be unique")
    if args.valid_runs_per_build != len(seeds):
        raise RuntimeError(
            "--valid-runs-per-build must equal the number of unique paired seeds"
        )

    output_directory = args.output_directory
    if output_directory is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_directory = args.balance_log_directory / f"comparison_{timestamp}"
    output_directory = output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    runs_path = output_directory / "all_runs.jsonl"
    errors_path = output_directory / "errors_and_exclusions.jsonl"
    request_path = output_directory / "comparison_request.json"
    records = _load_jsonl(runs_path)
    exclusions = _load_jsonl(errors_path)
    hashes = {
        build: profile_settings_hash(args.loaded_build_profiles[build])
        for build in builds
    }
    existing_request = _load_json_object(request_path)
    expected_signature: str | None = None
    if existing_request or records:
        if not existing_request:
            raise RuntimeError(
                "Cannot resume comparison without comparison_request.json"
            )
        expected_signature = validate_resume_inputs(
            existing_request, records, seeds, builds, hashes
        )

    accepted_pairs = {
        (int(record.get("run_seed", -1)), str(record.get("build_profile", "")))
        for record in records
    }
    expected_pairs = {(seed, build) for build in builds for seed in seeds}
    unexpected = accepted_pairs - expected_pairs
    if unexpected or len(accepted_pairs) != len(records):
        raise RuntimeError(
            "Existing all_runs.jsonl has duplicate or unexpected seed/build pairs"
        )

    seen_logs = {
        path.resolve() for path in args.balance_log_directory.glob("run_*.json")
    }
    request_metadata = {
        "seeds": list(seeds),
        "build_profiles": list(builds),
        "build_profile_settings_hashes": hashes,
        "player_level_requested": "intermediate",
        "player_level_confirmed": existing_request.get(
            "player_level_confirmed", ""
        ),
        "validation_variant": "fixed",
        "valid_runs_per_build": args.valid_runs_per_build,
        "dynamic_balance": False,
        "python_executable": str(Path(sys.executable).resolve()),
        "cwd": str(Path.cwd().resolve()),
        "mcp_endpoint": args.url,
        "build_profiles_path": str(args.build_profiles.resolve()),
        "resumed": bool(existing_request or records),
        "resumed_valid_runs": len(records),
    }
    _write_json_atomic(request_path, request_metadata)

    runtime: dict[str, Any] = {
        "current_build": "",
        "current_seed": None,
        "retry_count": 0,
        "retries": sum(
            not is_exclusion_event(item) for item in exclusions
        ),
        "last_error": {},
        "last_state": {},
    }

    def observe_state(state: dict[str, Any]) -> None:
        runtime["last_state"] = state

    def write_status(state: str = "running", error: str = "") -> None:
        _write_comparison_status(
            output_directory,
            state,
            records,
            exclusions,
            builds,
            current_build=str(runtime["current_build"]),
            current_seed=runtime["current_seed"],
            error=error,
            retry_count=int(runtime["retry_count"]),
            retries=int(runtime["retries"]),
            last_error=_as_dict(runtime["last_error"]),
            diagnostic_state=_as_dict(runtime["last_state"]),
        )

    def record_retry(details: dict[str, Any]) -> None:
        runtime["retry_count"] = int(details["retry_count"])
        runtime["retries"] += 1
        event = {
            "recorded_at": details["timestamp"],
            "excluded": False,
            "event": "connection_retry",
            "reason": "connection_error",
            "build": runtime["current_build"],
            "seed": runtime["current_seed"],
            "action": "reconnect_and_resume",
            **details,
            **state_diagnostic_snapshot(_as_dict(runtime["last_state"])),
        }
        runtime["last_error"] = event
        exclusions.append(event)
        _append_jsonl(errors_path, event)
        write_status()

    async def resilient_operation(
        name: str,
        operation: Callable[[ClientSession], Awaitable[Any]],
        *,
        max_attempts: int = MCP_CONNECTION_MAX_ATTEMPTS,
    ) -> Any:
        result = await run_mcp_operation_with_retries(
            args.url,
            name,
            operation,
            max_attempts=max_attempts,
            on_retry=record_retry,
        )
        runtime["retry_count"] = 0
        return result

    write_status()

    async def configure_level(session: ClientSession) -> tuple[dict[str, Any], dict[str, Any]]:
        result, state = await set_and_confirm_player_level(
            session, "intermediate"
        )
        observe_state(state)
        return result, state

    try:
        level_result, confirmed_state = await resilient_operation(
            "configure_player_level", configure_level
        )
    except McpRetryExhausted as error:
        raise RuntimeError(
            "MCP server remained unavailable while configuring comparison"
        ) from error
    request_metadata["player_level_confirmed"] = response_player_level(
        confirmed_state
    )
    request_metadata["set_player_level_ok"] = bool(level_result.get("ok", False))
    _write_json_atomic(request_path, request_metadata)
    print(
        json.dumps(
            {"event": "comparison_environment", **request_metadata},
            ensure_ascii=False,
        ),
        flush=True,
    )

    for build in builds:
        for seed in seeds:
            if (seed, build) in accepted_pairs:
                continue
            attempt = 0
            while (seed, build) not in accepted_pairs:
                attempt += 1
                runtime["current_build"] = build
                runtime["current_seed"] = seed
                runtime["retry_count"] = 0
                write_status()
                operation_state: dict[str, Any] = {}

                async def start_pair(session: ClientSession) -> dict[str, Any]:
                    return await ensure_comparison_run_started(
                        session,
                        seed,
                        build,
                        hashes[build],
                        args.validation_variant or "fixed",
                        observe_state,
                        operation_state,
                    )

                async def play_pair(session: ClientSession) -> dict[str, Any]:
                    return await play_current_run(
                        session,
                        "intermediate",
                        build,
                        args.loaded_build_profiles[build],
                        args.loaded_build_profiles,
                        hashes[build],
                        args.maximum_steps,
                        len(records) + 1,
                        observe_state,
                    )

                try:
                    await resilient_operation("start_or_resume_run", start_pair)
                    summary = await resilient_operation("play_current_run", play_pair)
                    while summary.get("reason") == "maximum_steps":
                        summary = await resilient_operation(
                            "continue_current_run", play_pair
                        )
                except McpRetryExhausted as error:
                    details = exception_details(error.last_error)
                    exclusion = {
                        "recorded_at": _utc_now(),
                        "excluded": True,
                        "build": build,
                        "seed": seed,
                        "run_seed": seed,
                        "build_profile": build,
                        "attempt": attempt,
                        "reason": "connection_failed",
                        "exception": details["exception_type"],
                        "message": details["exception_message"],
                        "retry_count": error.attempts,
                        "action": "discard_then_retry_same_seed",
                        **state_diagnostic_snapshot(
                            _as_dict(runtime["last_state"])
                        ),
                    }
                    runtime["last_error"] = exclusion
                    exclusions.append(exclusion)
                    _append_jsonl(errors_path, exclusion)
                    write_status()

                    async def recover_state(session: ClientSession) -> dict[str, Any]:
                        state = await get_state(session)
                        observe_state(state)
                        return state

                    try:
                        recovered = await resilient_operation(
                            "recover_after_retry_exhaustion", recover_state
                        )
                    except McpRetryExhausted as recovery_error:
                        raise RuntimeError(
                            "MCP server remained unavailable after excluding one run"
                        ) from recovery_error

                    if state_matches_comparison_run(recovered, seed, build):
                        discarded = await resilient_operation(
                            "finish_excluded_run", play_pair
                        )
                        while discarded.get("reason") == "maximum_steps":
                            discarded = await resilient_operation(
                                "finish_excluded_run", play_pair
                            )
                        if not is_terminal_summary(discarded):
                            raise RuntimeError(
                                "Excluded run could not be advanced safely to result: "
                                f"seed={seed}, build={build}, reason={discarded.get('reason')}"
                            )
                        discarded_log = await _wait_for_run_log(
                            args.balance_log_directory,
                            seen_logs,
                            seed,
                            build,
                            args.run_log_timeout,
                        )
                        if discarded_log is not None:
                            seen_logs.add(discarded_log[0].resolve())
                    elif recovered.get("scene") not in {"title", "result"}:
                        raise RuntimeError(
                            "Recovered into an unrelated run; data integrity cannot be guaranteed: "
                            + json.dumps(
                                state_diagnostic_snapshot(recovered),
                                ensure_ascii=False,
                            )
                        )
                    runtime["retry_count"] = 0
                    continue

                if not is_terminal_summary(summary):
                    exclusion = {
                        "recorded_at": _utc_now(),
                        "excluded": True,
                        "run_seed": seed,
                        "build_profile": build,
                        "attempt": attempt,
                        "reason": "abnormal_run_end",
                        "details": summary,
                    }
                    exclusions.append(exclusion)
                    _append_jsonl(errors_path, exclusion)
                    write_status()
                    raise RuntimeError(
                        "Abnormal run did not reach result scene; cannot safely "
                        "replace it without changing the in-progress game: "
                        f"seed={seed}, build={build}, reason={summary.get('reason')}"
                    )

                located = await _wait_for_run_log(
                    args.balance_log_directory,
                    seen_logs,
                    seed,
                    build,
                    args.run_log_timeout,
                )
                if located is None:
                    exclusion = {
                        "recorded_at": _utc_now(),
                        "excluded": True,
                        "run_seed": seed,
                        "build_profile": build,
                        "attempt": attempt,
                        "reason": "completed_run_log_not_found",
                        "details": summary,
                    }
                    exclusions.append(exclusion)
                    _append_jsonl(errors_path, exclusion)
                    write_status()
                    continue
                log_path, run = located
                record = extract_run_record(run, summary, log_path)
                if expected_signature is None:
                    expected_signature = record["configuration_signature"]
                failures = validate_comparison_record(
                    record,
                    seed,
                    build,
                    hashes[build],
                    expected_signature,
                )
                if "configuration_signature_changed" in failures:
                    raise RuntimeError(
                        "Game/configuration changed during comparison collection; "
                        f"seed={seed}, build={build}"
                    )
                if failures:
                    exclusion = {
                        "recorded_at": _utc_now(),
                        "excluded": True,
                        "run_seed": seed,
                        "build_profile": build,
                        "attempt": attempt,
                        "reason": "integrity_validation_failed",
                        "failures": failures,
                        "source_log": str(log_path),
                    }
                    exclusions.append(exclusion)
                    _append_jsonl(errors_path, exclusion)
                    write_status()
                    continue

                record["attempt"] = attempt
                record["accepted_at"] = _utc_now()
                records.append(record)
                accepted_pairs.add((seed, build))
                _append_jsonl(runs_path, record)
                write_comparison_outputs(
                    output_directory, records, exclusions, seeds, builds
                )
                write_status()
                print(
                    json.dumps(
                        {
                            "event": "comparison_run_accepted",
                            "accepted_runs": len(records),
                            "run_seed": seed,
                            "build_profile": build,
                            "attempt": attempt,
                        },
                        ensure_ascii=False,
                    ),
                    flush=True,
                )

    runtime["current_build"] = ""
    runtime["current_seed"] = None
    runtime["retry_count"] = 0
    write_comparison_outputs(output_directory, records, exclusions, seeds, builds)
    _write_comparison_status(
        output_directory,
        "completed",
        records,
        exclusions,
        builds,
        retries=int(runtime["retries"]),
        last_error=_as_dict(runtime["last_error"]),
        diagnostic_state=_as_dict(runtime["last_state"]),
    )
    print(json.dumps({
        "event": "comparison_complete",
        "accepted_runs": len(records),
        "excluded_attempts": sum(is_exclusion_event(item) for item in exclusions),
        "connection_retries": runtime["retries"],
        "output_directory": str(output_directory),
    }, ensure_ascii=False), flush=True)


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
                if not is_terminal_summary(summary):
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
    if args.run_log_timeout <= 0:
        raise SystemExit("--run-log-timeout must be positive")
    if any(seed < 0 or seed > 0xFFFFFFFF for seed in args.run_seed):
        raise SystemExit("--run-seed must be between 0 and 4294967295")
    if any(seed < 0 or seed > 0xFFFFFFFF for seed in args.comparison_seed):
        raise SystemExit("--comparison-seed must be between 0 and 4294967295")
    if args.valid_runs_per_build <= 0:
        raise SystemExit("--valid-runs-per-build must be positive")
    profiles, _ = load_build_profiles(args.build_profiles.resolve())
    if args.build_profile not in profiles:
        raise SystemExit(
            "--build-profile must be standard, heavy, pierce, bounce, "
            "or anchor"
        )
    args.loaded_build_profiles = profiles
    if args.comparison_suite:
        try:
            asyncio.run(collect_comparison_suite(args))
        except Exception as error:
            if args.output_directory is not None:
                output_directory = args.output_directory.resolve()
                output_directory.mkdir(parents=True, exist_ok=True)
                records = _load_jsonl(output_directory / "all_runs.jsonl")
                exclusions = _load_jsonl(
                    output_directory / "errors_and_exclusions.jsonl"
                )
                previous = _load_json_object(
                    output_directory / "comparison_status.json"
                )
                _write_comparison_status(
                    output_directory,
                    "failed",
                    records,
                    exclusions,
                    tuple(args.comparison_build or COMPARISON_BUILDS),
                    current_build=str(previous.get("current_build", "")),
                    current_seed=previous.get("current_seed"),
                    error=repr(error),
                    retry_count=int(previous.get("retry_count", 0)),
                    retries=int(previous.get("retries", 0)),
                    last_error=_as_dict(previous.get("last_error")),
                    diagnostic_state={
                        "scene": previous.get("scene"),
                        "game_state": previous.get("battle_state"),
                        "player": {"progress": previous.get("area_progress")},
                        "balance_validation": {
                            "cleared_stage_count": previous.get(
                                "cleared_stage_count"
                            ),
                            "run_seed": previous.get("observed_seed"),
                        },
                        "mcp_control": {
                            "build_profile": {
                                "id": previous.get("observed_build")
                            }
                        },
                    },
                )
            raise
    else:
        asyncio.run(collect(args))


if __name__ == "__main__":
    main()
