#pragma once

#include "json/json.hpp"

// Internal values derived from one completed shot. These helpers observe logged
// facts and format results; they do not mutate gameplay or own logger state.
namespace BalanceLogDetail
{
	struct ShotEventSummary
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
		nlohmann::json actualNextTurnPreventionByTarget = nlohmann::json::object();
		nlohmann::json actualFutureChargePreventionByTarget = nlohmann::json::object();
		int heavyStacksConsumed = 0;
		int actualNextTurnPrevention = 0;
		int actualFutureChargePrevention = 0;
	};

	struct ShotCompletionFacts
	{
		bool playerEnemyDamageKnown = false;
		bool enemyEnemyDamageKnown = false;
		int playerEnemyDamage = 0;
		int enemyEnemyDamage = 0;
		int enemyEnemyHitCount = 0;
		int enemiesAlive = 0;
		int enemiesDefeatedTotal = 0;
		int defeatedAtShotStart = 0;
	};

	struct ReflectionStackSummary
	{
		int before = 0;
		int after = 0;
		int pinballFizzleCount = 0;
	};

	ShotEventSummary SummarizeShotEvents(const nlohmann::json& pendingShot);
	void AppendShotDecisionLog(
		nlohmann::json& pendingShot,
		const ShotEventSummary& summary,
		const ShotCompletionFacts& facts);
	ReflectionStackSummary AppendShotStackTelemetry(
		nlohmann::json& pendingShot,
		nlohmann::json& latestPhysics,
		nlohmann::json& latestPierce,
		const ShotEventSummary& summary,
		long long durationMs);
}
