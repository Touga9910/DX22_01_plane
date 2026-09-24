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
    "player_cushion_charge": {
        "standalone_value": 10.0,
        "tags": ("bounce", "bank", "setup"),
    },
    "player_anchor": {
        "standalone_value": 13.0,
        "tags": ("anchor", "defense", "control"),
    },
    "player_chain_impact": {
        "standalone_value": 15.0,
        "tags": ("heavy", "chain", "collision"),
    },
    "player_refractive_pierce": {
        "standalone_value": 13.0,
        "tags": ("pierce", "precision", "chain"),
    },
    "player_stop_shield": {
        "standalone_value": 14.0,
        "tags": ("anchor", "defense", "control"),
    },
	"player_trace_driver": {
		"standalone_value": 11.0,
		"tags": ("pierce", "trace", "setup"),
	},
	"player_pierce_finisher": {
		"standalone_value": 14.0,
		"tags": ("pierce", "trace", "finisher"),
	},
	"player_ricochet_finisher": {
		"standalone_value": 14.0,
		"tags": ("bounce", "bank", "finisher"),
	},
	"player_anchor_finisher": {
		"standalone_value": 14.0,
		"tags": ("anchor", "control", "finisher"),
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


def _build_parts(profile: dict[str, Any]) -> tuple[list[str], list[str]]:
    core = [str(value) for value in _as_list(profile.get("core_parts"))]
    support = [
        str(value) for value in _as_list(profile.get("support_parts"))
    ]
    if not core:
        priorities = [
            str(value)
            for value in _as_list(profile.get("new_ball_priority"))
        ]
        core = priorities[:1]
    return core, support


def build_progress_snapshot(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    profile_id: str,
) -> dict[str, Any]:
    profile = profiles.get(profile_id, {})
    core_parts, support_parts = _build_parts(profile)
    counts = _ball_counts(state)
    owned_core = [part for part in core_parts if counts.get(part, 0) > 0]
    missing_core = [part for part in core_parts if counts.get(part, 0) == 0]
    owned_support = [
        part for part in support_parts if counts.get(part, 0) > 0
    ]
    missing_support = [
        part for part in support_parts if counts.get(part, 0) == 0
    ]
    total_parts = len(core_parts) + len(support_parts)
    completion_rate = (
        (len(owned_core) + len(owned_support)) / total_parts
        if total_parts
        else 1.0
    )
    core_completion_rate = (
        len(owned_core) / len(core_parts) if core_parts else 1.0
    )
    if not missing_core:
        phase = "reinforcement"
    elif not owned_core:
        phase = "foundation"
    else:
        phase = "synergy"
    return {
        "profile_id": profile_id,
        "phase": phase,
        "build_complete": not missing_core,
        "completion_rate": round(completion_rate, 4),
        "core_completion_rate": round(core_completion_rate, 4),
        "owned_core_parts": owned_core,
        "missing_core_parts": missing_core,
        "owned_support_parts": owned_support,
        "missing_support_parts": missing_support,
        "missing_key_parts": [*missing_core, *missing_support],
    }


def _reward_priority_bonus(
    profile: dict[str, Any],
    reward: str,
) -> float:
    reward = {
        "upgrade_ball": "ball_upgrade",
        "extra_money": "money",
    }.get(reward, reward)
    priorities = [
        str(value) for value in _as_list(profile.get("reward_priority"))
    ]
    try:
        rank = priorities.index(reward)
    except ValueError:
        return 0.0
    return float(max(0, len(priorities) - rank - 1) * 4)


def evaluate_build_hypotheses(
    state: dict[str, Any],
    profiles: dict[str, dict[str, Any]],
    preferred_profile_id: str = "",
) -> list[dict[str, Any]]:
    """指定ビルドを目的に保ちつつ、全ビルドの現在適合度も評価する。"""
    counts = _ball_counts(state)
    owned_relics = _owned_relic_names(state)
    hypotheses: list[dict[str, Any]] = []

    for profile_id, profile in profiles.items():
        priorities = [
            str(value)
            for value in _as_list(profile.get("new_ball_priority"))
        ]
        progress = build_progress_snapshot(state, profiles, profile_id)
        core_parts, _ = _build_parts(profile)
        core_ball = core_parts[0] if core_parts else ""
        core_ball_count = counts.get(core_ball, 0)
        ball_completion = float(progress["completion_rate"])
        core_relics = CORE_RELICS.get(profile_id, ())
        relic_matches = [
            relic for relic in core_relics if relic in owned_relics
        ]
        relic_completion = (
            len(relic_matches) / len(core_relics)
            if core_relics
            else 1.0
        )

        # A large starting stack of one generic ball must not permanently
        # dominate build affinity. Each definition contributes at most two.
        capped_counts = {
            definition_id: min(count, 2)
            for definition_id, count in counts.items()
        }
        deck_count = sum(capped_counts.values())
        affinity = (
            sum(
                count * _priority_fit(definition_id, priorities)
                for definition_id, count in capped_counts.items()
            ) / deck_count
            if deck_count > 0
            else 0.0
        )
        completion = ball_completion * 0.85 + relic_completion * 0.15
        preferred_bonus = 0.22 if profile_id == preferred_profile_id else 0.0
        commitment = _clamp(
            0.10 + completion * 0.68 + affinity * 0.14 + preferred_bonus,
            0.05,
            1.0,
        )
        fit_score = (
            completion * 55.0
            + affinity * 30.0
            + preferred_bonus * 400.0
        )

        missing = list(progress["missing_key_parts"])
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
                "phase": progress["phase"],
                "build_complete": progress["build_complete"],
                "completion_rate": progress["completion_rate"],
                "core_completion_rate": progress["core_completion_rate"],
                "owned_core_parts": progress["owned_core_parts"],
                "missing_core_parts": progress["missing_core_parts"],
                "owned_support_parts": progress["owned_support_parts"],
                "missing_support_parts": progress["missing_support_parts"],
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
    chain_radius = max(0.0, float(status.get("chainImpactRadius", 0.0)))
    if chain_radius > 0.0 and living_enemy_count > 1:
        immediate += min(16.0, chain_radius * 0.8)
    if bool(status.get("refractAfterPierce", False)) and living_enemy_count > 1:
        immediate += 10.0
    shield = max(0.0, float(status.get("stopShieldAmount", 0.0)))
    immediate += shield * 2.0
    survival = 0.0
    if hp_ratio < 0.5:
        urgency = (0.5 - hp_ratio) / 0.5
        survival = defense * 12.0 * urgency
        if bool(status.get("anchor", False)):
            survival += 12.0 * urgency
        survival += shield * 5.0 * urgency
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
    preferred_build_id = (
        preferred_profile_id
        if preferred_profile_id in profiles
        else (str(hypotheses[0]["id"]) if hypotheses else "")
    )
    preferred_profile = profiles.get(preferred_build_id, {})
    preferred_hypothesis = next(
        (
            hypothesis
            for hypothesis in hypotheses
            if hypothesis["id"] == preferred_build_id
        ),
        hypotheses[0] if hypotheses else {},
    )
    progress = build_progress_snapshot(
        state,
        profiles,
        preferred_build_id,
    )
    preferred_priorities = [
        str(value)
        for value in _as_list(preferred_profile.get("new_ball_priority"))
    ]
    core_parts, support_parts = _build_parts(preferred_profile)
    target_parts = set(core_parts) | set(support_parts)
    missing_core = set(progress["missing_core_parts"])
    missing_support = set(progress["missing_support_parts"])
    build_complete = bool(progress["build_complete"])
    preferred_commitment = float(
        preferred_hypothesis.get("commitment", 0.1)
    )
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
        flexibility_count = sum(
            _priority_rank(
                definition_id,
                [
                    str(value)
                    for value in _as_list(
                        profiles.get(str(hypothesis["id"]), {}).get(
                            "new_ball_priority"
                        )
                    )
                ],
            ) <= 1
            for hypothesis in hypotheses
        )
        preferred_fit = _priority_fit(
            definition_id,
            preferred_priorities,
        )
        synergy = preferred_fit * (
            16.0 + preferred_commitment * 20.0
        )
        if build_complete:
            synergy *= 0.55
        key_part = 0.0
        support_part = 0.0
        future_synergy = 0.0
        if definition_id in missing_core:
            key_part = 140.0 + (
                1.0 - float(progress["core_completion_rate"])
            ) * 30.0
            future_synergy = 28.0
        elif definition_id in missing_support:
            support_part = 52.0
            future_synergy = 18.0

        preferred_bonus = 0.0
        if definition_id in target_parts:
            preferred_bonus = 18.0 if not build_complete else 4.0

        definition_weights = _as_dict(
            preferred_profile.get("ball_score_weights")
        )
        raw_definition_bonus = float(
            _as_dict(definition_weights.get("definition_bonus")).get(
                definition_id,
                0.0,
            )
        )
        if definition_id in missing_core:
            definition_scale = 0.22
        elif definition_id in missing_support:
            definition_scale = 0.14
        elif definition_id in target_parts and not build_complete:
            definition_scale = 0.08
        elif definition_id in target_parts:
            definition_scale = 0.04
        else:
            definition_scale = 0.0
        definition_bonus = raw_definition_bonus * definition_scale

        alternative_synergy = 0.0
        alternative_build_id = ""
        for hypothesis in hypotheses:
            profile_id = str(hypothesis["id"])
            if profile_id == preferred_build_id:
                continue
            profile = profiles.get(profile_id, {})
            priorities = [
                str(value)
                for value in _as_list(profile.get("new_ball_priority"))
            ]
            fit = _priority_fit(definition_id, priorities)
            candidate_alternative = fit * (
                8.0 + float(hypothesis["commitment"]) * 12.0
            )
            if candidate_alternative > alternative_synergy:
                alternative_synergy = candidate_alternative
                alternative_build_id = profile_id
        alternative_cap = 32.0 if build_complete else 10.0
        alternative_synergy = min(alternative_synergy, alternative_cap)

        flexibility = min(5.0, flexibility_count * 2.5)
        if not build_complete and definition_id not in target_parts:
            flexibility *= 0.5
        generic_factor = 1.35 if build_complete else 0.75
        generic_value = standalone * (
            1.0 - min(preferred_commitment, 0.75)
        ) * generic_factor
        if not build_complete and definition_id not in target_parts:
            generic_value *= 0.55
        same_count = counts.get(definition_id, 0)
        redundancy = max(0, same_count - 2) * 9.0
        total = (
            immediate
            + survival
            + generic_value
            + flexibility
            + synergy
            + key_part
            + support_part
            + future_synergy
            + preferred_bonus
            + definition_bonus
            + alternative_synergy
            - redundancy
        )
        chosen_build_id = (
            preferred_build_id
            if definition_id in target_parts or synergy >= alternative_synergy
            else alternative_build_id or preferred_build_id
        )
        choice = {
                "index": int(ball.get("index", -1)),
                "instance_id": ball.get("instance_id", 0),
                "definition_id": definition_id,
                "score": round(total, 3),
                "chosen_build_id": chosen_build_id,
                "construction_phase": progress["phase"],
                "build_completion": progress["completion_rate"],
                "missing_key_parts": progress["missing_key_parts"],
                "breakdown": {
                    "immediate_power": round(immediate, 3),
                    "survival": round(survival, 3),
                    "generic_early_value": round(generic_value, 3),
                    "flexibility": round(flexibility, 3),
                    "synergy": round(synergy, 3),
                    "key_part": round(key_part, 3),
                    "support_part": round(support_part, 3),
                    "future_synergy": round(future_synergy, 3),
                    "preferred_build": round(preferred_bonus, 3),
                    "definition_bonus": round(definition_bonus, 3),
                    "alternative_synergy": round(alternative_synergy, 3),
                    "redundancy": round(-redundancy, 3),
                },
            }
        if "offer_index" in ball:
            choice["offer_index"] = int(ball.get("offer_index", -1))
        if "catalog_index" in ball:
            choice["catalog_index"] = int(ball.get("catalog_index", -1))
        choices.append(choice)

    choices.sort(key=lambda value: float(value["score"]), reverse=True)
    return {
        "source": source,
        "preferred_build_id": preferred_build_id,
        "build_progress": progress,
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
    preferred_build_id = (
        preferred_profile_id
        if preferred_profile_id in profiles
        else (str(hypotheses[0]["id"]) if hypotheses else "")
    )
    progress = build_progress_snapshot(
        state,
        profiles,
        preferred_build_id,
    )
    preferred_hypothesis = next(
        (
            hypothesis
            for hypothesis in hypotheses
            if str(hypothesis["id"]) == preferred_build_id
        ),
        hypotheses[0] if hypotheses else {},
    )
    preferred_commitment = float(
        preferred_hypothesis.get("commitment", 0.1)
    )
    build_complete = bool(progress["build_complete"])
    hp_ratio, _, _, _ = _player_status(state)
    player = _as_dict(state.get("player"))
    money = max(0, int(player.get("money", 0)))
    actions = set(state.get("available_actions", []))
    if "choose_relic" in actions:
        selection_context = "midboss_reward"
    elif state.get("scene") == "shop":
        selection_context = "shop"
    else:
        selection_context = "catalog"
    choices: list[dict[str, Any]] = []

    for relic in _as_list(state.get("relics")):
        if (
            not isinstance(relic, dict)
            or bool(relic.get("owned", False))
        ):
            continue
        price = int(relic.get("price", 0))
        if selection_context == "midboss_reward":
            selectable = bool(relic.get("midboss_offered", False))
        elif selection_context == "shop":
            selectable = (
                "buy_relic" in actions
                and bool(relic.get("shop_offered", False))
                and price <= money
            )
        else:
            selectable = price <= money
        canonical_name = _canonical_relic_name(relic.get("name"))
        standalone = RELIC_STANDALONE_VALUE.get(canonical_name, 10.0)
        survival = 0.0
        if hp_ratio < 0.5 and canonical_name in {
            "Guard Core",
            "Emergency Repair Kit",
        }:
            survival = (0.5 - hp_ratio) / 0.5 * 28.0

        preferred_policy = _as_dict(
            profiles.get(preferred_build_id, {}).get("relic_policy")
        )
        preferred_priorities = [
            _canonical_relic_name(name)
            for name in _as_list(preferred_policy.get("attack_priority"))
        ]
        preferred_fit = _priority_fit(canonical_name, preferred_priorities)
        preferred_synergy = preferred_fit * (
            14.0 + preferred_commitment * 24.0
        )
        missing_preferred_relics = [
            name
            for name in CORE_RELICS.get(preferred_build_id, ())
            if name not in _owned_relic_names(state)
        ]
        key_part = (
            46.0 if canonical_name in missing_preferred_relics else 0.0
        )
        preferred_bonus = (
            10.0
            if canonical_name in CORE_RELICS.get(preferred_build_id, ())
            else 0.0
        )
        alternative_synergy = 0.0
        alternative_build_id = ""
        for hypothesis in hypotheses:
            profile_id = str(hypothesis["id"])
            if profile_id == preferred_build_id:
                continue
            profile = profiles.get(profile_id, {})
            policy = _as_dict(profile.get("relic_policy"))
            priority_names = [
                _canonical_relic_name(name)
                for name in _as_list(policy.get("attack_priority"))
            ]
            fit = _priority_fit(canonical_name, priority_names)
            synergy = fit * (
                8.0 + float(hypothesis["commitment"]) * 14.0
            )
            if canonical_name in CORE_RELICS.get(profile_id, ()):
                synergy += 12.0
            if synergy > alternative_synergy:
                alternative_synergy = synergy
                alternative_build_id = profile_id
        alternative_synergy = min(
            alternative_synergy,
            28.0 if build_complete else 8.0,
        )
        build_value = (
            preferred_synergy
            + key_part
            + preferred_bonus
            + alternative_synergy
        )

        price_penalty = (
            0.0 if selection_context == "midboss_reward"
            else max(0, price - money // 2) * 0.3
        )
        total = standalone + survival + build_value - price_penalty
        chosen_build_id = (
            preferred_build_id
            if (
                canonical_name in CORE_RELICS.get(preferred_build_id, ())
                or preferred_synergy >= alternative_synergy
            )
            else alternative_build_id or preferred_build_id
        )
        choices.append(
            {
                "index": int(relic.get("index", -1)),
                "name": str(relic.get("name", "")),
                "canonical_name": canonical_name,
                "score": round(total, 3),
                "selectable": selectable,
                "chosen_build_id": chosen_build_id,
                "construction_phase": progress["phase"],
                "build_completion": progress["completion_rate"],
                "missing_key_parts": [
                    *progress["missing_key_parts"],
                    *missing_preferred_relics,
                ],
                "breakdown": {
                    "standalone_value": round(standalone, 3),
                    "survival": round(survival, 3),
                    "preferred_synergy": round(preferred_synergy, 3),
                    "key_part": round(key_part, 3),
                    "preferred_build": round(preferred_bonus, 3),
                    "alternative_synergy": round(
                        alternative_synergy,
                        3,
                    ),
                    "synergy_and_key_part": round(build_value, 3),
                    "price_pressure": round(-price_penalty, 3),
                },
            }
        )

    choices.sort(key=lambda value: float(value["score"]), reverse=True)
    offered_choices = [choice for choice in choices if choice["selectable"]]
    return {
        "selection_context": selection_context,
        "preferred_build_id": preferred_build_id,
        "build_progress": progress,
        "recommended": offered_choices[0] if offered_choices else None,
        "choices": offered_choices,
        "offered_choices": offered_choices,
        "global_evaluation": choices,
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
    new_ball_source = (
        "clear_reward_ball_offers"
        if "clear_reward_ball_offers" in state
        else "catalog_balls"
    )
    catalog = evaluate_ball_choices(
        state,
        profiles,
        preferred_profile_id,
        source=new_ball_source,
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
    active_build = (
        preferred_profile_id
        if preferred_profile_id in profiles
        else str(catalog.get("preferred_build_id", preferred_profile_id))
    )
    active_profile = profiles.get(str(active_build), {})
    progress = build_progress_snapshot(
        state,
        profiles,
        str(active_build),
    )
    policy = _as_dict(active_profile.get("stage_choice_policy"))
    target_deck_size = max(1, int(policy.get("target_deck_size", 8)))
    deck_size = len(_as_list(state.get("deck_balls")))

    options: list[dict[str, Any]] = []
    if catalog["recommended"] is not None:
        selected_ball = catalog["recommended"]
        selected_definition = str(selected_ball.get("definition_id", ""))
        is_missing_core = selected_definition in set(
            progress["missing_core_parts"]
        )
        is_missing_support = selected_definition in set(
            progress["missing_support_parts"]
        )
        deck_pressure_scale = (
            0.0 if is_missing_core else 0.35 if is_missing_support else 1.0
        )
        deck_pressure = (
            max(0, deck_size - target_deck_size + 1)
            * 25.0
            * deck_pressure_scale
        )
        new_ball_priority = _reward_priority_bonus(
            active_profile,
            "new_ball",
        )
        if is_missing_core:
            reason = "missing_core_part"
        elif is_missing_support:
            reason = "missing_support_part"
        else:
            reason = "build_fit_with_deck_pressure"
        options.append(
            {
                "reward": "new_ball",
                "score": round(
                    float(selected_ball["score"])
                    - deck_pressure
                    + new_ball_priority,
                    3,
                ),
                "offer_index": int(
                    selected_ball.get("offer_index", -1)
                ),
                "catalog_index": int(
                    selected_ball.get(
                        "catalog_index",
                        selected_ball["index"],
                    )
                ),
                "instance_id": 0,
                "definition_id": selected_definition,
                "chosen_build_id": str(active_build),
                "reason": reason,
                "breakdown": {
                    "ball_score": round(float(selected_ball["score"]), 3),
                    "deck_pressure": round(-deck_pressure, 3),
                    "reward_priority": round(new_ball_priority, 3),
                },
            }
        )
    if upgrades["recommended"] is not None:
        upgrade_instance_id = upgrades["recommended"]["instance_id"]
        upgrade_cost = next(
            int(ball["clear_reward_upgrade_cost"])
            for ball in affordable_upgrade_balls
            if ball.get("instance_id") == upgrade_instance_id
        )
        upgrade_priority = _reward_priority_bonus(
            active_profile,
            "upgrade_ball",
        )
        options.append(
            {
                "reward": "upgrade_ball",
                "score": round(
                    float(upgrades["recommended"]["score"])
                    + 8.0
                    - float(upgrade_cost) * 0.6
                    + upgrade_priority,
                    3,
                ),
                "catalog_index": -1,
                "instance_id": upgrade_instance_id,
                "upgrade_cost": upgrade_cost,
                "chosen_build_id": upgrades["recommended"][
                    "chosen_build_id"
                ],
                "reason": "paid_upgrade_without_deck_growth",
                "breakdown": {
                    "ball_score": round(
                        float(upgrades["recommended"]["score"]),
                        3,
                    ),
                    "upgrade_value": 8.0,
                    "upgrade_cost": round(-float(upgrade_cost) * 0.6, 3),
                    "reward_priority": round(upgrade_priority, 3),
                },
            }
        )
    money_value = 26.0 + (18.0 if money < 20 else 0.0)
    if deck_size >= target_deck_size:
        money_value += 12.0
    money_priority = _reward_priority_bonus(active_profile, "extra_money")
    money_value += money_priority
    options.append(
        {
            "reward": "extra_money",
            "score": round(money_value, 3),
            "catalog_index": -1,
            "instance_id": 0,
            "chosen_build_id": str(active_build),
            "reason": "flexible_future_value",
            "breakdown": {
                "base_and_economy": round(money_value - money_priority, 3),
                "reward_priority": round(money_priority, 3),
            },
        }
    )
    options.sort(key=lambda value: float(value["score"]), reverse=True)
    return {
        "recommended": options[0],
        "options": options,
        "preferred_build_id": str(active_build),
        "construction_phase": progress["phase"],
        "build_progress": progress,
        "new_ball_choices": catalog["choices"],
        "upgrade_choices": upgrades["choices"],
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
    active_build_id = (
        preferred_profile_id
        if preferred_profile_id in profiles
        else (
            str(hypotheses[0]["id"])
            if hypotheses
            else preferred_profile_id
        )
    )
    return {
        "preferred_build_id": preferred_profile_id,
        "active_build_id": active_build_id,
        "build_progress": build_progress_snapshot(
            state,
            profiles,
            active_build_id,
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
