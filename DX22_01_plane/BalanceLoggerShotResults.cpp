#include "BalanceLoggerShotSummary.h"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"

#include <algorithm>
#include <string>

using json = nlohmann::json;

namespace BalanceLogDetail
{
void AppendShotDecisionLog(
	json& pendingShot,
	const ShotEventSummary& summary,
	const ShotCompletionFacts& facts)
{
	const json mcpTelemetry = pendingShot["shot_context"].value(
		"mcp_telemetry", json::object());
	const json buildEvaluation = mcpTelemetry.value(
		"build_evaluation", json::object());
	const json candidateGeneration = buildEvaluation.value(
		"candidate_generation", json::object());
	const json predictedTacticalOutcomes = buildEvaluation.value(
		"tactical_outcomes", json::object());
	const json predictedChargeInterruptions =
		predictedTacticalOutcomes.value(
			"charge_interruptions", json::array());
	pendingShot["decision_log"] = {
		{ "used_ball", pendingShot.value("ball_id", std::string()) },
		{ "used_ball_upgrade_level",
			pendingShot.value("upgrade_level", 0) },
		{ "mcp_shot_policy",
			mcpTelemetry.value("mcp_shot_policy", std::string("standard")) },
		{ "candidate_comparison",
			mcpTelemetry.value("candidate_comparison", json::object()) },
		{ "target_id", buildEvaluation.value("target_id", std::string()) },
		{ "shot_path", buildEvaluation.value("shot_path", json::object()) },
		{ "candidate_score", buildEvaluation.value("score", 0.0) },
		{ "score_breakdown",
			buildEvaluation.value("score_breakdown", json::object()) },
		{ "decision_reasons",
			buildEvaluation.value("decision_reasons", json::array()) },
		{ "expected_damage",
			buildEvaluation.value("metrics", json::object()).value(
				"damage", 0.0) },
		{ "actual_damage",
			facts.playerEnemyDamageKnown && facts.enemyEnemyDamageKnown
				? json(facts.playerEnemyDamage + facts.enemyEnemyDamage)
				: json(nullptr) },
		{ "predicted_kills",
			buildEvaluation.value("predicted_kill_count", 0) },
		{ "actual_kills",
			(std::max)(0, facts.enemiesDefeatedTotal - facts.defeatedAtShotStart) },
		{ "predicted_chain_count",
			buildEvaluation.value("predicted_chain_count", 0) },
		{ "actual_chain_count", facts.enemyEnemyHitCount },
		{ "predicted_special_effects",
			buildEvaluation.value(
				"special_ability_estimates", json::object()) },
		{ "actual_special_effects", {
			{ "heavy_stacks_generated", summary.heavyStacksGenerated },
			{ "pierce_stacks_generated", summary.pierceStacksGenerated },
			{ "reflection_stacks_generated", summary.reflectionStacksGenerated },
			{ "reflection_stacks_consumed", summary.reflectionStacksConsumed },
			{ "pinball_activations", summary.pinballActivations },
			{ "cushion_empowered_hits", summary.empoweredHitActivations },
			{ "charge_interruptions", summary.chargeInterruptions },
			{ "protection_removals", summary.protectionRemovals },
			{ "swarm_attack_reduction", summary.swarmAttackReduction },
			{ "impact_brittle_attack_reduction", summary.brittleAttackReduction },
		} },
		{ "enemy_count_after", facts.enemiesAlive },
		{ "predicted_attack_prevention",
			buildEvaluation.value("expected_attack_prevention", 0.0) },
		{ "actual_attack_prevention", summary.actualAttackPrevention },
		{ "predicted_next_turn_prevention_before_defense",
			buildEvaluation.value(
				"expected_next_turn_prevention_before_defense", 0.0) },
		{ "actual_next_turn_prevention_before_defense",
			summary.actualNextTurnPrevention },
		{ "actual_next_turn_prevention_by_target",
			summary.actualNextTurnPreventionByTarget },
		{ "predicted_future_charge_prevention",
			buildEvaluation.value(
				"expected_future_charge_prevention", 0.0) },
		{ "actual_future_charge_prevention",
			summary.actualFutureChargePrevention },
		{ "actual_future_charge_prevention_by_target",
			summary.actualFutureChargePreventionByTarget },
		{ "charge_interruption_candidates_generated",
			candidateGeneration.value(
				"charge_interruption_candidates", 0) },
		{ "charge_interruption_candidate_adopted",
			predictedChargeInterruptions.is_array() &&
			!predictedChargeInterruptions.empty() },
		{ "charge_interruption_success", summary.chargeInterruptions > 0 },
		{ "actual_tactical_outcomes", {
			{ "charge_interruptions", summary.chargeInterruptions },
			{ "protection_removals", summary.protectionRemovals },
			{ "swarm_attack_reduction", summary.swarmAttackReduction },
			{ "impact_brittle_attack_reduction", summary.brittleAttackReduction },
		} },
	};
}

ReflectionStackSummary AppendShotStackTelemetry(
	json& pendingShot,
	json& latestPhysics,
	json& latestPierce,
	const ShotEventSummary& summary,
	long long durationMs)
{
	const int heavyStackGainLimitPerTurn = (std::max)(
		0,
		pendingShot["shot_context"].value(
			"heavy_stack_gain_limit_per_turn",
			10));
	const float heavyStackDistancePerStack =
		pendingShot["shot_context"].value(
			"heavy_stack_distance_per_stack",
			HeavyCollisionRules::DefaultDistancePerStack);
	pendingShot["heavy_stacks_uncapped_gain"] =
		summary.heavyStacksUncappedGain;
	pendingShot["heavy_stacks_generated"] = summary.heavyStacksGenerated;
	pendingShot["heavy_stack_gain_limit_per_turn"] =
		heavyStackGainLimitPerTurn;
	pendingShot["heavy_stack_distance_per_stack"] =
		heavyStackDistancePerStack;
	pendingShot["heavy_stacks_discarded_by_turn_limit"] =
		summary.heavyStacksDiscardedByTurnLimit;
	pendingShot["heavy_stacks_discarded_by_storage_limit"] =
		summary.heavyStacksDiscardedByStorageLimit;
	pendingShot["heavy_stacks_consumed"] = summary.heavyStacksConsumed;
	const int heavyStacksBefore =
		pendingShot["shot_context"].value("heavy_stacks_before", 0);
	pendingShot["heavy_stacks_before"] = heavyStacksBefore;
	pendingShot["heavy_stacks_after"] = (std::max)(
		0,
		heavyStacksBefore + summary.heavyStacksGenerated - summary.heavyStacksConsumed);
	const int pierceStacksBefore = (std::max)(
		0,
		pendingShot["shot_context"].value(
			"pierce_stacks_before",
			0));
	const int pierceStacksConsumed = (std::max)(
		0,
		pendingShot["shot_context"].value(
			"pierce_stacks_consumed_on_start",
			0));
	const int pierceStacksAfter = std::clamp(
		pierceStacksBefore - pierceStacksConsumed + summary.pierceStacksGenerated,
		0,
		PierceTraceRules::MaxStackCount);
	const int pierceMinionsPlanned = (std::max)(
		0,
		pendingShot["shot_context"].value(
			"pierce_minions_planned",
			0));
	const bool pierceMinionActivationAttempted =
		pendingShot["shot_context"].value(
			"pierce_minion_activation_attempted",
			false);
	pendingShot["pierce_stacks_before"] = pierceStacksBefore;
	pendingShot["pierce_stacks_uncapped_gain"] =
		summary.pierceStacksUncappedGain;
	pendingShot["pierce_stacks_generated"] = summary.pierceStacksGenerated;
	pendingShot["pierce_stacks_consumed"] = pierceStacksConsumed;
	pendingShot["pierce_stacks_after"] = pierceStacksAfter;
	pendingShot["pierce_stack_max"] = PierceTraceRules::MaxStackCount;
	pendingShot["pierce_stack_max_reached"] =
		pierceStacksAfter >= PierceTraceRules::MaxStackCount;
	pendingShot["pierce_minion_fizzle_count"] =
		pierceMinionActivationAttempted && pierceMinionsPlanned == 0 ? 1 : 0;
	latestPhysics["heavy_stacks_uncapped_gain"] =
		summary.heavyStacksUncappedGain;
	latestPhysics["heavy_stacks_generated"] =
		summary.heavyStacksGenerated;
	latestPhysics["heavy_stack_gain_limit_per_turn"] =
		heavyStackGainLimitPerTurn;
	latestPhysics["heavy_stack_distance_per_stack"] =
		heavyStackDistancePerStack;
	latestPhysics["heavy_stacks_discarded_by_turn_limit"] =
		summary.heavyStacksDiscardedByTurnLimit;
	latestPhysics["heavy_stacks_discarded_by_storage_limit"] =
		summary.heavyStacksDiscardedByStorageLimit;
	latestPierce["shot_duration_ms"] =
		durationMs;
	latestPierce["pierce_stacks_before"] =
		pierceStacksBefore;
	latestPierce["pierce_stacks_uncapped_gain"] =
		summary.pierceStacksUncappedGain;
	latestPierce["pierce_stacks_generated"] =
		summary.pierceStacksGenerated;
	latestPierce["pierce_stacks_consumed"] =
		pierceStacksConsumed;
	latestPierce["pierce_stacks_after"] =
		pierceStacksAfter;
	latestPierce["pierce_stack_max"] =
		PierceTraceRules::MaxStackCount;
	latestPierce["pierce_stack_max_reached"] =
		pierceStacksAfter >= PierceTraceRules::MaxStackCount;
	latestPierce["pierce_minion_fizzle_count"] =
		pierceMinionActivationAttempted && pierceMinionsPlanned == 0 ? 1 : 0;
	const int reflectionStacksBefore = (std::max)(
		0,
		pendingShot["shot_context"].value(
			"reflection_stacks_before",
			0));
	const int reflectionStacksAfter = (std::max)(
		0,
		reflectionStacksBefore + summary.reflectionStacksGenerated -
			summary.reflectionStacksConsumed);
	const int pinballFizzleCount =
		summary.pinballActivationAttempts > 0 && summary.pinballActivations == 0 ? 1 : 0;
	return { reflectionStacksBefore, reflectionStacksAfter, pinballFizzleCount };
}
}
