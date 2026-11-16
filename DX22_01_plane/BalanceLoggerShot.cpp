#include "BalanceLogger.h"
#include "BalanceLoggerShotSummary.h"
#include "BalanceLoggerTelemetryJson.h"

#include <algorithm>
#include <cmath>
#include <utility>

using json = nlohmann::json;
using namespace BalanceLogDetail;

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

void BalanceLogger::EndShot(
	int playerHp,
	int enemiesAlive,
	int enemiesDefeatedTotal)
{
	using namespace BalanceLogDetail;

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
	const ShotEventSummary summary = SummarizeShotEvents(m_PendingShot);
	const ShotCompletionFacts facts = {
		playerEnemyDamageKnown,
		enemyEnemyDamageKnown,
		m_PlayerEnemyDamage,
		m_EnemyEnemyDamage,
		m_EnemyEnemyHitCount,
		enemiesAlive,
		enemiesDefeatedTotal,
		m_DefeatedAtShotStart,
	};
	AppendShotDecisionLog(m_PendingShot, summary, facts);
	const ReflectionStackSummary reflection = AppendShotStackTelemetry(
		m_PendingShot,
		m_LatestShotPhysicsTelemetry,
		m_LatestShotPierceAttackTelemetry,
		summary,
		nowMs - m_ShotStartedMs);
	m_LatestShotBounceTelemetry = MakeBounceTelemetryJson(
		physicsSnapshot,
		attackSnapshot,
		m_BounceAttackTimeline,
		summary.reflectionValidCount,
		summary.reflectionGeneratingCount,
		summary.reflectionConsumingCount,
		reflection.before,
		summary.reflectionStacksUncappedGain,
		summary.reflectionStacksGenerated,
		summary.reflectionStacksConsumed,
		reflection.after,
		summary.pinballActivationAttempts,
		summary.pinballActivations,
		reflection.pinballFizzleCount,
		summary.empoweredHitActivations,
		m_BounceSpecialDamageEventCount,
		m_BounceSpecialCalculatedDamage,
		m_BounceSpecialAppliedDamage,
		m_BounceMaximumDamageStage,
		(std::max)(0, enemiesDefeatedTotal - m_DefeatedAtShotStart),
		nowMs - m_ShotStartedMs);
	m_PendingShot["reflection_stacks_before"] = reflection.before;
	m_PendingShot["reflection_stacks_uncapped_gain"] =
		summary.reflectionStacksUncappedGain;
	m_PendingShot["reflection_stacks_generated"] =
		summary.reflectionStacksGenerated;
	m_PendingShot["reflection_stacks_consumed"] =
		summary.reflectionStacksConsumed;
	m_PendingShot["reflection_stacks_after"] = reflection.after;
	m_PendingShot["reflection_stack_max"] = nullptr;
	m_PendingShot["reflection_stacks_discarded_by_max"] = 0;
	m_PendingShot["reflection_pinball_fizzle_count"] = reflection.pinballFizzleCount;
	m_PendingShot["physics_chain"] = m_LatestShotPhysicsTelemetry;
	m_PendingShot["pierce_attack"] = m_LatestShotPierceAttackTelemetry;
	m_PendingShot["bounce_axis"] = m_LatestShotBounceTelemetry;
	m_LatestShotCombatResult = {
		{ "measurement_scope", "all_recorded_enemy_hp_damage" },
		{ "shot_no", m_PendingShot.value("shot_no", 0) },
		{ "ball_id", m_PendingShot.value("ball_id", std::string()) },
		{ "upgrade_level", m_PendingShot.value("upgrade_level", 0) },
		{ "total_enemy_damage",
			m_PendingShot.value("total_enemy_damage", json(nullptr)) },
		{ "player_enemy_damage",
			m_PendingShot.value("player_enemy_damage", json(nullptr)) },
		{ "enemy_enemy_damage",
			m_PendingShot.value("enemy_enemy_damage", json(nullptr)) },
		{ "player_enemy_hit_count",
			m_PendingShot.value("player_enemy_hit_count", 0) },
		{ "enemy_enemy_hit_count",
			m_PendingShot.value("enemy_enemy_hit_count", 0) },
		{ "enemies_defeated",
			m_PendingShot.value("enemies_defeated", 0) },
		{ "enemies_alive_after",
			m_PendingShot.value("enemies_alive_after", 0) },
		{ "player_hp_after",
			m_PendingShot.value("player_hp_after", 0) },
		{ "decision_log",
			m_PendingShot.value("decision_log", json::object()) },
	};

	(*stage)["shots"].push_back(std::move(m_PendingShot));
	m_PendingShot = json{};
	m_ShotActive = false;
	m_HasCurrentDamageCollision = false;
	m_SavePending = true;

	Save();
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
