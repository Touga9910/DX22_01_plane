#include "BalanceLoggerTelemetryJson.h"
#include "json/json.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

namespace BalanceLogDetail
{
	/// <summary>
	/// 数値を小数第2位に丸める。
	/// </summary>
	double RoundToTwoDecimals(double value)
	{
		return std::round(value * 100.0) / 100.0;
	}

namespace
{
	/// <summary>
	/// 貫通攻撃の発生源別の回数やダメージ合計をJSONに変換する。
	/// </summary>
	json MakePierceSourceTotalsJson(
		const PierceAttackSourceTotals& totals)
	{
		return {
			{ "attack_check_count", totals.attackCheckCount },
			{ "hit_count", totals.hitCount },
			{ "effective_attack_count", totals.effectiveAttackCount },
			{ "calculated_damage", totals.calculatedDamage },
			{ "applied_hp_damage", totals.appliedHpDamage },
			{ "overkill_damage", totals.overkillDamage },
		};
	}
}

	/// <summary>
	/// 敵の移動、物理処理時間、衝突などのショット計測データをJSONに変換する。
	/// </summary>
	json MakeShotPhysicsTelemetryJson(
		const ShotPhysicsTelemetry::Snapshot& telemetry)
	{
		json enemyDistances = json::array();
		for (const auto& item : telemetry.enemyDistances)
		{
			enemyDistances.push_back({
				{ "enemy_index", item.enemy.stableIndex },
				{ "enemy_id", item.enemy.enemyId },
				{ "initial_hp", item.enemy.hp },
				{ "max_hp", item.enemy.maxHp },
				{ "initial_position", {
					item.enemy.positionX,
					item.enemy.positionZ,
				} },
				{ "distance_xz", item.distance },
			});
		}

		json collisionEvents = json::array();
		for (std::size_t index = 0;
			index < telemetry.collisions.size();
			++index)
		{
			const auto& event = telemetry.collisions[index];
			collisionEvents.push_back({
				{ "event_index", index },
				{ "first_enemy_index", event.firstEnemyIndex },
				{ "second_enemy_index", event.secondEnemyIndex },
				{ "approach_speed", event.approachSpeed },
				{ "position_xz", {
					event.positionX,
					event.positionZ,
				} },
			});
		}

		json pairSummaries = json::array();
		for (const auto& pair : telemetry.pairSummaries)
		{
			pairSummaries.push_back({
				{ "first_enemy_index", pair.firstEnemyIndex },
				{ "second_enemy_index", pair.secondEnemyIndex },
				{ "collision_count", pair.collisionCount },
			});
		}

		json wallReflections = json::array();
		for (std::size_t index = 0;
			index < telemetry.wallReflections.size();
			++index)
		{
			const auto& event = telemetry.wallReflections[index];
			const double speedBefore = std::hypot(
				event.velocityBeforeX,
				event.velocityBeforeZ);
			const double speedAfterPhysics = std::hypot(
				event.velocityAfterPhysicsX,
				event.velocityAfterPhysicsZ);
			const double speedAfterEffects = std::hypot(
				event.velocityAfterEffectsX,
				event.velocityAfterEffectsZ);
			wallReflections.push_back({
				{ "event_index", index },
				{ "timeline_event_index", event.timelineEventIndex },
				{ "wall_index", event.wallIndex },
				{ "wall_group", event.wallGroup },
				{ "ball_kind", event.ballKind },
				{ "position_xz", { event.positionX, event.positionZ } },
				{ "velocity_before_xz", {
					event.velocityBeforeX,
					event.velocityBeforeZ } },
				{ "velocity_after_physics_xz", {
					event.velocityAfterPhysicsX,
					event.velocityAfterPhysicsZ } },
				{ "velocity_after_effects_xz", {
					event.velocityAfterEffectsX,
					event.velocityAfterEffectsZ } },
				{ "speed_before", speedBefore },
				{ "speed_after_physics", speedAfterPhysics },
				{ "speed_after_effects", speedAfterEffects },
				{ "physics_speed_retention", speedBefore > 0.000001
					? speedAfterPhysics / speedBefore : 0.0 },
				{ "final_speed_retention", speedBefore > 0.000001
					? speedAfterEffects / speedBefore : 0.0 },
			});
		}

		return {
			{ "enabled", true },
			{ "effective_collision_definition",
				"A new approaching enemy-enemy contact episode accepted by BallPhysicsRules::Pair. Persistent contact is counted once; separation followed by re-contact is counted again." },
			{ "effective_enemy_enemy_collision_count",
				static_cast<int>(telemetry.collisions.size()) },
			{ "involved_enemy_count", telemetry.involvedEnemyCount },
			{ "unique_enemy_pair_count",
				static_cast<int>(telemetry.pairSummaries.size()) },
			{ "maximum_collisions_for_one_pair",
				telemetry.maximumCollisionsForOnePair },
			{ "enemy_distances", std::move(enemyDistances) },
			{ "total_enemy_distance_xz", telemetry.totalEnemyDistance },
			{ "maximum_enemy_distance_xz", telemetry.maximumEnemyDistance },
			{ "collision_events", std::move(collisionEvents) },
			{ "pair_summaries", std::move(pairSummaries) },
			{ "wall_reflections", std::move(wallReflections) },
			{ "physics_processing_microseconds",
				telemetry.physicsProcessingMicroseconds },
			{ "physics_step_count", telemetry.physicsStepCount },
		};
	}

	/// <summary>
	/// 球種ごとの壁反射と反射に伴うダメージの計測をJSONにまとめる。
	/// </summary>
	json MakeBounceTelemetryJson(
		const ShotPhysicsTelemetry::Snapshot& physics,
		const PierceAttackTelemetrySnapshot& attacks,
		const json& attackTimeline,
		int validReflections,
		int stackGeneratingReflections,
		int stackConsumingReflections,
		int stacksBefore,
		int uncappedGain,
		int actualGain,
		int consumed,
		int stacksAfter,
		int pinballAttempts,
		int pinballActivations,
		int pinballFizzleCount,
		int empoweredHitActivations,
		int specialDamageEventCount,
		int specialCalculatedDamage,
		int specialAppliedDamage,
		int maximumDamageStage,
		int enemiesDefeated,
		long long shotDurationMs)
	{
		int playerReflections = 0;
		int pinballReflections = 0;
		for (const auto& event : physics.wallReflections)
		{
			if (event.ballKind == "player") ++playerReflections;
			else if (event.ballKind == "pinball_target") ++pinballReflections;
		}
		const auto makeAttackPhaseSummary = [&attackTimeline](
			const char* phase)
		{
			int attackChecks = 0;
			int hits = 0;
			int effectiveAttacks = 0;
			int calculatedDamage = 0;
			int appliedDamage = 0;
			int overkillDamage = 0;
			std::unordered_set<int> uniqueEffectiveTargets;
			for (const auto& event : attackTimeline)
			{
				if (event.value("timing_phase", "") != phase) continue;
				++attackChecks;
				if (event.value("target_was_alive", false)) ++hits;
				const int applied = (std::max)(
					0,
					event.value("applied_hp_damage", 0));
				if (applied > 0)
				{
					++effectiveAttacks;
					uniqueEffectiveTargets.insert(
						event.value("target_index", -1));
				}
				calculatedDamage += (std::max)(
					0,
					event.value("calculated_damage", 0));
				appliedDamage += applied;
				overkillDamage += (std::max)(
					0,
					event.value("overkill_damage", 0));
			}
			uniqueEffectiveTargets.erase(-1);
			return json{
				{ "attack_check_count", attackChecks },
				{ "hit_count", hits },
				{ "effective_attack_count", effectiveAttacks },
				{ "calculated_damage", calculatedDamage },
				{ "applied_hp_damage", appliedDamage },
				{ "overkill_damage", overkillDamage },
				{ "unique_effective_target_count",
					static_cast<int>(uniqueEffectiveTargets.size()) },
			};
		};
		const json beforeFirstReflection =
			makeAttackPhaseSummary("before_first_reflection");
		const json afterFirstReflection =
			makeAttackPhaseSummary("after_first_reflection");
		return {
			{ "enabled", true },
			{ "measurement_scope", "reflection_axis_diagnostics" },
			{ "attacks_measurement_scope",
				"tracked_contact_attack_sources_only" },
			{ "enemies_defeated_measurement_scope", "all_sources_in_shot" },
			{ "is_generic_combat_total", false },
			{ "physical_reflection_definition",
				"A BallPhysicsRules::Wall call that accepted an approaching contact and changed velocity. Persistent contact without a new accepted bounce is not counted." },
			{ "effective_attack_definition",
				"A contact attack that actually reduced enemy HP." },
			{ "physical_reflection_count",
				static_cast<int>(physics.wallReflections.size()) },
			{ "player_physical_reflection_count", playerReflections },
			{ "pinball_target_physical_reflection_count", pinballReflections },
			{ "effective_reflection_count", validReflections },
			{ "stack_generating_reflection_count", stackGeneratingReflections },
			{ "stack_consuming_reflection_count", stackConsumingReflections },
			{ "attacks", MakePierceSourceTotalsJson(attacks.total) },
			{ "attack_timing_classification",
				"Exclusive by the first accepted player or pinball-target wall reflection. Reflection-derived is an independent attribute and does not add another attack count." },
			{ "attack_timing", {
				{ "before_first_reflection", beforeFirstReflection },
				{ "after_first_reflection", afterFirstReflection },
			} },
			{ "attack_timeline", attackTimeline },
			{ "post_reflection_effective_attack_succeeded",
				afterFirstReflection.value("effective_attack_count", 0) > 0 },
			{ "unique_effective_target_count", attacks.uniqueEffectiveTargetCount },
			{ "special_effect_activation_count",
				empoweredHitActivations + pinballActivations +
					specialDamageEventCount },
			{ "pinball_activation_attempt_count", pinballAttempts },
			{ "pinball_activation_count", pinballActivations },
			{ "pinball_fizzle_count", pinballFizzleCount },
			{ "empowered_hit_activation_count", empoweredHitActivations },
			{ "pinball_damage_event_count", specialDamageEventCount },
			{ "pinball_maximum_damage_stage", maximumDamageStage },
			{ "pinball_calculated_bonus_damage", specialCalculatedDamage },
			{ "pinball_applied_bonus_damage", specialAppliedDamage },
			{ "enemies_defeated", enemiesDefeated },
			{ "reflection_stacks_before", stacksBefore },
			{ "reflection_stacks_uncapped_gain", uncappedGain },
			{ "reflection_stacks_generated", actualGain },
			{ "reflection_stacks_consumed", consumed },
			{ "reflection_stacks_after", stacksAfter },
			{ "reflection_stack_max", nullptr },
			{ "reflection_stacks_discarded_by_max", 0 },
			{ "shot_duration_ms", shotDurationMs },
			{ "physics_processing_microseconds",
				physics.physicsProcessingMicroseconds },
			{ "physics_step_count", physics.physicsStepCount },
			{ "average_physics_processing_microseconds_per_step",
				physics.physicsStepCount > 0
					? static_cast<double>(physics.physicsProcessingMicroseconds) /
						static_cast<double>(physics.physicsStepCount)
					: 0.0 },
		};
	}

	/// <summary>
	/// 貫通ミニオンの履歴と攻撃の件数およびダメージの計測をJSONにまとめる。
	/// </summary>
	json MakePierceAttackTelemetryJson(
		const PierceAttackTelemetrySnapshot& telemetry)
	{
		json minions = json::array();
		for (const auto& minion : telemetry.minions)
		{
			minions.push_back({
				{ "minion_id", minion.ordinal },
				{ "attack", minion.attack },
				{ "spawn_position_xz", {
					minion.spawnX,
					minion.spawnZ,
				} },
				{ "launch_direction_xz", {
					minion.directionX,
					minion.directionZ,
				} },
				{ "launch_speed", minion.launchSpeed },
				{ "movement_distance_xz", minion.movementDistance },
				{ "lifetime_microseconds", minion.lifetimeMicroseconds },
				{ "destruction_reason", minion.destructionReason },
				{ "attack_check_count", minion.attackCheckCount },
				{ "hit_count", minion.hitCount },
				{ "effective_attack_count", minion.effectiveAttackCount },
				{ "calculated_damage", minion.calculatedDamage },
				{ "applied_hp_damage", minion.appliedHpDamage },
				{ "overkill_damage", minion.overkillDamage },
				{ "unique_target_count", minion.uniqueTargetCount },
			});
		}

		return {
			{ "enabled", true },
			{ "measurement_scope", "pierce_attack_telemetry_sources_only" },
			{ "is_generic_combat_total", false },
			{ "classification",
				"Exclusive source categories. A player contact that consumes a pierce use is player_pierce, not also player_direct." },
			{ "effective_attack_definition",
				"A contact attack that actually reduced enemy HP." },
			{ "sources", {
				{ "player_direct", MakePierceSourceTotalsJson(telemetry.playerDirect) },
				{ "player_pierce", MakePierceSourceTotalsJson(telemetry.playerPierce) },
				{ "minion", MakePierceSourceTotalsJson(telemetry.minion) },
				{ "trace", MakePierceSourceTotalsJson(telemetry.trace) },
				{ "other", MakePierceSourceTotalsJson(telemetry.other) },
			} },
			{ "total", MakePierceSourceTotalsJson(telemetry.total) },
			{ "unique_effective_target_count",
				telemetry.uniqueEffectiveTargetCount },
			{ "minions_generated", telemetry.minionsGenerated },
			{ "maximum_simultaneous_minions",
				telemetry.maximumSimultaneousMinions },
			{ "minions_without_effective_attack",
				telemetry.minionsWithoutEffectiveAttack },
			{ "minions_with_multiple_targets",
				telemetry.minionsWithMultipleTargets },
			{ "minions", std::move(minions) },
		};
	}
}
