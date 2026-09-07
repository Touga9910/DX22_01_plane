"""MCPゲーム操作向けの、説明可能で状態依存のビルド判断。"""

from __future__ import annotations

from typing import Any


BALL_METADATA: dict[str, dict[str, Any]] = {
    "player_standard": {
        "standalone_value": 18.0,
        "tags": ("generic", "direct"),
    },
    "player_heavy": {
        "standalone_value": 11.0,
        "tags": ("heavy", "collision_chain"),
    },
    "player_pierce": {
        "standalone_value": 10.0,
        "tags": ("pierce", "line"),
    },
    "player_bounce": {
        "standalone_value": 8.0,
        "tags": ("bounce", "bank"),
    },
    "player_anchor": {
        "standalone_value": 13.0,
        "tags": ("anchor", "defense", "control"),
    },
}

CORE_RELICS: dict[str, tuple[str, ...]] = {
    "standard": ("Power Core",),
    "heavy": ("Impact Accelerator",),
    "pierce": ("Power Core",),
    "bounce": ("Bank Shot",),
    "anchor": ("Guard Core", "Emergency Repair Kit"),
}

RELIC_NAME_ALIASES = {
    "Power Core": "Power Core",
    "攻撃コア": "Power Core",
    "Guard Core": "Guard Core",
    "防御コア": "Guard Core",
    "Impact Accelerator": "Impact Accelerator",
    "衝撃加速装置": "Impact Accelerator",
    "Bank Shot": "Bank Shot",
    "バンクショット": "Bank Shot",
    "Emergency Repair Kit": "Emergency Repair Kit",
    "緊急修理キット": "Emergency Repair Kit",
}

RELIC_STANDALONE_VALUE = {
    "Power Core": 22.0,
    "Guard Core": 20.0,
    "Impact Accelerator": 21.0,
    "Bank Shot": 14.0,
    "Emergency Repair Kit": 18.0,
}


def _clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def _as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def _as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def _canonical_relic_name(name: Any) -> str:
    value = str(name or "")
    return RELIC_NAME_ALIASES.get(value, value)


def _ball_counts(state: dict[str, Any]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for ball in _as_list(state.get("deck_balls")):
        if not isinstance(ball, dict):
            continue
        definition_id = str(ball.get("definition_id", ""))
        if definition_id:
            counts[definition_id] = counts.get(definition_id, 0) + 1
    return counts


def _owned_relic_names(state: dict[str, Any]) -> set[str]:
    return {
        _canonical_relic_name(relic.get("name"))
        for relic in _as_list(state.get("relics"))
        if isinstance(relic, dict) and bool(relic.get("owned", False))
    }


def _priority_rank(definition_id: str, priorities: list[str]) -> int:
    try:
        return priorities.index(definition_id)
    except ValueError:
        return len(priorities)


def _priority_fit(definition_id: str, priorities: list[str]) -> float:
    if not priorities:
        return 0.0
    rank = _priority_rank(definition_id, priorities)
    if rank >= len(priorities):
        return 0.0
    return 1.0 - rank / max(1, len(priorities) - 1)


def _player_status(state: dict[str, Any]) -> tuple[float, int, int, int]:
    player = _as_dict(state.get("player"))
    current_hp = max(0, int(player.get("current_hp", 0)))
    maximum_hp = max(1, int(player.get("max_hp", max(1, current_hp))))
    money = max(0, int(player.get("money", 0)))
    return current_hp / maximum_hp, current_hp, maximum_hp, money


def evaluate_build_hypotheses(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> list[dict[str, Any]]:
    """1つへ恒久的に決め打ちせず、すべてのビルドを評価する。"""
    counts = _ball_counts(state)
    owned_relics = _owned_relic_names(state)
    hypotheses: list[dict[str, Any]] = []

    for profile_id, profile in profiles.items():
        priorities = [
            str(value)
            for value in _as_list(profile.get("new_ball_priority"))
        ]
        core_ball = priorities[0] if priorities else ""
        core_ball_count = counts.get(core_ball, 0)
        ball_completion = _clamp(core_ball_count / 2.0, 0.0, 1.0)
        core_relics = CORE_RELICS.get(profile_id, ())
        relic_matches = [
            relic for relic in core_relics if relic in owned_relics
        ]
        relic_completion = (
            len(relic_matches) / len(core_relics)
            if core_relics
            else 1.0
        )

        deck_count = sum(counts.values())
        affinity = (
            sum(
                count * _priority_fit(definition_id, priorities)
                for definition_id, count in counts.items()
            ) / deck_count
            if deck_count > 0
            else 0.0
        )
        completion = (
            ball_completion * 0.75 + relic_completion * 0.25
        )
        preferred_bonus = 0.08 if profile_id == preferred_profile_id else 0.0
        commitment = _clamp(
            0.10 + completion * 0.68 + affinity * 0.14 + preferred_bonus,
            0.05,
            1.0,
        )
        fit_score = (
            completion * 55.0
            + affinity * 30.0
            + preferred_bonus * 100.0
        )

        missing: list[str] = []
        if core_ball_count == 0 and core_ball:
            missing.append(core_ball)
        missing.extend(
            relic for relic in core_relics if relic not in owned_relics
        )
        hypotheses.append(
            {
                "id": profile_id,
                "label": str(profile.get("label", profile_id)),
                "preferred": profile_id == preferred_profile_id,
                "core_ball": core_ball,
                "core_ball_count": core_ball_count,
                "owned_core_relics": relic_matches,
                "missing_key_parts": missing,
                "completion": round(completion, 4),
                "affinity": round(affinity, 4),
                "commitment": round(commitment, 4),
                "fit_score": round(fit_score, 3),
            }
        )

    hypotheses.sort(
        key=lambda value: (
            float(value["fit_score"]),
            float(value["commitment"]),
            bool(value["preferred"]),
        ),
        reverse=True,
    )
    return hypotheses


def _immediate_ball_value(
    ball: dict[str, Any],
    living_enemy_count: int,
    hp_ratio: float,
) -> tuple[float, float]:
    status = _as_dict(ball.get("status"))
    attack = float(status.get("attack", 0))
    defense = float(status.get("defense", 0))
    mass = float(status.get("mass", 0))
    restitution = float(status.get("restitution", 0))
    immediate = (
        attack * 12.0
        + defense * 7.0
        + max(0.0, mass - 1.0) * 2.0
        + max(0.0, restitution) * 5.0
    )
    if bool(status.get("pierce", False)) and living_enemy_count > 1:
        immediate += 9.0
    if bool(status.get("anchor", False)):
        immediate += 7.0
    survival = 0.0
    if hp_ratio < 0.5:
        urgency = (0.5 - hp_ratio) / 0.5
        survival = defense * 12.0 * urgency
        if bool(status.get("anchor", False)):
            survival += 12.0 * urgency
    return immediate, survival


def evaluate_ball_choices(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
    source: str = "offered_balls",
) -> dict[str, Any]:
    hypotheses = evaluate_build_hypotheses(
        state,
        profiles,
        preferred_profile_id,
    )
    top_commitment = (
        float(hypotheses[0]["commitment"]) if hypotheses else 0.0
    )
    top_build_id = str(hypotheses[0]["id"]) if hypotheses else ""
    hp_ratio, _, _, _ = _player_status(state)
    living_enemy_count = sum(
        not bool(enemy.get("defeated", False))
        and not bool(enemy.get("pocketed", False))
        for enemy in _as_list(state.get("enemies"))
        if isinstance(enemy, dict)
    )
    counts = _ball_counts(state)
    choices: list[dict[str, Any]] = []

    for ball in _as_list(state.get(source)):
        if not isinstance(ball, dict):
            continue
        definition_id = str(ball.get("definition_id", ""))
        immediate, survival = _immediate_ball_value(
            ball,
            living_enemy_count,
            hp_ratio,
        )
        metadata = BALL_METADATA.get(definition_id, {})
        standalone = float(metadata.get("standalone_value", 6.0))
        flexibility_count = 0
        best_build: dict[str, Any] | None = None
        best_build_score = -1.0e9
        best_build_breakdown: dict[str, float] = {}

        for hypothesis in hypotheses:
            profile_id = str(hypothesis["id"])
            profile = profiles.get(profile_id, {})
            priorities = [
                str(value)
                for value in _as_list(profile.get("new_ball_priority"))
            ]
            fit = _priority_fit(definition_id, priorities)
            if _priority_rank(definition_id, priorities) <= 1:
                flexibility_count += 1
            commitment = float(hypothesis["commitment"])
            completion = float(hypothesis["completion"])
            synergy = fit * (12.0 + commitment * 25.0)
            key_part_multiplier = (
                1.0
                if profile_id == top_build_id
                else max(0.15, 1.0 - top_commitment)
            )
            key_part = (
                (1.0 - completion) * 32.0 * key_part_multiplier
                if definition_id == hypothesis.get("core_ball")
                else 0.0
            )
            preferred = 3.0 if bool(hypothesis["preferred"]) else 0.0
            build_score = synergy + key_part + preferred
            if build_score > best_build_score:
                best_build = hypothesis
                best_build_score = build_score
                best_build_breakdown = {
                    "synergy": synergy,
                    "key_part": key_part,
                    "preferred_build": preferred,
                }

        flexibility = flexibility_count * 2.5
        generic_value = standalone * (1.0 - top_commitment) * 1.6
        same_count = counts.get(definition_id, 0)
        redundancy = max(0, same_count - 2) * 9.0
        total = (
            immediate
            + survival
            + generic_value
            + flexibility
            + best_build_score
            - redundancy
        )
        choices.append(
            {
                "index": int(ball.get("index", -1)),
                "instance_id": ball.get("instance_id", 0),
                "definition_id": definition_id,
                "score": round(total, 3),
                "chosen_build_id": (
                    str(best_build["id"]) if best_build else None
                ),
                "breakdown": {
                    "immediate_power": round(immediate, 3),
                    "survival": round(survival, 3),
                    "generic_early_value": round(generic_value, 3),
                    "flexibility": round(flexibility, 3),
                    "synergy": round(
                        best_build_breakdown.get("synergy", 0.0), 3
                    ),
                    "key_part": round(
                        best_build_breakdown.get("key_part", 0.0), 3
                    ),
                    "preferred_build": round(
                        best_build_breakdown.get("preferred_build", 0.0), 3
                    ),
                    "redundancy": round(-redundancy, 3),
                },
            }
        )

    choices.sort(key=lambda value: float(value["score"]), reverse=True)
    return {
        "source": source,
        "recommended": choices[0] if choices else None,
        "choices": choices,
    }


def evaluate_upgrade_choices(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> dict[str, Any]:
    upgrade_state = dict(state)
    upgrade_state["upgrade_candidates"] = [
        ball
        for ball in _as_list(state.get("deck_balls"))
        if isinstance(ball, dict) and bool(ball.get("can_upgrade", False))
    ]
    result = evaluate_ball_choices(
        upgrade_state,
        profiles,
        preferred_profile_id,
        source="upgrade_candidates",
    )
    for choice in result["choices"]:
        level = next(
            (
                int(ball.get("upgrade_level", 0))
                for ball in upgrade_state["upgrade_candidates"]
                if ball.get("instance_id") == choice.get("instance_id")
            ),
            0,
        )
        upgrade_gain = 24.0 - min(level, 2) * 4.0
        choice["breakdown"]["upgrade_gain"] = round(upgrade_gain, 3)
        choice["score"] = round(float(choice["score"]) + upgrade_gain, 3)
    result["choices"].sort(
        key=lambda value: float(value["score"]), reverse=True
    )
    result["recommended"] = (
        result["choices"][0] if result["choices"] else None
    )
    result["source"] = "upgrade_candidates"
    return result


def evaluate_relic_choices(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> dict[str, Any]:
    hypotheses = evaluate_build_hypotheses(
        state,
        profiles,
        preferred_profile_id,
    )
    top_build_id = str(hypotheses[0]["id"]) if hypotheses else ""
    top_commitment = (
        float(hypotheses[0]["commitment"]) if hypotheses else 0.0
    )
    hp_ratio, _, _, _ = _player_status(state)
    player = _as_dict(state.get("player"))
    money = max(0, int(player.get("money", 0)))
    choices: list[dict[str, Any]] = []

    for relic in _as_list(state.get("relics")):
        if (
            not isinstance(relic, dict)
            or bool(relic.get("owned", False))
            or int(relic.get("price", 0)) > money
        ):
            continue
        canonical_name = _canonical_relic_name(relic.get("name"))
        standalone = RELIC_STANDALONE_VALUE.get(canonical_name, 10.0)
        survival = 0.0
        if hp_ratio < 0.5 and canonical_name in {
            "Guard Core",
            "Emergency Repair Kit",
        }:
            survival = (0.5 - hp_ratio) / 0.5 * 28.0

        best_build: dict[str, Any] | None = None
        best_synergy = 0.0
        for hypothesis in hypotheses:
            profile_id = str(hypothesis["id"])
            profile = profiles.get(profile_id, {})
            policy = _as_dict(profile.get("relic_policy"))
            priority_names = [
                _canonical_relic_name(name)
                for name in _as_list(policy.get("attack_priority"))
            ]
            fit = _priority_fit(canonical_name, priority_names)
            synergy = fit * (
                10.0 + float(hypothesis["commitment"]) * 22.0
            )
            if canonical_name in CORE_RELICS.get(profile_id, ()):
                key_part_multiplier = (
                    1.0
                    if profile_id == top_build_id
                    else max(0.15, 1.0 - top_commitment)
                )
                synergy += (
                    1.0 - float(hypothesis["completion"])
                ) * 34.0 * key_part_multiplier
            if bool(hypothesis["preferred"]):
                synergy += 2.0
            if synergy > best_synergy:
                best_synergy = synergy
                best_build = hypothesis

        price_penalty = max(0, int(relic.get("price", 0)) - money // 2) * 0.3
        total = standalone + survival + best_synergy - price_penalty
        choices.append(
            {
                "index": int(relic.get("index", -1)),
                "name": str(relic.get("name", "")),
                "canonical_name": canonical_name,
                "score": round(total, 3),
                "chosen_build_id": (
                    str(best_build["id"]) if best_build else None
                ),
                "breakdown": {
                    "standalone_value": round(standalone, 3),
                    "survival": round(survival, 3),
                    "synergy_and_key_part": round(best_synergy, 3),
                    "price_pressure": round(-price_penalty, 3),
                },
            }
        )

    choices.sort(key=lambda value: float(value["score"]), reverse=True)
    return {
        "recommended": choices[0] if choices else None,
        "choices": choices,
    }


def evaluate_risk_tradeoff(
    state: dict[str, Any],
    benefit_score: float,
    hp_cost: int = 0,
    enemy_strength_increase: float = 0.0,
    floors_to_recovery: int = 1,
) -> dict[str, Any]:
    """将来のビルド利益を、HP消費と敵強化のリスクに対して評価する。"""
    hp_ratio, current_hp, maximum_hp, _ = _player_status(state)
    projected_hp = max(0, current_hp - max(0, hp_cost))
    projected_ratio = projected_hp / maximum_hp
    recovery_distance = max(0, floors_to_recovery)
    minimum_survival_ratio = _clamp(
        0.20 + recovery_distance * 0.04,
        0.20,
        0.40,
    )
    hp_penalty = max(0, hp_cost) / maximum_hp * 100.0
    enemy_penalty = max(0.0, enemy_strength_increase) * 24.0
    low_hp_penalty = (
        max(0.0, 0.45 - projected_ratio) * 70.0
    )
    risk_penalty = hp_penalty + enemy_penalty + low_hp_penalty
    net_value = float(benefit_score) - risk_penalty
    survival_floor_met = projected_ratio >= minimum_survival_ratio
    accept = survival_floor_met and net_value > 0.0
    return {
        "accept": accept,
        "benefit_score": round(float(benefit_score), 3),
        "risk_penalty": round(risk_penalty, 3),
        "net_value": round(net_value, 3),
        "current_hp_ratio": round(hp_ratio, 4),
        "projected_hp_ratio": round(projected_ratio, 4),
        "minimum_survival_ratio": round(minimum_survival_ratio, 4),
        "survival_floor_met": survival_floor_met,
        "breakdown": {
            "hp_cost": round(-hp_penalty, 3),
            "enemy_strength": round(-enemy_penalty, 3),
            "low_hp_exposure": round(-low_hp_penalty, 3),
        },
    }


def evaluate_clear_reward(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> dict[str, Any]:
    player = _as_dict(state.get("player"))
    money = max(0, int(player.get("money", 0)))
    catalog = evaluate_ball_choices(
        state,
        profiles,
        preferred_profile_id,
        source="catalog_balls",
    )
    affordable_upgrade_balls: list[dict[str, Any]] = []
    for ball in _as_list(state.get("deck_balls")):
        if not isinstance(ball, dict) or not bool(ball.get("can_upgrade", False)):
            continue
        level = int(ball.get("upgrade_level", 0))
        default_cost = 15 if level == 0 else 30 if level == 1 else -1
        raw_cost = ball.get("clear_reward_upgrade_cost", default_cost)
        upgrade_cost = int(raw_cost) if raw_cost is not None else -1
        if upgrade_cost < 0 or money < upgrade_cost:
            continue
        candidate = dict(ball)
        candidate["clear_reward_upgrade_cost"] = upgrade_cost
        affordable_upgrade_balls.append(candidate)
    reward_state = dict(state)
    reward_state["deck_balls"] = affordable_upgrade_balls
    upgrades = evaluate_upgrade_choices(
        reward_state,
        profiles,
        preferred_profile_id,
    )
    hypotheses = evaluate_build_hypotheses(
        state,
        profiles,
        preferred_profile_id,
    )
    active_build = hypotheses[0]["id"] if hypotheses else preferred_profile_id
    active_profile = profiles.get(str(active_build), {})
    policy = _as_dict(active_profile.get("stage_choice_policy"))
    target_deck_size = max(1, int(policy.get("target_deck_size", 8)))
    deck_size = len(_as_list(state.get("deck_balls")))

    options: list[dict[str, Any]] = []
    if catalog["recommended"] is not None:
        deck_pressure = max(0, deck_size - target_deck_size + 1) * 25.0
        options.append(
            {
                "reward": "new_ball",
                "score": round(
                    float(catalog["recommended"]["score"])
                    - deck_pressure,
                    3,
                ),
                "catalog_index": int(catalog["recommended"]["index"]),
                "instance_id": 0,
                "chosen_build_id": catalog["recommended"][
                    "chosen_build_id"
                ],
                "reason": "build_fit_with_deck_pressure",
            }
        )
    if upgrades["recommended"] is not None:
        upgrade_instance_id = upgrades["recommended"]["instance_id"]
        upgrade_cost = next(
            int(ball["clear_reward_upgrade_cost"])
            for ball in affordable_upgrade_balls
            if ball.get("instance_id") == upgrade_instance_id
        )
        options.append(
            {
                "reward": "upgrade_ball",
                "score": round(
                    float(upgrades["recommended"]["score"])
                    + 8.0
                    - float(upgrade_cost) * 0.6,
                    3,
                ),
                "catalog_index": -1,
                "instance_id": upgrade_instance_id,
                "upgrade_cost": upgrade_cost,
                "chosen_build_id": upgrades["recommended"][
                    "chosen_build_id"
                ],
                "reason": "paid_upgrade_without_deck_growth",
            }
        )
    money_value = 26.0 + (18.0 if money < 20 else 0.0)
    if deck_size >= target_deck_size:
        money_value += 12.0
    options.append(
        {
            "reward": "extra_money",
            "score": round(money_value, 3),
            "catalog_index": -1,
            "instance_id": 0,
            "chosen_build_id": str(active_build),
            "reason": "flexible_future_value",
        }
    )
    options.sort(key=lambda value: float(value["score"]), reverse=True)
    return {
        "recommended": options[0],
        "options": options,
        "target_deck_size": target_deck_size,
        "current_deck_size": deck_size,
    }


def build_decision_snapshot(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> dict[str, Any]:
    """MCPと自動操作向けに、JSONへ変換可能な判断情報を1つ構築する。"""
    hypotheses = evaluate_build_hypotheses(
        state,
        profiles,
        preferred_profile_id,
    )
    return {
        "preferred_build_id": preferred_profile_id,
        "active_build_id": (
            str(hypotheses[0]["id"])
            if hypotheses
            else preferred_profile_id
        ),
        "hypotheses": hypotheses,
        "offered_ball_choices": evaluate_ball_choices(
            state,
            profiles,
            preferred_profile_id,
            source="offered_balls",
        ),
        "catalog_ball_choices": evaluate_ball_choices(
            state,
            profiles,
            preferred_profile_id,
            source="catalog_balls",
        ),
        "upgrade_choices": evaluate_upgrade_choices(
            state,
            profiles,
            preferred_profile_id,
        ),
        "relic_choices": evaluate_relic_choices(
            state,
            profiles,
            preferred_profile_id,
        ),
        "clear_reward": evaluate_clear_reward(
            state,
            profiles,
            preferred_profile_id,
        ),
        "risk_context": evaluate_risk_tradeoff(
            state,
            benefit_score=0.0,
        ),
    }
