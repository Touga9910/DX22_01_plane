#pragma once

#include "PierceAttackTelemetry.h"
#include "ShotPhysicsTelemetry.h"
#include "json/json_fwd.hpp"

// Internal JSON projections shared by shot finalization and stage/run results.
namespace BalanceLogDetail
{

	double RoundToTwoDecimals(double value);

	nlohmann::json MakeShotPhysicsTelemetryJson(
		const ShotPhysicsTelemetry::Snapshot& telemetry);

	nlohmann::json MakeBounceTelemetryJson(
		const ShotPhysicsTelemetry::Snapshot& physics,
		const PierceAttackTelemetrySnapshot& attacks,
		const nlohmann::json& attackTimeline,
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
		long long shotDurationMs);

	nlohmann::json MakePierceAttackTelemetryJson(
		const PierceAttackTelemetrySnapshot& telemetry);
}
