"""ランログからビルド、経済、経路、ポケットのテレメトリを集計する。"""

from __future__ import annotations

import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


LoadedRun = tuple[Path, dict[str, Any], dict[str, Any]]


def _counter(counter: Counter[str]) -> dict[str, int]:
    return dict(sorted(counter.items()))


def _rate(numerator: int | float, denominator: int | float) -> float:
    return round(numerator / denominator, 4) if denominator else 0.0


def _average(total: int | float, count: int) -> float:
    return round(total / count, 4) if count else 0.0


def _details(event: dict[str, Any]) -> dict[str, Any]:
    details = event.get("details", {})
    return details if isinstance(details, dict) else {}


def _configuration_supports_pockets(run: dict[str, Any]) -> bool:
    configuration = run.get("configuration", {})
    if not isinstance(configuration, dict):
        return False
    files = configuration.get("files", [])
    if not isinstance(files, list):
        return False
    return any(
        isinstance(entry, dict)
        and str(entry.get("path", ""))
        .replace("\\", "/")
        .lower()
        .endswith("assets/data/pocket_rules.json")
        for entry in files
    )


def calculate_system_metrics(
    selected_runs: list[LoadedRun],
) -> dict[str, Any]:
    run_count = len(selected_runs)

    reward_offered: Counter[str] = Counter()
    reward_chosen: Counter[str] = Counter()
    ball_offered: Counter[str] = Counter()
    acquired_balls: Counter[str] = Counter()
    ball_offer_progress: dict[str, list[int]] = defaultdict(list)
    ball_acquisition_progress: dict[str, list[int]] = defaultdict(list)
    upgrade_balls: Counter[str] = Counter()
    upgrade_sources: Counter[str] = Counter()
    relic_purchases: Counter[str] = Counter()
    relic_effect_triggers: Counter[str] = Counter()
    ball_shots: Counter[str] = Counter()
    final_ball_copies: Counter[str] = Counter()
    reward_offer_count = 0
    reward_choice_count = 0
    mapped_shot_count = 0
    unmapped_shot_count = 0
    final_deck_run_count = 0
    final_deck_size_total = 0
    final_upgrade_level_total = 0
    final_deck_ball_total = 0
    runs_with_relic_purchase = 0

    route_candidate_slots: Counter[str] = Counter()
    route_offered_decisions: Counter[str] = Counter()
    route_selected: Counter[str] = Counter()
    route_controller_counts: Counter[str] = Counter()
    route_decision_count = 0
    invalid_route_selection_count = 0

    total_stage_money = 0
    total_extra_money = 0
    total_relic_spending = 0
    total_final_money = 0
    money_observed_run_count = 0
    rest_heal_count = 0
    rest_heal_total = 0
    rest_heal_waste_total = 0
    shop_visit_count = 0
    shop_purchase_count = 0
    shop_visits_without_purchase = 0
    final_money_values: list[int] = []

    pocket_controlled = 0
    pocket_returned = 0
    pocket_live_finishers = 0
    pocket_post_defeat_finishers = 0
    player_pocket_count = 0
    player_pocket_damage = 0
    pocket_by_stage_type: dict[str, Counter[str]] = {}
    pocket_by_enemy: dict[str, Counter[str]] = {}
    pocket_supported_run_count = 0
    runs_using_tactical_pocket = 0
    total_shot_count = 0

    event_log_run_count = 0
    shot_selection_run_count = 0
    monetary_event_run_count = 0

    for _, run, _ in selected_runs:
        events = run.get("events", [])
        if not isinstance(events, list):
            events = []
        else:
            event_log_run_count += 1
        if _configuration_supports_pockets(run):
            pocket_supported_run_count += 1

        run_context = run.get("run_context", {})
        if not isinstance(run_context, dict):
            run_context = {}
        current_money = int(run_context.get("initial_money", 0))
        money_observed = False
        last_reward_offer: dict[str, Any] | None = None
        run_relics: set[str] = set()
        run_used_tactical_pocket = False
        run_shop_visits = 0
        run_shop_purchases = 0

        for event in events:
            if not isinstance(event, dict):
                continue
            event_type = str(event.get("event_type", ""))
            details = _details(event)

            if "money_after" in details:
                current_money = int(details.get("money_after", current_money))
                money_observed = True

            if event_type == "clear_reward_offered":
                reward_offer_count += 1
                last_reward_offer = details
                event_progress = int(event.get("stage_index", 0))
                reward_types = details.get("reward_types", [])
                if isinstance(reward_types, list):
                    reward_offered.update({
                        str(reward_type)
                        for reward_type in reward_types
                    })
                candidates = details.get("new_ball_candidates", [])
                if isinstance(candidates, list):
                    offered_ids = {
                        str(candidate.get("ball_id", ""))
                        for candidate in candidates
                        if isinstance(candidate, dict)
                        and candidate.get("ball_id")
                    }
                    ball_offered.update(offered_ids)
                    for ball_id in offered_ids:
                        ball_offer_progress[ball_id].append(event_progress)
            elif event_type == "clear_reward_choice":
                reward_choice_count += 1
                reward = str(details.get("reward", "unknown"))
                reward_chosen[reward] += 1
                if reward == "new_ball" and last_reward_offer is not None:
                    candidates = last_reward_offer.get(
                        "new_ball_candidates",
                        [],
                    )
                    selected_index = int(details.get("selected_index", -1))
                    if (
                        isinstance(candidates, list)
                        and 0 <= selected_index < len(candidates)
                        and isinstance(candidates[selected_index], dict)
                    ):
                        ball_id = str(
                            candidates[selected_index].get("ball_id", "")
                        )
                        if ball_id:
                            acquired_balls[ball_id] += 1
                            ball_acquisition_progress[ball_id].append(
                                int(event.get("stage_index", 0))
                            )
                elif reward == "extra_money" and last_reward_offer is not None:
                    amount = int(
                        last_reward_offer.get("extra_money_amount", 0)
                    )
                    total_extra_money += amount
                last_reward_offer = None
            elif event_type == "ball_upgraded":
                ball_id = str(details.get("ball_id", "unknown"))
                upgrade_balls[ball_id] += 1
                upgrade_sources[str(details.get("source_scene", "unknown"))] += 1
            elif event_type == "relic_purchased":
                relic_name = str(details.get("relic_name", "unknown"))
                relic_purchases[relic_name] += 1
                run_relics.add(relic_name)
                total_relic_spending += int(details.get("cost", 0))
                shop_purchase_count += 1
                run_shop_purchases += 1
            elif event_type in {
                "shop_ball_purchased",
                "shop_ball_removed",
            }:
                shop_purchase_count += 1
                run_shop_purchases += 1
            elif event_type == "bank_shot_triggered":
                relic_effect_triggers["Bank Shot"] += 1
            elif event_type == "emergency_repair_triggered":
                relic_effect_triggers["Emergency Repair Kit"] += 1
            elif event_type == "stage_money_reward":
                total_stage_money += int(details.get("amount", 0))
            elif event_type == "rest_heal":
                rest_heal_count += 1
                actual_heal = int(details.get("heal_amount", 0))
                configured_heal = int(
                    details.get("configured_heal_amount", actual_heal)
                )
                rest_heal_total += actual_heal
                rest_heal_waste_total += max(0, configured_heal - actual_heal)
            elif event_type == "route_choice":
                route_decision_count += 1
                controller = str(details.get("controller", "unknown"))
                route_controller_counts[controller] += 1
                offered_routes = details.get("offered_routes", [])
                if not isinstance(offered_routes, list):
                    offered_routes = []
                offered_names = [str(route) for route in offered_routes]
                route_candidate_slots.update(offered_names)
                route_offered_decisions.update(set(offered_names))
                selected_route = str(details.get("selected_route", ""))
                selected_index = int(details.get("selected_index", -1))
                if selected_route:
                    route_selected[selected_route] += 1
                    if selected_route == "Shop":
                        shop_visit_count += 1
                        run_shop_visits += 1
                if (
                    selected_index < 0
                    or selected_index >= len(offered_names)
                    or offered_names[selected_index] != selected_route
                ):
                    invalid_route_selection_count += 1
            elif event_type in {
                "enemy_pocket_controlled",
                "enemy_pocket_finisher",
                "enemy_pocket_returned",
            }:
                stage_type = str(details.get("stage_type", "unknown"))
                enemy_id = str(details.get("enemy_id", "unknown"))
                stage_counter = pocket_by_stage_type.setdefault(
                    stage_type,
                    Counter(),
                )
                enemy_counter = pocket_by_enemy.setdefault(
                    enemy_id,
                    Counter(),
                )
                if event_type == "enemy_pocket_controlled":
                    pocket_controlled += 1
                    stage_counter["controlled"] += 1
                    enemy_counter["controlled"] += 1
                    run_used_tactical_pocket = True
                elif event_type == "enemy_pocket_returned":
                    pocket_returned += 1
                    stage_counter["returned"] += 1
                    enemy_counter["returned"] += 1
                elif bool(details.get("already_defeated", False)):
                    pocket_post_defeat_finishers += 1
                    stage_counter["post_defeat_finisher"] += 1
                    enemy_counter["post_defeat_finisher"] += 1
                else:
                    pocket_live_finishers += 1
                    stage_counter["live_finisher"] += 1
                    enemy_counter["live_finisher"] += 1
                    run_used_tactical_pocket = True

        if run_relics:
            runs_with_relic_purchase += 1
        if run_used_tactical_pocket:
            runs_using_tactical_pocket += 1
        if money_observed:
            monetary_event_run_count += 1
            total_final_money += current_money
            money_observed_run_count += 1
            final_money_values.append(current_money)
        shop_visits_without_purchase += max(
            0,
            run_shop_visits - run_shop_purchases,
        )

        stages = [
            stage
            for stage in run.get("stages", [])
            if isinstance(stage, dict)
        ]
        latest_deck: list[dict[str, Any]] = []
        run_has_mapped_shot = False
        for stage in stages:
            stage_context = stage.get("stage_context", {})
            if not isinstance(stage_context, dict):
                stage_context = {}
            deck = stage_context.get("deck", [])
            if not isinstance(deck, list):
                deck = []
            typed_deck = [item for item in deck if isinstance(item, dict)]
            if typed_deck:
                latest_deck = typed_deck
            instance_to_ball = {
                int(item.get("instance_id", -1)): str(item.get("id", ""))
                for item in typed_deck
                if item.get("id")
            }
            shots = stage.get("shots", [])
            if not isinstance(shots, list):
                continue
            total_shot_count += len(shots)
            for shot in shots:
                if not isinstance(shot, dict):
                    continue
                shot_context = shot.get("shot_context", {})
                if not isinstance(shot_context, dict):
                    shot_context = {}
                instance_id = int(
                    shot_context.get("selected_instance_id", -1)
                )
                ball_id = instance_to_ball.get(instance_id, "")
                if ball_id:
                    ball_shots[ball_id] += 1
                    mapped_shot_count += 1
                    run_has_mapped_shot = True
                else:
                    unmapped_shot_count += 1

            damage_events = stage.get("damage_events", [])
            if not isinstance(damage_events, list):
                continue
            for damage_event in damage_events:
                if (
                    isinstance(damage_event, dict)
                    and damage_event.get("source") == "pocket"
                ):
                    player_pocket_count += 1
                    player_pocket_damage += int(
                        damage_event.get("damage", 0)
                    )

        if run_has_mapped_shot:
            shot_selection_run_count += 1
        if latest_deck:
            final_deck_run_count += 1
            final_deck_size_total += len(latest_deck)
            for ball in latest_deck:
                ball_id = str(ball.get("id", "unknown"))
                final_ball_copies[ball_id] += 1
                final_upgrade_level_total += int(
                    ball.get("upgrade_level", 0)
                )
                final_deck_ball_total += 1

    reward_selection_rates = {
        reward: _rate(reward_chosen[reward], offered_count)
        for reward, offered_count in sorted(reward_offered.items())
    }
    ball_selection_rates = {
        ball_id: _rate(acquired_balls[ball_id], offered_count)
        for ball_id, offered_count in sorted(ball_offered.items())
    }
    route_selection_rates = {
        route: _rate(route_selected[route], offered_count)
        for route, offered_count in sorted(route_offered_decisions.items())
    }
    ball_shot_shares = {
        ball_id: _rate(count, mapped_shot_count)
        for ball_id, count in sorted(ball_shots.items())
    }
    tactical_enemy_pocket_count = (
        pocket_controlled + pocket_live_finishers
    )
    reward_completion_rate = _rate(
        reward_choice_count,
        reward_offer_count,
    )
    dominant_reward = max(
        reward_selection_rates,
        key=reward_selection_rates.get,
        default="",
    )
    dead_reward_types = [
        reward
        for reward, offered_count in sorted(reward_offered.items())
        if offered_count >= 5 and reward_chosen[reward] == 0
    ]

    return {
        "schema_version": 1,
        "sample_count": run_count,
        "coverage": {
            "event_log_run_count": event_log_run_count,
            "shot_selection_run_count": shot_selection_run_count,
            "monetary_event_run_count": monetary_event_run_count,
            "pocket_schema_run_count": pocket_supported_run_count,
        },
        "build": {
            "reward_decisions": {
                "offer_count": reward_offer_count,
                "choice_count": reward_choice_count,
                "completion_rate": reward_completion_rate,
                "offered_by_type": _counter(reward_offered),
                "chosen_by_type": _counter(reward_chosen),
                "selection_rate_when_offered": reward_selection_rates,
                "dominant_reward_type": dominant_reward,
                "dominant_selection_rate": (
                    reward_selection_rates.get(dominant_reward, 0.0)
                ),
                "dead_reward_types_minimum_five_offers": dead_reward_types,
            },
            "new_ball_choices": {
                "offered_by_ball_id": _counter(ball_offered),
                "acquired_by_ball_id": _counter(acquired_balls),
                "selection_rate_when_offered": ball_selection_rates,
                "average_offer_progress_by_ball_id": {
                    ball_id: round(statistics.fmean(values), 2)
                    for ball_id, values in sorted(ball_offer_progress.items())
                    if values
                },
                "average_acquisition_progress_by_ball_id": {
                    ball_id: round(statistics.fmean(values), 2)
                    for ball_id, values in sorted(
                        ball_acquisition_progress.items()
                    )
                    if values
                },
            },
            "new_ball_acquisitions_by_ball_id": _counter(acquired_balls),
            "upgrades": {
                "count": sum(upgrade_balls.values()),
                "by_ball_id": _counter(upgrade_balls),
                "by_source_scene": _counter(upgrade_sources),
            },
            "ball_usage": {
                "mapped_shot_count": mapped_shot_count,
                "unmapped_shot_count": unmapped_shot_count,
                "shots_by_ball_id": _counter(ball_shots),
                "shot_share_by_ball_id": ball_shot_shares,
            },
            "relics": {
                "purchase_count": sum(relic_purchases.values()),
                "runs_with_purchase": runs_with_relic_purchase,
                "purchases_by_name": _counter(relic_purchases),
                "effect_triggers_by_name": _counter(
                    relic_effect_triggers
                ),
            },
            "final_deck": {
                "observed_run_count": final_deck_run_count,
                "average_size": _average(
                    final_deck_size_total,
                    final_deck_run_count,
                ),
                "average_upgrade_level": _average(
                    final_upgrade_level_total,
                    final_deck_ball_total,
                ),
                "average_copies_by_ball_id": {
                    ball_id: _average(count, final_deck_run_count)
                    for ball_id, count in sorted(final_ball_copies.items())
                },
            },
        },
        "economy": {
            "stage_money_earned": total_stage_money,
            "extra_money_earned": total_extra_money,
            "total_money_earned": total_stage_money + total_extra_money,
            "relic_money_spent": total_relic_spending,
            "average_money_earned_per_run": _average(
                total_stage_money + total_extra_money,
                monetary_event_run_count,
            ),
            "average_money_spent_per_run": _average(
                total_relic_spending,
                monetary_event_run_count,
            ),
            "average_final_money": _average(
                total_final_money,
                money_observed_run_count,
            ),
            "median_final_money": round(
                float(statistics.median(final_money_values))
                if final_money_values
                else 0.0,
                2,
            ),
            "shop_visit_count": shop_visit_count,
            "shop_purchase_count": shop_purchase_count,
            "shop_visits_without_purchase": shop_visits_without_purchase,
            "shop_purchase_rate_per_visit": _rate(
                shop_purchase_count,
                shop_visit_count,
            ),
            "relic_purchases_per_shop_visit": _rate(
                sum(relic_purchases.values()),
                shop_visit_count,
            ),
            "rest": {
                "heal_count": rest_heal_count,
                "total_hp_recovered": rest_heal_total,
                "wasted_healing": rest_heal_waste_total,
            },
        },
        "routes": {
            "decision_count": route_decision_count,
            "candidate_slots_by_route": _counter(route_candidate_slots),
            "decisions_offering_route": _counter(
                route_offered_decisions
            ),
            "selected_by_route": _counter(route_selected),
            "selection_rate_when_offered": route_selection_rates,
            "controller_counts": _counter(route_controller_counts),
            "invalid_selection_count": invalid_route_selection_count,
        },
        "pockets": {
            "enemy": {
                "controlled_count": pocket_controlled,
                "returned_count": pocket_returned,
                "unreturned_control_count": max(
                    0,
                    pocket_controlled - pocket_returned,
                ),
                "live_finisher_count": pocket_live_finishers,
                "post_defeat_finisher_count": (
                    pocket_post_defeat_finishers
                ),
                "tactical_event_count": tactical_enemy_pocket_count,
                "control_return_rate": _rate(
                    pocket_returned,
                    pocket_controlled,
                ),
                "tactical_events_per_100_shots": round(
                    _rate(tactical_enemy_pocket_count, total_shot_count)
                    * 100.0,
                    2,
                ),
                "by_stage_type": {
                    stage_type: _counter(counts)
                    for stage_type, counts in sorted(
                        pocket_by_stage_type.items()
                    )
                },
                "by_enemy_id": {
                    enemy_id: _counter(counts)
                    for enemy_id, counts in sorted(pocket_by_enemy.items())
                },
            },
            "player": {
                "pocket_count": player_pocket_count,
                "total_damage": player_pocket_damage,
                "average_damage": _average(
                    player_pocket_damage,
                    player_pocket_count,
                ),
                "pockets_per_100_shots": round(
                    _rate(player_pocket_count, total_shot_count) * 100.0,
                    2,
                ),
            },
            "runs_using_tactical_pocket": runs_using_tactical_pocket,
            "tactical_pocket_run_rate": _rate(
                runs_using_tactical_pocket,
                pocket_supported_run_count,
            ),
            "total_shot_count": total_shot_count,
        },
    }
