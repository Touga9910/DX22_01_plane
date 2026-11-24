#include "BalanceLoggerShotSummary.h"

#include <algorithm>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace BalanceLogDetail
{
/// <summary>
/// ショット中に記録されたイベントから、スタックの増減や発動回数などを集計する。
/// </summary>
ShotEventSummary SummarizeShotEvents(const json& pendingShot)
{
	int heavyStacksUncappedGain = 0;
	int heavyStacksGenerated = 0;
	int heavyStacksDiscardedByTurnLimit = 0;
	int heavyStacksDiscardedByStorageLimit = 0;
	int pierceStacksUncappedGain = 0;
	int pierceStacksGenerated = 0;
	int reflectionValidCount = 0;
	int reflectionGeneratingCount = 0;
	int reflectionConsumingCount = 0;
	int reflectionStacksUncappedGain = 0;
	int reflectionStacksGenerated = 0;
	int reflectionStacksConsumed = 0;
	int pinballActivationAttempts = 0;
	int pinballActivations = 0;
	int empoweredHitActivations = 0;
	int chargeInterruptions = 0;
	int protectionRemovals = 0;
	int swarmAttackReduction = 0;
	int brittleAttackReduction = 0;
	int actualAttackPrevention = 0;
	json actualNextTurnPreventionByTarget = json::object();
	json actualFutureChargePreventionByTarget = json::object();
	const auto creditPrevention = [](
		json& byTarget,
		const std::string& targetId,
		int amount,
		const std::string& reason)
	{
		if (targetId.empty() || amount <= 0)
		{
			return;
		}
		const int previous = byTarget.contains(targetId)
			? byTarget[targetId].value("amount", 0)
			: 0;
		if (amount > previous)
		{
			byTarget[targetId] = {
				{ "amount", amount },
				{ "reason", reason },
			};
		}
	};
	int heavyStacksConsumed =
		pendingShot["shot_context"].value(
			"heavy_stacks_consumed_on_start",
			0);
	for (const json& event : pendingShot["shot_events"])
	{
		const std::string eventType =
			event.value("event_type", std::string());
		const json details = event.value("details", json::object());
		if (eventType == "heavy_distance_stacks_generated" ||
			eventType == "heavy_micro_stack_generated")
		{
			const int legacyAmount =
				eventType == "heavy_micro_stack_generated"
				? 1
				: details.value("amount", 0);
			heavyStacksUncappedGain += (std::max)(
				0,
				details.value("uncapped_amount", legacyAmount));
			heavyStacksGenerated += (std::max)(
				0,
				details.value("granted_amount", legacyAmount));
			heavyStacksDiscardedByTurnLimit += (std::max)(
				0,
				details.value("limited_amount", 0));
			heavyStacksDiscardedByStorageLimit += (std::max)(
				0,
				details.value("storage_limited_amount", 0));
		}
		else if (eventType == "pierce_trace_generated")
		{
			const int legacyAmount =
				details.value("stack_generated", false) ? 1 : 0;
			pierceStacksUncappedGain += (std::max)(
				0,
				details.value("requested_amount", legacyAmount));
			pierceStacksGenerated += (std::max)(
				0,
				details.value("granted_amount", legacyAmount));
		}
		else if (eventType == "heavy_collision_boost_armed")
		{
			heavyStacksConsumed += (std::max)(
				0, details.value("consumed", 0));
		}
		else if (eventType == "heavy_finisher_triggered")
		{
			heavyStacksConsumed += (std::max)(
				0, details.value("consumed_collision_count", 0));
		}
		else if (eventType == "cushion_reflection_resolved")
		{
			reflectionValidCount = (std::max)(
				reflectionValidCount,
				details.value("valid_reflections_this_shot", 0));
			reflectionStacksUncappedGain += (std::max)(
				0,
				details.value("uncapped_generated", 0));
			const int generated = (std::max)(
				0,
				details.value("generated", 0));
			const int consumed = (std::max)(
				0,
				details.value("consumed", 0));
			reflectionStacksGenerated += generated;
			reflectionStacksConsumed += consumed;
			if (generated > 0) ++reflectionGeneratingCount;
			if (consumed > 0) ++reflectionConsumingCount;
		}
		else if (eventType == "pinball_activation_attempted")
		{
			++pinballActivationAttempts;
			if (details.value("activated", false))
			{
				++pinballActivations;
				reflectionStacksConsumed += (std::max)(
					0,
					details.value("consumed", 0));
			}
		}
		else if (eventType == "cushion_empowered_hit")
		{
			++empoweredHitActivations;
		}
		else if (eventType == "enemy_charge_interrupted")
		{
			++chargeInterruptions;
			const int amount = (std::max)(
				0, details.value("prevented_attack_damage", 0));
			const std::string targetId = details.value(
				"target_id", std::string());
			if (details.value("planned_action", std::string()) ==
				"charge_attack")
			{
				creditPrevention(
					actualNextTurnPreventionByTarget,
					targetId,
					amount,
					"charge_interruption");
			}
			else
			{
				creditPrevention(
					actualFutureChargePreventionByTarget,
					targetId,
					amount,
					"charge_interruption");
			}
		}
		else if (eventType == "enemy_defeated")
		{
			const std::string targetId = details.value(
				"target_id", std::string());
			creditPrevention(
				actualNextTurnPreventionByTarget,
				targetId,
				(std::max)(0, details.value(
					"prevented_next_turn_damage_before_defense", 0)),
				"defeat");
			creditPrevention(
				actualFutureChargePreventionByTarget,
				targetId,
				(std::max)(0, details.value(
					"prevented_future_charge_damage_before_defense", 0)),
				"defeat");
		}
		else if (eventType == "enemy_board_ability_state_changed")
		{
			const std::string abilityId =
				details.value("ability_id", std::string());
			const int attackReduction = (std::max)(
				0,
				details.value("previous_attack", 0) -
				details.value("current_attack", 0));
			if (abilityId == "protection_resonance" &&
				details.value("reason", std::string()) ==
					"protection_removed")
			{
				++protectionRemovals;
			}
			else if (abilityId == "swarm_amplifier")
			{
				swarmAttackReduction += attackReduction;
				if (details.value("affects_next_turn", false))
				{
					creditPrevention(
						actualNextTurnPreventionByTarget,
						details.value("target_id", std::string()),
						attackReduction,
						"swarm_stage_reduction");
				}
			}
			else if (abilityId == "impact_brittle")
			{
				brittleAttackReduction += attackReduction;
				if (details.value("affects_next_turn", false))
				{
					creditPrevention(
						actualNextTurnPreventionByTarget,
						details.value("target_id", std::string()),
						attackReduction,
						"impact_brittle");
				}
			}
		}
	}
	int actualNextTurnPrevention = 0;
	for (const auto& entry : actualNextTurnPreventionByTarget.items())
	{
		actualNextTurnPrevention += entry.value().value("amount", 0);
	}
	int actualFutureChargePrevention = 0;
	for (const auto& entry : actualFutureChargePreventionByTarget.items())
	{
		actualFutureChargePrevention += entry.value().value("amount", 0);
	}
	actualAttackPrevention =
		actualNextTurnPrevention + actualFutureChargePrevention;
	return {
		heavyStacksUncappedGain,
		heavyStacksGenerated,
		heavyStacksDiscardedByTurnLimit,
		heavyStacksDiscardedByStorageLimit,
		pierceStacksUncappedGain,
		pierceStacksGenerated,
		reflectionValidCount,
		reflectionGeneratingCount,
		reflectionConsumingCount,
		reflectionStacksUncappedGain,
		reflectionStacksGenerated,
		reflectionStacksConsumed,
		pinballActivationAttempts,
		pinballActivations,
		empoweredHitActivations,
		chargeInterruptions,
		protectionRemovals,
		swarmAttackReduction,
		brittleAttackReduction,
		actualAttackPrevention,
		std::move(actualNextTurnPreventionByTarget),
		std::move(actualFutureChargePreventionByTarget),
		heavyStacksConsumed,
		actualNextTurnPrevention,
		actualFutureChargePrevention,
	};
}
}
