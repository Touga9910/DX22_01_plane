#include "BalanceLogger.h"

#include "json/json.hpp"
#include "HeavyCollisionRules.h"
#include "PierceTraceRules.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <unordered_set>

using json = nlohmann::json;

namespace
{
	double RoundToTwoDecimals(double value)
	{
		return std::round(value * 100.0) / 100.0;
	}

	json MakePierceSourceTotalsJson(
		const PierceAttackSourceTotals& totals);

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

BalanceLogger& BalanceLogger::GetInstance()
{
	static BalanceLogger instance;
	return instance;
}

BalanceLogger::BalanceLogger()
	: m_BalanceMetadata(LoadBalanceMetadata())
{
}

void BalanceLogger::BeginRun(
	int playerMaxHp,
	int playerCurrentHp,
	const std::vector<BalanceBallSnapshot>& deck,
	const std::string& controllerType,
	const json& runContext)
{
	if (m_RunActive)
	{
		EndRun(
			"restarted",
			playerCurrentHp,
			playerMaxHp,
			0);
	}

	m_RunStartedMs = GetEpochMilliseconds();
	const std::string runId = MakeRunId();
	const std::string startedAt = MakeUtcTimestamp();
	m_BalanceMetadata = LoadBalanceMetadata();
	m_LogPath =
		std::filesystem::path("logs") /
		(runContext.value("debug_sandbox", false) ? "debug_battle" : "balance") /
		(runId + ".json");

	json deckJson = json::array();
	for (const BalanceBallSnapshot& ball : deck)
	{
		deckJson.push_back(
			{
				{ "id", ball.id },
				{ "instance_id", ball.instanceId },
				{ "upgrade_level", ball.upgradeLevel },
				{ "attack", ball.attack },
				{ "mass", ball.mass },
				{ "radius", ball.radius },
				{ "restitution", ball.restitution },
				{ "friction", ball.friction },
				{
					"abilities",
					{
						{ "split", ball.split },
						{ "pierce", ball.pierce },
						{ "anchor", ball.anchor },
					}
				},
			});
	}

	m_Root =
	{
		{ "schema_version", 4 },
		{ "data_schema_version", 4 },
		{
			"balance_version",
			m_BalanceMetadata.value(
				"balance_version",
				std::string("Legacy/Unknown"))
		},
		{
			"balance_config_hash",
			m_BalanceMetadata.value(
				"balance_config_hash",
				std::string())
		},
		{ "run_id", runId },
		{ "started_at", startedAt },
		{ "controller_type", controllerType },
		{
			"controller",
			{
				{ "type", controllerType },
				{
					"profile",
					runContext.value("controller_profile", std::string())
				},
				{
					"build_profile",
					runContext.value("build_profile", std::string())
				},
				{
					"build_profile_settings_hash",
					runContext.value(
						"build_profile_settings_hash",
						std::string())
				},
			}
		},
		{ "run_context", runContext },
		{
			"measurement",
			{
				{ "measured_at", startedAt },
				{ "mode", controllerType },
				{
					"seeds",
					runContext.value("randomness", json(nullptr))
				},
				{
					"validation",
					runContext.value("validation", json(nullptr))
				},
			}
		},
		{ "configuration", MakeConfigurationSnapshot() },
		{
			"build",
			{
				{ "compiled_date", __DATE__ },
				{ "compiled_time", __TIME__ },
				{
					"git_commit",
					m_BalanceMetadata.value(
						"git_commit",
						std::string("unknown"))
				},
#if defined(_MSC_VER)
				{ "compiler", "msvc" },
				{ "compiler_version", _MSC_VER },
#elif defined(__clang__)
				{ "compiler", "clang" },
				{ "compiler_version", __clang_version__ },
#elif defined(__GNUC__)
				{ "compiler", "gcc" },
				{ "compiler_version", __VERSION__ },
#else
				{ "compiler", "unknown" },
#endif
			}
		},
		{
			"initial_player",
			{
				{ "max_hp", playerMaxHp },
				{ "current_hp", playerCurrentHp },
				{ "deck", deckJson },
			}
		},
		{ "stages", json::array() },
		{ "events", json::array() },
	};

	m_CurrentStageIndex = NoStage;
	m_StageActive = false;
	m_ShotActive = false;
	m_ShotPhysicsTelemetry.Reset();
	m_PierceAttackTelemetry.Reset();
	m_LatestShotPhysicsTelemetry = json(nullptr);
	m_LatestShotPierceAttackTelemetry = json(nullptr);
	m_LatestShotBounceTelemetry = json(nullptr);
	m_RunActive = true;
	m_SavePending = true;

	Save();

	std::cout << "[BalanceLogger] Run log: "
		<< m_LogPath.string() << std::endl;
}

void BalanceLogger::BeginStage(
	const std::string& stageId,
	const std::string& stageType,
	int difficulty,
	int playerCurrentHp,
	int playerMaxHp,
	const std::vector<BalanceEnemySnapshot>& enemies,
	const json& stageContext)
{
	if (!m_RunActive)
	{
		return;
	}

	if (m_StageActive)
	{
		EndStage(
			"abandoned",
			playerCurrentHp,
			playerMaxHp,
			0);
	}

	// Latest-shot MCP data belongs to the previous battle until the next
	// battle begins. Clear it here so a restarted battle cannot expose stale
	// Pierce or physics telemetry before its first shot.
	m_ShotPhysicsTelemetry.Reset();
	m_PierceAttackTelemetry.Reset();
	m_LatestShotPhysicsTelemetry = json(nullptr);
	m_LatestShotPierceAttackTelemetry = json(nullptr);
	m_LatestShotBounceTelemetry = json(nullptr);

	json enemiesJson = json::array();
	for (const BalanceEnemySnapshot& enemy : enemies)
	{
		enemiesJson.push_back(
			{
				{ "id", enemy.id },
				{ "max_hp", enemy.maxHp },
				{ "attack", enemy.attack },
				{ "defense", enemy.defense },
				{ "mass", enemy.mass },
				{ "radius", enemy.radius },
				{ "restitution", enemy.restitution },
				{ "friction", enemy.friction },
				{
					"position",
					{
						enemy.positionX,
						enemy.positionY,
						enemy.positionZ,
					}
				},
			});
	}

	m_StageStartedMs = GetEpochMilliseconds();
	json stage =
	{
		{
			"stage_index",
			static_cast<int>(m_Root["stages"].size()) + 1
		},
		{ "stage_id", stageId },
		{ "stage_type", stageType },
		{ "difficulty", difficulty },
		{ "stage_context", stageContext },
		{ "started_at", MakeUtcTimestamp() },
		{ "elapsed_from_run_start_ms", m_StageStartedMs - m_RunStartedMs },
		{
			"stage_start",
			{
				{ "player_hp", playerCurrentHp },
				{ "player_max_hp", playerMaxHp },
				{ "enemy_count", static_cast<int>(enemies.size()) },
				{ "enemies", enemiesJson },
			}
		},
		{ "shots", json::array() },
		{ "damage_events", json::array() },
	};

	m_Root["stages"].push_back(std::move(stage));
	m_CurrentStageIndex = m_Root["stages"].size() - 1;
	m_StageActive = true;
	m_ShotActive = false;
	m_SavePending = true;

	Save();
}

void BalanceLogger::BeginShot(
	const std::string& ballId,
	int upgradeLevel,
	float power,
	float positionX,
	float positionY,
	float positionZ,
	float velocityX,
	float velocityY,
	float velocityZ,
	int playerHp,
	int enemiesAlive,
	int enemiesDefeatedTotal,
	const std::vector<BalanceShotEnemySnapshot>& enemies,
	const json& shotContext)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || m_ShotActive)
	{
		return;
	}

	// 前回のショット後に記録されたダメージイベントと進行イベントを書き出す。
	// 個々のイベントごとに文書全体を書き直さず、ログの永続性を保つ。
	Save();

	m_ShotStartedMs = GetEpochMilliseconds();
	m_PlayerEnemyHitCount = 0;
	m_EnemyEnemyHitCount = 0;
	m_PlayerEnemyDamage = 0;
	m_EnemyEnemyDamage = 0;
	m_PlayerEnemyDamageObserved = false;
	m_EnemyEnemyDamageObserved = false;
	m_HasCurrentDamageCollision = false;
	m_DefeatedAtShotStart = enemiesDefeatedTotal;
	m_ShotPhysicsTelemetry.BeginShot(enemies);
	m_PierceAttackTelemetry.BeginShot();
	m_BounceSpecialDamageEventCount = 0;
	m_BounceSpecialCalculatedDamage = 0;
	m_BounceSpecialAppliedDamage = 0;
	m_BounceMaximumDamageStage = 0;
	m_BounceTimelineNextEventIndex = 0;
	m_BouncePhysicalReflectionsSoFar = 0;
	m_BouncePlayerReflectionsSoFar = 0;
	m_BouncePinballReflectionsSoFar = 0;
	m_BounceNextTargetIndex = 0;
	m_BounceAttackTimeline = json::array();
	m_BounceTargetIndices.clear();
	m_BouncePendingEmpoweredBonuses.clear();

	m_PendingShot =
	{
		{
			"shot_no",
			static_cast<int>((*stage)["shots"].size()) + 1
		},
		{ "fired_at", MakeUtcTimestamp() },
		{ "elapsed_from_run_start_ms", m_ShotStartedMs - m_RunStartedMs },
		{ "ball_id", ballId },
		{ "upgrade_level", upgradeLevel },
		{ "power", power },
		{ "position", { positionX, positionY, positionZ } },
		{ "velocity", { velocityX, velocityY, velocityZ } },
		{ "player_hp_before", playerHp },
		{ "enemies_alive_before", enemiesAlive },
		{ "shot_context", shotContext },
		{ "enemy_damage_events", json::array() },
		{ "shot_events", json::array() },
		{
			"controller_type",
			m_Root.value("controller_type", "human")
		},
	};

	m_ShotActive = true;
}

void BalanceLogger::RecordEnemyEnemyCollision(
	std::uintptr_t firstEnemyKey,
	const std::string& firstEnemyId,
	std::uintptr_t secondEnemyKey,
	const std::string& secondEnemyId,
	float approachSpeed,
	float positionX,
	float positionZ)
{
	if (!m_ShotActive)
	{
		return;
	}
	RecordDamageCollision(BalanceCollisionType::EnemyEnemy);
	m_ShotPhysicsTelemetry.RecordEnemyEnemyCollision(
		firstEnemyKey,
		firstEnemyId,
		secondEnemyKey,
		secondEnemyId,
		approachSpeed,
		positionX,
		positionZ);
}

void BalanceLogger::RecordEnemyMovement(
	std::uintptr_t enemyKey,
	const std::string& enemyId,
	float distance)
{
	m_ShotPhysicsTelemetry.RecordEnemyMovement(
		enemyKey,
		enemyId,
		distance);
}

void BalanceLogger::RecordPhysicsStep(
	long long processingMicroseconds)
{
	m_ShotPhysicsTelemetry.RecordPhysicsStep(
		processingMicroseconds);
}

void BalanceLogger::RecordWallReflection(
	int wallIndex,
	const std::string& wallGroup,
	const std::string& ballKind,
	float positionX,
	float positionZ,
	float velocityBeforeX,
	float velocityBeforeZ,
	float velocityAfterPhysicsX,
	float velocityAfterPhysicsZ,
	float velocityAfterEffectsX,
	float velocityAfterEffectsZ)
{
	if (!m_ShotActive) return;
	const int timelineEventIndex = m_BounceTimelineNextEventIndex++;
	m_ShotPhysicsTelemetry.RecordWallReflection(
		wallIndex,
		wallGroup,
		ballKind,
		positionX,
		positionZ,
		velocityBeforeX,
		velocityBeforeZ,
		velocityAfterPhysicsX,
		velocityAfterPhysicsZ,
		velocityAfterEffectsX,
		velocityAfterEffectsZ,
		timelineEventIndex);
	++m_BouncePhysicalReflectionsSoFar;
	if (ballKind == "player") ++m_BouncePlayerReflectionsSoFar;
	else if (ballKind == "pinball_target") ++m_BouncePinballReflectionsSoFar;
}

void BalanceLogger::RecordBounceSpecialDamage(
	int damageStage,
	int calculatedDamage,
	int appliedHpDamage)
{
	if (!m_ShotActive || damageStage <= 0) return;
	++m_BounceSpecialDamageEventCount;
	m_BounceSpecialCalculatedDamage += (std::max)(0, calculatedDamage);
	m_BounceSpecialAppliedDamage += (std::max)(0, appliedHpDamage);
	m_BounceMaximumDamageStage = (std::max)(
		m_BounceMaximumDamageStage,
		damageStage);
	if (!m_BounceAttackTimeline.empty())
	{
		auto& attack = m_BounceAttackTimeline.back();
		attack["reflection_derived"] = true;
		attack["reflection_derivation"] = "pinball_damage_stage";
		attack["reflection_damage_stage"] = damageStage;
		attack["reflection_bonus_calculated_damage"] =
			(std::max)(0, calculatedDamage);
		attack["reflection_bonus_applied_hp_damage"] =
			(std::max)(0, appliedHpDamage);
	}
}

void BalanceLogger::RecordPierceMinionSpawn(
	std::uintptr_t minionKey,
	int attack,
	float spawnX,
	float spawnZ,
	float directionX,
	float directionZ,
	float launchSpeed)
{
	m_PierceAttackTelemetry.RecordMinionSpawn(
		minionKey,
		attack,
		spawnX,
		spawnZ,
		directionX,
		directionZ,
		launchSpeed);
}

void BalanceLogger::RecordPierceMinionMovement(
	std::uintptr_t minionKey,
	float distance)
{
	m_PierceAttackTelemetry.RecordMinionMovement(
		minionKey,
		distance);
}

void BalanceLogger::RecordPierceMinionDestroyed(
	std::uintptr_t minionKey,
	const std::string& reason)
{
	m_PierceAttackTelemetry.RecordMinionDestroyed(
		minionKey,
		reason);
}

void BalanceLogger::RecordPierceAttack(
	PierceAttackSource source,
	std::uintptr_t sourceKey,
	std::uintptr_t targetKey,
	int calculatedDamage,
	int appliedHpDamage,
	bool targetWasAlive)
{
	m_PierceAttackTelemetry.RecordAttack(
		source,
		sourceKey,
		targetKey,
		calculatedDamage,
		appliedHpDamage,
		targetWasAlive);
	if (!m_ShotActive) return;
	const auto [targetIt, inserted] = m_BounceTargetIndices.try_emplace(
		targetKey,
		m_BounceNextTargetIndex);
	if (inserted) ++m_BounceNextTargetIndex;
	const auto empowered = m_BouncePendingEmpoweredBonuses.find(targetKey);
	const bool reflectionDerived =
		empowered != m_BouncePendingEmpoweredBonuses.end();
	const int reflectionBonus = reflectionDerived ? empowered->second : 0;
	const int safeCalculatedDamage = (std::max)(0, calculatedDamage);
	const int safeAppliedDamage = (std::max)(0, appliedHpDamage);
	const char* contactSource = !m_HasCurrentDamageCollision
		? "unknown"
		: (m_CurrentDamageCollisionType == BalanceCollisionType::PlayerEnemy
			? "player_enemy_collision"
			: "enemy_enemy_collision");
	m_BounceAttackTimeline.push_back({
		{ "attack_event_index",
			static_cast<int>(m_BounceAttackTimeline.size()) },
		{ "timeline_event_index", m_BounceTimelineNextEventIndex++ },
		{ "source", contactSource },
		{ "telemetry_source", ToString(source) },
		{ "target_index", targetIt->second },
		{ "target_was_alive", targetWasAlive },
		{ "calculated_damage", safeCalculatedDamage },
		{ "applied_hp_damage", safeAppliedDamage },
		{ "overkill_damage", targetWasAlive
			? (std::max)(0, safeCalculatedDamage - safeAppliedDamage)
			: 0 },
		{ "timing_phase", m_BouncePhysicalReflectionsSoFar > 0
			? "after_first_reflection"
			: "before_first_reflection" },
		{ "physical_reflections_before",
			m_BouncePhysicalReflectionsSoFar },
		{ "player_reflections_before", m_BouncePlayerReflectionsSoFar },
		{ "pinball_reflections_before", m_BouncePinballReflectionsSoFar },
		{ "reflection_derived", reflectionDerived },
		{ "reflection_derivation", reflectionDerived
			? json("cushion_empowered_hit")
			: json(nullptr) },
		{ "reflection_damage_stage", nullptr },
		{ "reflection_bonus_calculated_damage", reflectionDerived
			? json(reflectionBonus)
			: json(nullptr) },
		{ "reflection_bonus_applied_hp_damage", nullptr },
	});
	if (reflectionDerived)
		m_BouncePendingEmpoweredBonuses.erase(empowered);
}

void BalanceLogger::RecordDamageCollision(
	BalanceCollisionType collisionType,
	int damageToFirstEnemy,
	int damageToSecondEnemy)
{
	if (!m_ShotActive)
	{
		return;
	}
	m_CurrentDamageCollisionType = collisionType;
	m_HasCurrentDamageCollision = true;

	switch (collisionType)
	{
	case BalanceCollisionType::PlayerEnemy:
		m_PlayerEnemyHitCount++;
		if (damageToFirstEnemy >= 0 || damageToSecondEnemy >= 0)
		{
			m_PlayerEnemyDamageObserved = true;
			m_PlayerEnemyDamage +=
				(std::max)(0, damageToFirstEnemy) +
				(std::max)(0, damageToSecondEnemy);
		}
		break;
	case BalanceCollisionType::EnemyEnemy:
		m_EnemyEnemyHitCount++;
		if (damageToFirstEnemy >= 0 || damageToSecondEnemy >= 0)
		{
			m_EnemyEnemyDamageObserved = true;
			m_EnemyEnemyDamage +=
				(std::max)(0, damageToFirstEnemy) +
				(std::max)(0, damageToSecondEnemy);
		}
		break;
	default:
		break;
	}
}

void BalanceLogger::RecordEnemyDamage(
	const std::string& enemyId,
	int damage)
{
	if (!m_ShotActive || !m_HasCurrentDamageCollision)
	{
		return;
	}

	const int safeDamage = (std::max)(0, damage);
	const char* source = "player_enemy_collision";
	if (m_CurrentDamageCollisionType ==
		BalanceCollisionType::PlayerEnemy)
	{
		m_PlayerEnemyDamageObserved = true;
		m_PlayerEnemyDamage += safeDamage;
	}
	else
	{
		source = "enemy_enemy_collision";
		m_EnemyEnemyDamageObserved = true;
		m_EnemyEnemyDamage += safeDamage;
	}

	m_PendingShot["enemy_damage_events"].push_back(
		{
			{ "source", source },
			{ "enemy_id", enemyId },
			{ "damage", safeDamage },
		});
}

void BalanceLogger::RecordPlayerDamage(
	const std::string& source,
	int damage,
	const std::string& sourceId)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || damage <= 0)
	{
		return;
	}

	json event =
	{
		{ "recorded_at", MakeUtcTimestamp() },
		{
			"elapsed_from_run_start_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{ "source", source },
		{ "damage", damage },
	};
	if (!sourceId.empty())
	{
		event["source_id"] = sourceId;
	}
	(*stage)["damage_events"].push_back(std::move(event));
	m_SavePending = true;
}

void BalanceLogger::RecordEvent(
	const std::string& eventType,
	const json& details)
{
	if (!m_RunActive || eventType.empty())
	{
		return;
	}
	if (m_ShotActive && eventType == "cushion_empowered_hit")
	{
		const std::uintptr_t targetKey = static_cast<std::uintptr_t>(
			details.value("target_id", static_cast<std::uint64_t>(0)));
		if (targetKey != 0)
		{
			m_BouncePendingEmpoweredBonuses[targetKey] = (std::max)(
				0,
				details.value("damage_bonus", 0));
		}
	}

	json event =
	{
		{ "event_type", eventType },
		{ "recorded_at", MakeUtcTimestamp() },
		{
			"elapsed_from_run_start_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{ "details", details },
	};
	if (m_ShotActive && m_PendingShot.is_object())
	{
		m_PendingShot["shot_events"].push_back({
			{ "event_type", eventType },
			{ "details", details },
		});
	}
	if (m_StageActive && m_CurrentStageIndex != NoStage)
	{
		event["stage_index"] =
			static_cast<int>(m_CurrentStageIndex) + 1;
	}
	m_Root["events"].push_back(std::move(event));
	m_SavePending = true;
}

void BalanceLogger::EndShot(
	int playerHp,
	int enemiesAlive,
	int enemiesDefeatedTotal)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr || !m_ShotActive)
	{
		return;
	}

	const long long nowMs = GetEpochMilliseconds();
	const int collisionEffect =
		m_PlayerEnemyHitCount +
		m_EnemyEnemyHitCount * 2;

	m_PendingShot["resolved_at"] = MakeUtcTimestamp();
	m_PendingShot["duration_ms"] = nowMs - m_ShotStartedMs;
	m_PendingShot["player_enemy_hit_count"] =
		m_PlayerEnemyHitCount;
	m_PendingShot["enemy_enemy_hit_count"] =
		m_EnemyEnemyHitCount;
	const bool playerEnemyDamageKnown =
		m_PlayerEnemyHitCount == 0 || m_PlayerEnemyDamageObserved;
	const bool enemyEnemyDamageKnown =
		m_EnemyEnemyHitCount == 0 || m_EnemyEnemyDamageObserved;
	m_PendingShot["player_enemy_damage"] =
		playerEnemyDamageKnown
		? json(m_PlayerEnemyDamage)
		: json(nullptr);
	m_PendingShot["enemy_enemy_damage"] =
		enemyEnemyDamageKnown
		? json(m_EnemyEnemyDamage)
		: json(nullptr);
	m_PendingShot["total_enemy_damage"] =
		playerEnemyDamageKnown && enemyEnemyDamageKnown
		? json(m_PlayerEnemyDamage + m_EnemyEnemyDamage)
		: json(nullptr);
	m_PendingShot["total_damage_collision_count"] =
		m_PlayerEnemyHitCount + m_EnemyEnemyHitCount;
	m_PendingShot["collision_effect"] = collisionEffect;
	m_PendingShot["shot_score"] =
		CalculateShotScore(collisionEffect);
	m_PendingShot["enemies_defeated"] =
		(std::max)(
			0,
			enemiesDefeatedTotal - m_DefeatedAtShotStart);
	m_PendingShot["enemies_alive_after"] = enemiesAlive;
	m_PendingShot["player_hp_after"] = playerHp;
	const ShotPhysicsTelemetry::Snapshot physicsSnapshot =
		m_ShotPhysicsTelemetry.EndShot();
	const PierceAttackTelemetrySnapshot attackSnapshot =
		m_PierceAttackTelemetry.EndShot();
	m_LatestShotPhysicsTelemetry = MakeShotPhysicsTelemetryJson(
		physicsSnapshot);
	m_LatestShotPierceAttackTelemetry = MakePierceAttackTelemetryJson(
		attackSnapshot);
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
		m_PendingShot["shot_context"].value(
			"heavy_stacks_consumed_on_start",
			0);
	for (const json& event : m_PendingShot["shot_events"])
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
	const json mcpTelemetry = m_PendingShot["shot_context"].value(
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
	m_PendingShot["decision_log"] = {
		{ "used_ball", m_PendingShot.value("ball_id", std::string()) },
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
			playerEnemyDamageKnown && enemyEnemyDamageKnown
				? json(m_PlayerEnemyDamage + m_EnemyEnemyDamage)
				: json(nullptr) },
		{ "predicted_kills",
			buildEvaluation.value("predicted_kill_count", 0) },
		{ "actual_kills",
			(std::max)(0, enemiesDefeatedTotal - m_DefeatedAtShotStart) },
		{ "predicted_chain_count",
			buildEvaluation.value("predicted_chain_count", 0) },
		{ "actual_chain_count", m_EnemyEnemyHitCount },
		{ "predicted_attack_prevention",
			buildEvaluation.value("expected_attack_prevention", 0.0) },
		{ "actual_attack_prevention", actualAttackPrevention },
		{ "predicted_next_turn_prevention_before_defense",
			buildEvaluation.value(
				"expected_next_turn_prevention_before_defense", 0.0) },
		{ "actual_next_turn_prevention_before_defense",
			actualNextTurnPrevention },
		{ "actual_next_turn_prevention_by_target",
			actualNextTurnPreventionByTarget },
		{ "predicted_future_charge_prevention",
			buildEvaluation.value(
				"expected_future_charge_prevention", 0.0) },
		{ "actual_future_charge_prevention",
			actualFutureChargePrevention },
		{ "actual_future_charge_prevention_by_target",
			actualFutureChargePreventionByTarget },
		{ "charge_interruption_candidates_generated",
			candidateGeneration.value(
				"charge_interruption_candidates", 0) },
		{ "charge_interruption_candidate_adopted",
			predictedChargeInterruptions.is_array() &&
			!predictedChargeInterruptions.empty() },
		{ "charge_interruption_success", chargeInterruptions > 0 },
		{ "actual_tactical_outcomes", {
			{ "charge_interruptions", chargeInterruptions },
			{ "protection_removals", protectionRemovals },
			{ "swarm_attack_reduction", swarmAttackReduction },
			{ "impact_brittle_attack_reduction", brittleAttackReduction },
		} },
	};
	const int heavyStackGainLimitPerTurn = (std::max)(
		0,
		m_PendingShot["shot_context"].value(
			"heavy_stack_gain_limit_per_turn",
			10));
	const float heavyStackDistancePerStack =
		m_PendingShot["shot_context"].value(
			"heavy_stack_distance_per_stack",
			HeavyCollisionRules::DefaultDistancePerStack);
	m_PendingShot["heavy_stacks_uncapped_gain"] =
		heavyStacksUncappedGain;
	m_PendingShot["heavy_stacks_generated"] = heavyStacksGenerated;
	m_PendingShot["heavy_stack_gain_limit_per_turn"] =
		heavyStackGainLimitPerTurn;
	m_PendingShot["heavy_stack_distance_per_stack"] =
		heavyStackDistancePerStack;
	m_PendingShot["heavy_stacks_discarded_by_turn_limit"] =
		heavyStacksDiscardedByTurnLimit;
	m_PendingShot["heavy_stacks_discarded_by_storage_limit"] =
		heavyStacksDiscardedByStorageLimit;
	m_PendingShot["heavy_stacks_consumed"] = heavyStacksConsumed;
	const int heavyStacksBefore =
		m_PendingShot["shot_context"].value("heavy_stacks_before", 0);
	m_PendingShot["heavy_stacks_before"] = heavyStacksBefore;
	m_PendingShot["heavy_stacks_after"] = (std::max)(
		0,
		heavyStacksBefore + heavyStacksGenerated - heavyStacksConsumed);
	const int pierceStacksBefore = (std::max)(
		0,
		m_PendingShot["shot_context"].value(
			"pierce_stacks_before",
			0));
	const int pierceStacksConsumed = (std::max)(
		0,
		m_PendingShot["shot_context"].value(
			"pierce_stacks_consumed_on_start",
			0));
	const int pierceStacksAfter = std::clamp(
		pierceStacksBefore - pierceStacksConsumed + pierceStacksGenerated,
		0,
		PierceTraceRules::MaxStackCount);
	const int pierceMinionsPlanned = (std::max)(
		0,
		m_PendingShot["shot_context"].value(
			"pierce_minions_planned",
			0));
	const bool pierceMinionActivationAttempted =
		m_PendingShot["shot_context"].value(
			"pierce_minion_activation_attempted",
			false);
	m_PendingShot["pierce_stacks_before"] = pierceStacksBefore;
	m_PendingShot["pierce_stacks_uncapped_gain"] =
		pierceStacksUncappedGain;
	m_PendingShot["pierce_stacks_generated"] = pierceStacksGenerated;
	m_PendingShot["pierce_stacks_consumed"] = pierceStacksConsumed;
	m_PendingShot["pierce_stacks_after"] = pierceStacksAfter;
	m_PendingShot["pierce_stack_max"] = PierceTraceRules::MaxStackCount;
	m_PendingShot["pierce_stack_max_reached"] =
		pierceStacksAfter >= PierceTraceRules::MaxStackCount;
	m_PendingShot["pierce_minion_fizzle_count"] =
		pierceMinionActivationAttempted && pierceMinionsPlanned == 0 ? 1 : 0;
	m_LatestShotPhysicsTelemetry["heavy_stacks_uncapped_gain"] =
		heavyStacksUncappedGain;
	m_LatestShotPhysicsTelemetry["heavy_stacks_generated"] =
		heavyStacksGenerated;
	m_LatestShotPhysicsTelemetry["heavy_stack_gain_limit_per_turn"] =
		heavyStackGainLimitPerTurn;
	m_LatestShotPhysicsTelemetry["heavy_stack_distance_per_stack"] =
		heavyStackDistancePerStack;
	m_LatestShotPhysicsTelemetry["heavy_stacks_discarded_by_turn_limit"] =
		heavyStacksDiscardedByTurnLimit;
	m_LatestShotPhysicsTelemetry["heavy_stacks_discarded_by_storage_limit"] =
		heavyStacksDiscardedByStorageLimit;
	m_LatestShotPierceAttackTelemetry["shot_duration_ms"] =
		nowMs - m_ShotStartedMs;
	m_LatestShotPierceAttackTelemetry["pierce_stacks_before"] =
		pierceStacksBefore;
	m_LatestShotPierceAttackTelemetry["pierce_stacks_uncapped_gain"] =
		pierceStacksUncappedGain;
	m_LatestShotPierceAttackTelemetry["pierce_stacks_generated"] =
		pierceStacksGenerated;
	m_LatestShotPierceAttackTelemetry["pierce_stacks_consumed"] =
		pierceStacksConsumed;
	m_LatestShotPierceAttackTelemetry["pierce_stacks_after"] =
		pierceStacksAfter;
	m_LatestShotPierceAttackTelemetry["pierce_stack_max"] =
		PierceTraceRules::MaxStackCount;
	m_LatestShotPierceAttackTelemetry["pierce_stack_max_reached"] =
		pierceStacksAfter >= PierceTraceRules::MaxStackCount;
	m_LatestShotPierceAttackTelemetry["pierce_minion_fizzle_count"] =
		pierceMinionActivationAttempted && pierceMinionsPlanned == 0 ? 1 : 0;
	const int reflectionStacksBefore = (std::max)(
		0,
		m_PendingShot["shot_context"].value(
			"reflection_stacks_before",
			0));
	const int reflectionStacksAfter = (std::max)(
		0,
		reflectionStacksBefore + reflectionStacksGenerated -
			reflectionStacksConsumed);
	const int pinballFizzleCount =
		pinballActivationAttempts > 0 && pinballActivations == 0 ? 1 : 0;
	m_LatestShotBounceTelemetry = MakeBounceTelemetryJson(
		physicsSnapshot,
		attackSnapshot,
		m_BounceAttackTimeline,
		reflectionValidCount,
		reflectionGeneratingCount,
		reflectionConsumingCount,
		reflectionStacksBefore,
		reflectionStacksUncappedGain,
		reflectionStacksGenerated,
		reflectionStacksConsumed,
		reflectionStacksAfter,
		pinballActivationAttempts,
		pinballActivations,
		pinballFizzleCount,
		empoweredHitActivations,
		m_BounceSpecialDamageEventCount,
		m_BounceSpecialCalculatedDamage,
		m_BounceSpecialAppliedDamage,
		m_BounceMaximumDamageStage,
		(std::max)(0, enemiesDefeatedTotal - m_DefeatedAtShotStart),
		nowMs - m_ShotStartedMs);
	m_PendingShot["reflection_stacks_before"] = reflectionStacksBefore;
	m_PendingShot["reflection_stacks_uncapped_gain"] =
		reflectionStacksUncappedGain;
	m_PendingShot["reflection_stacks_generated"] =
		reflectionStacksGenerated;
	m_PendingShot["reflection_stacks_consumed"] =
		reflectionStacksConsumed;
	m_PendingShot["reflection_stacks_after"] = reflectionStacksAfter;
	m_PendingShot["reflection_stack_max"] = nullptr;
	m_PendingShot["reflection_stacks_discarded_by_max"] = 0;
	m_PendingShot["reflection_pinball_fizzle_count"] = pinballFizzleCount;
	m_PendingShot["physics_chain"] = m_LatestShotPhysicsTelemetry;
	m_PendingShot["pierce_attack"] = m_LatestShotPierceAttackTelemetry;
	m_PendingShot["bounce_axis"] = m_LatestShotBounceTelemetry;

	(*stage)["shots"].push_back(std::move(m_PendingShot));
	m_PendingShot = json{};
	m_ShotActive = false;
	m_HasCurrentDamageCollision = false;
	m_SavePending = true;

	Save();
}

void BalanceLogger::EndStage(
	const std::string& result,
	int playerHp,
	int playerMaxHp,
	int enemiesDefeatedTotal)
{
	json* stage = GetCurrentStage();
	if (stage == nullptr)
	{
		return;
	}

	if (m_ShotActive)
	{
		const int enemyCount =
			(*stage)["stage_start"].value("enemy_count", 0);
		EndShot(
			playerHp,
			(std::max)(0, enemyCount - enemiesDefeatedTotal),
			enemiesDefeatedTotal);
		stage = GetCurrentStage();
		if (stage == nullptr)
		{
			return;
		}
	}

	const json& shots = (*stage)["shots"];
	int noHitShotCount = 0;
	int totalPlayerEnemyHits = 0;
	int totalEnemyEnemyHits = 0;
	int totalPlayerEnemyDamage = 0;
	int totalEnemyEnemyDamage = 0;
	int totalPlayerDamageTaken = 0;
	bool stageEnemyDamageKnown = true;
	double totalShotScore = 0.0;

	for (const json& shot : shots)
	{
		const int collisionEffect =
			shot.value("collision_effect", 0);
		if (collisionEffect == 0)
		{
			noHitShotCount++;
		}

		totalPlayerEnemyHits +=
			shot.value("player_enemy_hit_count", 0);
		totalEnemyEnemyHits +=
			shot.value("enemy_enemy_hit_count", 0);
		if (shot.contains("player_enemy_damage") &&
			shot["player_enemy_damage"].is_number_integer())
		{
			totalPlayerEnemyDamage +=
				shot["player_enemy_damage"].get<int>();
		}
		else
		{
			stageEnemyDamageKnown = false;
		}
		if (shot.contains("enemy_enemy_damage") &&
			shot["enemy_enemy_damage"].is_number_integer())
		{
			totalEnemyEnemyDamage +=
				shot["enemy_enemy_damage"].get<int>();
		}
		else
		{
			stageEnemyDamageKnown = false;
		}
		totalShotScore +=
			shot.value("shot_score", 0.0);
	}
	for (const json& damageEvent : (*stage)["damage_events"])
	{
		totalPlayerDamageTaken +=
			damageEvent.value("damage", 0);
	}

	const int totalShots = static_cast<int>(shots.size());
	const double averageShotScore = totalShots == 0
		? 0.0
		: totalShotScore / static_cast<double>(totalShots);
	const double hpRatio = playerMaxHp <= 0
		? 0.0
		: static_cast<double>(playerHp) /
			static_cast<double>(playerMaxHp);

	(*stage)["stage_result"] =
	{
		{ "result", result },
		{ "ended_at", MakeUtcTimestamp() },
		{
			"duration_ms",
			GetEpochMilliseconds() - m_StageStartedMs
		},
		{ "total_shots", totalShots },
		{ "turn_count", totalShots },
		{ "player_hp", playerHp },
		{ "player_max_hp", playerMaxHp },
		{ "remaining_hp_ratio", RoundToTwoDecimals(hpRatio) },
		{ "enemies_defeated", enemiesDefeatedTotal },
		{ "player_enemy_hit_count", totalPlayerEnemyHits },
		{ "enemy_enemy_hit_count", totalEnemyEnemyHits },
		{
			"player_enemy_damage",
			stageEnemyDamageKnown
				? json(totalPlayerEnemyDamage)
				: json(nullptr)
		},
		{
			"enemy_enemy_damage",
			stageEnemyDamageKnown
				? json(totalEnemyEnemyDamage)
				: json(nullptr)
		},
		{
			"total_enemy_damage",
			stageEnemyDamageKnown
				? json(totalPlayerEnemyDamage + totalEnemyEnemyDamage)
				: json(nullptr)
		},
		{ "player_damage_taken", totalPlayerDamageTaken },
		{ "no_hit_shot_count", noHitShotCount },
		{
			"no_hit_shot_rate",
			totalShots == 0
				? 0.0
				: RoundToTwoDecimals(
					static_cast<double>(noHitShotCount) /
					static_cast<double>(totalShots))
		},
		{
			"average_shot_score",
			RoundToTwoDecimals(averageShotScore)
		},
	};

	m_StageActive = false;
	m_CurrentStageIndex = NoStage;
	m_SavePending = true;
	Save();
}

void BalanceLogger::EndRun(
	const std::string& result,
	int playerHp,
	int playerMaxHp,
	int clearedStageCount)
{
	if (!m_RunActive)
	{
		return;
	}

	if (m_StageActive)
	{
		EndStage(
			"abandoned",
			playerHp,
			playerMaxHp,
			0);
	}

	const double hpRatio = playerMaxHp <= 0
		? 0.0
		: static_cast<double>(playerHp) /
			static_cast<double>(playerMaxHp);

	m_Root["run_result"] =
	{
		{ "result", result },
		{ "ended_at", MakeUtcTimestamp() },
		{
			"duration_ms",
			GetEpochMilliseconds() - m_RunStartedMs
		},
		{
			"reached_stage_index",
			static_cast<int>(m_Root["stages"].size())
		},
		{ "cleared_stage_count", clearedStageCount },
		{ "player_hp", playerHp },
		{ "player_max_hp", playerMaxHp },
		{ "remaining_hp_ratio", RoundToTwoDecimals(hpRatio) },
	};

	m_SavePending = true;
	Save();
	m_RunActive = false;
	m_StageActive = false;
	m_ShotActive = false;
	m_CurrentStageIndex = NoStage;
}

void BalanceLogger::Save()
{
	if (!m_RunActive || m_LogPath.empty() || !m_SavePending)
	{
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(
		m_LogPath.parent_path(),
		error);
	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to create log directory: "
			<< error.message() << std::endl;
		return;
	}

	std::filesystem::path temporaryPath = m_LogPath;
	temporaryPath += ".tmp";

	{
		std::ofstream file(
			temporaryPath,
			std::ios::out | std::ios::trunc);
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to open log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}

		file << std::setw(2) << m_Root << '\n';
		if (!file)
		{
			std::cerr
				<< "[BalanceLogger] Failed to write log file: "
				<< temporaryPath.string() << std::endl;
			return;
		}
	}

	std::filesystem::remove(m_LogPath, error);
	error.clear();
	std::filesystem::rename(
		temporaryPath,
		m_LogPath,
		error);

	if (error)
	{
		std::cerr
			<< "[BalanceLogger] Failed to replace log file: "
			<< error.message() << std::endl;
		return;
	}

	m_SavePending = false;
}

json* BalanceLogger::GetCurrentStage()
{
	if (!m_RunActive ||
		!m_StageActive ||
		m_CurrentStageIndex == NoStage ||
		!m_Root.contains("stages") ||
		m_CurrentStageIndex >= m_Root["stages"].size())
	{
		return nullptr;
	}

	return &m_Root["stages"][m_CurrentStageIndex];
}

long long BalanceLogger::GetEpochMilliseconds()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string BalanceLogger::MakeUtcTimestamp()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);

	std::tm utcTime{};
#if defined(_WIN32)
	gmtime_s(&utcTime, &nowTime);
#else
	gmtime_r(&nowTime, &utcTime);
#endif

	std::ostringstream stream;
	stream << std::put_time(
		&utcTime,
		"%Y-%m-%dT%H:%M:%SZ");
	return stream.str();
}

std::string BalanceLogger::MakeRunId()
{
	const auto now = std::chrono::system_clock::now();
	const std::time_t nowTime =
		std::chrono::system_clock::to_time_t(now);
	const long long milliseconds =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()).count() % 1000;

	std::tm localTime{};
#if defined(_WIN32)
	localtime_s(&localTime, &nowTime);
#else
	localtime_r(&nowTime, &localTime);
#endif

	std::ostringstream stream;
	stream
		<< "run_"
		<< std::put_time(&localTime, "%Y%m%d_%H%M%S")
		<< '_'
		<< std::setw(3)
		<< std::setfill('0')
		<< milliseconds;
	return stream.str();
}

double BalanceLogger::CalculateShotScore(int collisionEffect)
{
	const int safeCollisionEffect =
		(std::max)(0, collisionEffect);
	const double score =
		100.0 *
		(1.0 - std::exp(
			-static_cast<double>(safeCollisionEffect) / 2.0));
	return RoundToTwoDecimals(score);
}

json BalanceLogger::MakeConfigurationSnapshot()
{
	json paths = json::array();
	std::ifstream registry("assets/data/balance_version.json");
	if (registry)
	{
		try
		{
			json value;
			registry >> value;
			if (value.contains("managed_files") &&
				value["managed_files"].is_array())
			{
				paths = value["managed_files"];
			}
		}
		catch (const json::exception&)
		{
			paths = json::array();
		}
	}
	if (paths.empty())
	{
		paths = {
			"assets/data/stage_01.json",
			"assets/data/enemy_data.json",
			"assets/data/player_status.json",
			"assets/data/player_deck.json",
			"assets/data/dynamic_balance.json",
			"assets/data/difficulty_profiles.json",
			"assets/data/encounter_balance.json",
			"assets/data/pocket_rules.json",
		};
	}

	json files = json::array();
	for (const json& value : paths)
	{
		if (!value.is_string())
		{
			continue;
		}
		const std::filesystem::path path = value.get<std::string>();
		files.push_back(MakeFileFingerprint(path));
	}
	return { { "files", std::move(files) } };
}

json BalanceLogger::LoadBalanceMetadata()
{
	json result = {
		{ "balance_version", "Legacy/Unknown" },
		{ "balance_config_hash", "" },
		{ "git_commit", "unknown" },
	};
	std::ifstream source("assets/data/balance_version.json");
	if (!source)
	{
		return result;
	}
	try
	{
		json registry;
		source >> registry;
		if (registry.contains("current_version") &&
			registry["current_version"].is_string())
		{
			result["balance_version"] = registry["current_version"];
		}
		if (registry.contains("current_config_hash") &&
			registry["current_config_hash"].is_string())
		{
			result["balance_config_hash"] =
				registry["current_config_hash"];
		}
		if (registry.contains("registered_git_commit") &&
			registry["registered_git_commit"].is_string())
		{
			result["git_commit"] = registry["registered_git_commit"];
		}
	}
	catch (const json::exception&)
	{
		return result;
	}
	return result;
}

json BalanceLogger::MakeFileFingerprint(
	const std::filesystem::path& path)
{
	json result =
	{
		{ "path", path.generic_string() },
		{ "exists", false },
	};

	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		return result;
	}

	constexpr std::uint64_t fnvOffset = 14695981039346656037ull;
	constexpr std::uint64_t fnvPrime = 1099511628211ull;
	std::uint64_t hash = fnvOffset;
	std::uint64_t size = 0;
	char buffer[4096];
	while (file)
	{
		file.read(buffer, sizeof(buffer));
		const std::streamsize count = file.gcount();
		for (std::streamsize index = 0; index < count; index++)
		{
			hash ^= static_cast<unsigned char>(buffer[index]);
			hash *= fnvPrime;
		}
		size += static_cast<std::uint64_t>(count);
	}

	std::ostringstream hashStream;
	hashStream << std::hex << std::setw(16) << std::setfill('0') << hash;
	result["exists"] = true;
	result["size_bytes"] = size;
	result["fnv1a64"] = hashStream.str();
	return result;
}
