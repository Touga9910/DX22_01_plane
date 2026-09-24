"""到達可能なショットだけを、要求条件から順に探す。"""

from __future__ import annotations

from typing import Any

from build_shot_evaluator import evaluate_build_shots


def fallback_shot_candidates(
    state: dict[str, Any],
    profile: dict[str, Any],
    target_id: str,
    shot_goal: str = "auto",
    shot_type: str = "auto",
    wall_index: int = -1,
    pocket_index: int = -1,
    fixed_power: float | None = None,
) -> list[dict[str, Any]]:
    """選択中の球で、元の指定から全生存敵へ探索を広げる。"""
    candidates = evaluate_build_shots(state, profile, fixed_power=fixed_power)
    if not candidates:
        return []

    def exact(choice: dict[str, Any]) -> bool:
        return (
            choice["target_id"] == target_id
            and (shot_goal == "auto" or choice["shot_goal"] == shot_goal)
            and (shot_type == "auto" or choice["shot_type"] == shot_type)
            and (wall_index < 0 or choice["wall_index"] == wall_index)
            and (pocket_index < 0 or choice["pocket_index"] == pocket_index)
        )

    def priority(choice: dict[str, Any]) -> int:
        if exact(choice):
            return 0
        same_target = choice["target_id"] == target_id
        same_goal = shot_goal == "auto" or choice["shot_goal"] == shot_goal
        direct_or_requested = (
            choice["shot_type"] == shot_type
            if shot_type != "auto"
            else choice["shot_type"] == "direct"
        )
        if not same_target and same_goal and direct_or_requested:
            return 1  # 同じ球で別の敵
        if same_target and direct_or_requested:
            return 2  # 同じ敵でdamage/pocketを変更
        if same_target:
            return 3  # 許可されていればbankへ変更
        return 4  # 残りの到達可能な敵と経路

    return sorted(candidates, key=lambda choice: (
        priority(choice), -choice["score"], choice["target_id"],
    ))


def recommended_action(
    state: dict[str, Any],
    shot_tactics: dict[str, Any],
    build_shot_choices: dict[str, Any],
) -> dict[str, Any] | None:
    """選択済みの球を優先し、撃てないときだけ別球を選ぶ。"""
    actions = state.get("available_actions", [])
    if "fire_shot" not in actions or state.get("game_state") != "aiming_direction":
        return None
    selected = next((ball for ball in state.get("offered_balls", [])
                     if ball.get("selected")), None)
    target_id = shot_tactics.get("recommended_target_id")
    if selected and target_id:
        return {
            "action": "fire_shot", "ball_index": selected.get("index"),
            "ball_instance_id": selected.get("instance_id"),
            "target_id": target_id,
            "shot_goal": shot_tactics.get("recommended_goal", "auto"),
            "shot_type": shot_tactics.get("recommended_shot_type", "auto"),
            "pocket_index": shot_tactics.get("recommended_pocket_index", -1),
            "wall_index": shot_tactics.get("recommended_wall_index", -1),
            "state_sequence": state.get("sequence"),
        }
    if "select_ball" in actions:
        selected_index = selected.get("index") if selected else None
        for choice in build_shot_choices.get("choices", []):
            if choice.get("offer_index") == selected_index:
                continue
            return {
                "action": "select_ball",
                "ball_index": choice["offer_index"],
                "ball_instance_id": choice.get("instance_id"),
                "target_id_after_selection": choice["target_id"],
                "reason": "selected_ball_has_no_reachable_shot",
                "state_sequence": state.get("sequence"),
            }
    return {"action": "none", "reason": "no_reachable_shot", "state_sequence": state.get("sequence")}
