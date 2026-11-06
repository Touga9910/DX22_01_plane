#include "../AnchorStackRules.h"
#include "../CushionChargeRules.h"
#include "../HeavyCollisionRules.h"
#include "../PierceTraceRules.h"
#include "../PierceAttackTelemetry.h"
#include "../ShotPhysicsTelemetry.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using DirectX::SimpleMath::Vector3;

int main()
{
	PierceAttackTelemetry pierceTelemetry;
	pierceTelemetry.BeginShot();
	pierceTelemetry.RecordMinionSpawn(
		101, 2, 1.0f, 2.0f, 1.0f, 0.0f, 5.0f);
	pierceTelemetry.RecordMinionSpawn(
		102, 1, 3.0f, 4.0f, 0.0f, 1.0f, 5.0f);
	pierceTelemetry.RecordMinionMovement(101, 12.5f);
	pierceTelemetry.RecordAttack(
		PierceAttackSource::PlayerDirect, 0, 1001, 3, 3, true);
	pierceTelemetry.RecordAttack(
		PierceAttackSource::PlayerPierce, 0, 1002, 5, 2, true);
	pierceTelemetry.RecordAttack(
		PierceAttackSource::Minion, 101, 1003, 2, 2, true);
	pierceTelemetry.RecordAttack(
		PierceAttackSource::Minion, 101, 1004, 2, 1, true);
	pierceTelemetry.RecordAttack(
		PierceAttackSource::Minion, 102, 1004, 0, 0, false);
	pierceTelemetry.RecordMinionDestroyed(101, "stopped");
	const auto pierceSnapshot = pierceTelemetry.EndShot();
	assert(pierceSnapshot.total.attackCheckCount == 5);
	assert(pierceSnapshot.total.hitCount == 4);
	assert(pierceSnapshot.total.effectiveAttackCount == 4);
	assert(pierceSnapshot.total.calculatedDamage == 12);
	assert(pierceSnapshot.total.appliedHpDamage == 8);
	assert(pierceSnapshot.total.overkillDamage == 4);
	assert(pierceSnapshot.playerDirect.effectiveAttackCount == 1);
	assert(pierceSnapshot.playerPierce.effectiveAttackCount == 1);
	assert(pierceSnapshot.minion.effectiveAttackCount == 2);
	assert(pierceSnapshot.uniqueEffectiveTargetCount == 4);
	assert(pierceSnapshot.minionsGenerated == 2);
	assert(pierceSnapshot.maximumSimultaneousMinions == 2);
	assert(pierceSnapshot.minionsWithoutEffectiveAttack == 1);
	assert(pierceSnapshot.minionsWithMultipleTargets == 1);
	assert(pierceSnapshot.minions.size() == 2);
	assert(std::abs(
		pierceSnapshot.minions[0].movementDistance - 12.5f) < 0.001f);
	assert(pierceSnapshot.minions[0].destructionReason == "stopped");
	assert(pierceSnapshot.minions[0].effectiveAttackCount == 2);
	assert(pierceSnapshot.minions[0].uniqueTargetCount == 2);
	assert(
		pierceSnapshot.minions[1].destructionReason ==
		"active_at_shot_end");

	ShotPhysicsTelemetry telemetry;
	telemetry.BeginShot({
		{ 101, 0, "enemy_normal", 10, 10, -2.0f, 4.0f },
		{ 202, 1, "enemy_strong", 20, 20, 2.0f, 4.0f },
		{ 303, 2, "enemy_tank", 30, 30, 6.0f, 4.0f },
	});
	telemetry.RecordEnemyMovement(101, "enemy_normal", 3.0f);
	telemetry.RecordEnemyMovement(101, "enemy_normal", 2.0f);
	telemetry.RecordEnemyMovement(202, "enemy_strong", 7.0f);
	telemetry.RecordEnemyEnemyCollision(
		101, "enemy_normal", 202, "enemy_strong", 4.0f, 0.0f, 4.0f);
	telemetry.RecordEnemyEnemyCollision(
		202, "enemy_strong", 101, "enemy_normal", 2.0f, 1.0f, 4.0f);
	telemetry.RecordEnemyEnemyCollision(
		202, "enemy_strong", 303, "enemy_tank", 1.0f, 4.0f, 4.0f);
	telemetry.RecordPhysicsStep(125);
	telemetry.RecordPhysicsStep(75);
	telemetry.RecordWallReflection(
		1, "table", "player",
		4.0f, 8.0f,
		5.0f, 0.0f,
		-4.0f, 0.0f,
		-4.4f, 0.0f,
		3);
	telemetry.RecordWallReflection(
		2, "table", "pinball_target",
		-4.0f, 2.0f,
		-6.0f, 0.0f,
		4.8f, 0.0f,
		5.4f, 0.0f,
		7);
	const auto telemetrySnapshot = telemetry.EndShot();
	assert(telemetrySnapshot.collisions.size() == 3);
	assert(telemetrySnapshot.involvedEnemyCount == 3);
	assert(telemetrySnapshot.pairSummaries.size() == 2);
	assert(telemetrySnapshot.maximumCollisionsForOnePair == 2);
	assert(std::abs(telemetrySnapshot.totalEnemyDistance - 12.0f) < 0.001f);
	assert(std::abs(telemetrySnapshot.maximumEnemyDistance - 7.0f) < 0.001f);
	assert(telemetrySnapshot.enemyDistances.size() == 3);
	assert(std::abs(telemetrySnapshot.enemyDistances[2].distance) < 0.001f);
	assert(telemetrySnapshot.physicsProcessingMicroseconds == 200);
	assert(telemetrySnapshot.physicsStepCount == 2);
	assert(telemetrySnapshot.wallReflections.size() == 2);
	assert(telemetrySnapshot.wallReflections[0].wallIndex == 1);
	assert(telemetrySnapshot.wallReflections[0].timelineEventIndex == 3);
	assert(telemetrySnapshot.wallReflections[0].ballKind == "player");
	assert(std::abs(
		telemetrySnapshot.wallReflections[0].velocityAfterEffectsX +
		4.4f) < 0.001f);
	assert(telemetrySnapshot.wallReflections[1].ballKind == "pinball_target");
	assert(telemetrySnapshot.wallReflections[1].timelineEventIndex == 7);

	HeavyCollisionRules::State heavy{};
	assert(std::abs(
		heavy.distancePerStack -
		HeavyCollisionRules::DefaultDistancePerStack) < 0.001f);
	HeavyCollisionRules::BeginShot(heavy);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 1, 94.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 2, 183.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 3, 99.0f, false);
	const auto initialHeavyGain = HeavyCollisionRules::EndShot(heavy);
	assert(initialHeavyGain.requested == 6 && initialHeavyGain.granted == 6);
	assert(initialHeavyGain.discardedByTurnLimit == 0);
	assert(heavy.stackCount == 6);
	HeavyCollisionRules::BeginShot(heavy);
	assert(HeavyCollisionRules::GenerateOncePerEnemy(heavy, 1));
	assert(!HeavyCollisionRules::GenerateOncePerEnemy(heavy, 1));
	assert(HeavyCollisionRules::GenerateOncePerEnemy(heavy, 2));
	const auto heavyFinish = HeavyCollisionRules::ConsumeForFinisher(heavy, 2.0f, 5);
	assert(heavyFinish.referenced == 8 && heavyFinish.consumed == 5);
	assert(heavyFinish.bonusDamage == 10 && heavy.stackCount == 3);
	HeavyCollisionRules::ArmNextEnemyCollisionMultiplier(heavy, 1.5f);
	HeavyCollisionRules::BeginShot(heavy);
	assert(HeavyCollisionRules::ScaleEnemyCollisionDamage(heavy, 3) == 5);
	HeavyCollisionRules::EndShot(heavy);
	assert(HeavyCollisionRules::ScaleEnemyCollisionDamage(heavy, 3) == 3);

	HeavyCollisionRules::State cappedHeavy{};
	HeavyCollisionRules::SetGainLimitPerTurn(cappedHeavy, 10);
	HeavyCollisionRules::BeginShot(cappedHeavy);
	HeavyCollisionRules::RecordEnemyMovement(
		cappedHeavy, 1, 4520.0f, true);
	const auto standardHeavyGain = HeavyCollisionRules::EndShot(cappedHeavy);
	assert(standardHeavyGain.requested == 113);
	assert(standardHeavyGain.granted == 10);
	assert(standardHeavyGain.discardedByTurnLimit == 103);
	assert(standardHeavyGain.discardedByStorageLimit == 0);
	assert(cappedHeavy.stackCount == 10);

	HeavyCollisionRules::BeginShot(cappedHeavy);
	HeavyCollisionRules::RecordEnemyMovement(
		cappedHeavy, 1, 7960.0f, true);
	const auto plusOneHeavyGain = HeavyCollisionRules::EndShot(cappedHeavy);
	assert(plusOneHeavyGain.requested == 199);
	assert(plusOneHeavyGain.granted == 10);
	assert(cappedHeavy.stackCount == 20);

	HeavyCollisionRules::BeginShot(cappedHeavy);
	HeavyCollisionRules::RecordEnemyMovement(
		cappedHeavy, 1, 5000.0f, true);
	const auto plusTwoHeavyGain = HeavyCollisionRules::EndShot(cappedHeavy);
	assert(plusTwoHeavyGain.requested == 125);
	assert(plusTwoHeavyGain.granted == 10);
	assert(cappedHeavy.stackCount == 30);

	HeavyCollisionRules::BeginShot(cappedHeavy);
	HeavyCollisionRules::RecordEnemyMovement(cappedHeavy, 1, 39.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(cappedHeavy, 2, 40.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(cappedHeavy, 3, 81.0f, true);
	const auto underLimitGain = HeavyCollisionRules::EndShot(cappedHeavy);
	assert(underLimitGain.requested == 3 && underLimitGain.granted == 3);
	assert(underLimitGain.discardedByTurnLimit == 0);

	HeavyCollisionRules::BeginShot(cappedHeavy);
	for (std::uintptr_t enemyId = 1; enemyId <= 4; ++enemyId)
		assert(HeavyCollisionRules::GenerateOncePerEnemy(
			cappedHeavy, enemyId).granted == 1);
	HeavyCollisionRules::RecordEnemyMovement(cappedHeavy, 10, 400.0f, true);
	const auto mixedGain = HeavyCollisionRules::EndShot(cappedHeavy);
	assert(mixedGain.requested == 10 && mixedGain.granted == 6);
	assert(mixedGain.discardedByTurnLimit == 4);

	HeavyCollisionRules::BeginShot(cappedHeavy);
	const int beforeConsumeTest = cappedHeavy.stackCount;
	assert(HeavyCollisionRules::ApplyGain(cappedHeavy, 10).granted == 10);
	assert(HeavyCollisionRules::Consume(cappedHeavy, 5) == 5);
	const auto afterConsumeGain = HeavyCollisionRules::GenerateOncePerEnemy(
		cappedHeavy, 100);
	assert(afterConsumeGain.requested == 1 && afterConsumeGain.granted == 0);
	assert(afterConsumeGain.discardedByTurnLimit == 1);
	assert(cappedHeavy.stackCount == beforeConsumeTest + 5);

	// The current one-ball-per-turn flow always begins the next ball with BeginShot.
	HeavyCollisionRules::BeginShot(cappedHeavy);
	assert(cappedHeavy.gainedThisTurn == 0);
	assert(HeavyCollisionRules::GenerateOncePerEnemy(
		cappedHeavy, 101).granted == 1);

	HeavyCollisionRules::SetDistancePerStack(cappedHeavy, 20.0f);
	HeavyCollisionRules::Reset(cappedHeavy);
	assert(std::abs(cappedHeavy.distancePerStack - 20.0f) < 0.001f);
	HeavyCollisionRules::SetDistancePerStack(cappedHeavy, 0.0f);
	assert(std::abs(
		cappedHeavy.distancePerStack -
		HeavyCollisionRules::DefaultDistancePerStack) < 0.001f);

	HeavyCollisionRules::State zeroLimit{};
	assert(zeroLimit.gainLimitPerTurn == 10);
	HeavyCollisionRules::SetGainLimitPerTurn(zeroLimit, -4);
	assert(zeroLimit.gainLimitPerTurn == 0);
	HeavyCollisionRules::BeginShot(zeroLimit);
	const auto zeroGain = HeavyCollisionRules::ApplyGain(zeroLimit, 4);
	assert(zeroGain.granted == 0 && zeroGain.discardedByTurnLimit == 4);

	HeavyCollisionRules::State storageLimited{};
	storageLimited.stackCount = (std::numeric_limits<int>::max)() - 3;
	HeavyCollisionRules::BeginShot(storageLimited);
	const auto storageGain = HeavyCollisionRules::ApplyGain(storageLimited, 5);
	assert(storageGain.granted == 3);
	assert(storageGain.discardedByTurnLimit == 0);
	assert(storageGain.discardedByStorageLimit == 2);

	AnchorStackRules::State anchor{};
	int enemyA = 3;
	int enemyB = 4;
	auto firstConversion = AnchorStackRules::ConsumeEnemyStacks(
		enemyA, anchor, 0, 99);
	assert(firstConversion.consumed == 3 && firstConversion.playerStacksGenerated == 0);
	assert(enemyA == 0 && anchor.playerStacks == 0 && anchor.conversionProgress == 3);
	auto secondConversion = AnchorStackRules::ConsumeEnemyStacks(
		enemyB, anchor, 0, 99);
	assert(secondConversion.consumed == 4 && secondConversion.playerStacksGenerated == 1);
	assert(enemyB == 0 && anchor.playerStacks == 1 && anchor.conversionProgress == 2);
	assert(AnchorStackRules::CalculateEnemyGrant(2, 6, 4, 0) == 3);
	assert(AnchorStackRules::CalculateEnemyGrant(2, 6, 3, 1) == 5);
	assert(AnchorStackRules::CalculateShield(1, 7) == 1);
	assert(AnchorStackRules::CalculateShield(2, 7) == 3);
	assert(AnchorStackRules::CalculateShield(3, 7) == 4);
	assert(AnchorStackRules::CalculateShield(4, 7) == 6);
	assert(AnchorStackRules::CalculateShield(5, 7) == 7);
	assert(AnchorStackRules::FinisherDamagePerEnemyStack(4) == 2);
	assert(AnchorStackRules::FinisherDamagePerEnemyStack(5) == 3);
	assert(AnchorStackRules::FinisherDamagePerEnemyStack(10) == 4);
	assert(AnchorStackRules::DefeatAutoConsumeAmount(11) == 5);
	Vector3 returnPosition;
	AnchorStackRules::SaveReturnPosition(anchor, { 4.0f, 1.0f, 2.0f });
	assert(AnchorStackRules::ConsumeReturnPosition(anchor, returnPosition));
	assert(returnPosition == Vector3(4.0f, 1.0f, 2.0f));
	assert(!AnchorStackRules::ConsumeReturnPosition(anchor, returnPosition));

	PierceTraceRules::State traces{};
	PierceTraceRules::BeginShot(traces);
	assert(PierceTraceRules::RecordPierce(traces, 1));
	assert(!PierceTraceRules::RecordPierce(traces, 1));
	for (std::uintptr_t id = 2; id <= 20; ++id)
		PierceTraceRules::RecordPierce(traces, id);
	assert(traces.stackCount == PierceTraceRules::MaxStackCount);
	assert(PierceTraceRules::ConsumeStacks(traces, 9) == 9);
	assert(traces.stackCount == 6);
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 0 }, { 20, 0, 0 }));
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 5 }, { 20, 0, 5 }));
	const auto oldest = traces.traces.front().id;
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 10 }, { 20, 0, 10 }));
	assert(PierceTraceRules::AddTrace(traces, { 0, 0, 15 }, { 20, 0, 15 }));
	assert(traces.traces.size() == 3 && traces.overwrittenCount == 1);
	assert(traces.traces.front().id != oldest);
	const auto fiveStackMinions = PierceTraceRules::BuildMinionLaunches(
		traces, 5, false);
	assert(fiveStackMinions.size() == 2);
	assert(fiveStackMinions[0].attack == 2 &&
		std::abs(fiveStackMinions[0].knockbackMultiplier - 1.5f) < 0.001f);
	assert(fiveStackMinions[1].attack == 2 &&
		std::abs(fiveStackMinions[1].knockbackMultiplier - 1.0f) < 0.001f);
	const auto finisherMinions = PierceTraceRules::BuildMinionLaunches(
		traces, 15, true);
	assert(finisherMinions.size() == 3);
	for (const auto& minion : finisherMinions)
	{
		assert(minion.attack == 3);
		assert(std::abs(minion.knockbackMultiplier - 1.875f) < 0.001f);
	}
	assert(PierceTraceRules::MinionLaunchSpeed == 5.0f);

	CushionChargeRules::State cushions{};
	CushionChargeRules::BeginPlayerShot(cushions);
	CushionChargeRules::ReflectionConfig charger;
	charger.generatesStacks = true;
	charger.generationLimit = 3;
	charger.stacksPerReflection = 1;
	Vector3 velocity(4.0f, 0.0f, 3.0f);
	for (int index = 0; index < 5; ++index)
		CushionChargeRules::ApplyValidReflection(cushions, charger, velocity);
	assert(cushions.stackCount == 3);
	CushionChargeRules::EndPlayerShot(cushions);
	CushionChargeRules::ArmSetupForNextShot(cushions, 2);
	CushionChargeRules::BeginPlayerShot(cushions);
	CushionChargeRules::ReflectionConfig bounce;
	bounce.consumesStacks = true;
	bounce.consumptionLimit = 2;
	bounce.reflectedSpeedMultiplier = 1.2f;
	const auto firstBounce = CushionChargeRules::ApplyValidReflection(
		cushions, bounce, velocity);
	const auto secondBounce = CushionChargeRules::ApplyValidReflection(
		cushions, bounce, velocity);
	const auto thirdBounce = CushionChargeRules::ApplyValidReflection(
		cushions, bounce, velocity);
	assert(firstBounce.consumed == 1 && firstBounce.setupTriggered);
	assert(secondBounce.consumed == 1 && thirdBounce.consumed == 0);
	assert(cushions.stackCount == 3 && cushions.empoweredHitsRemaining == 2);
	assert(CushionChargeRules::ConsumeEmpoweredHit(cushions, 1));
	assert(!CushionChargeRules::ConsumeEmpoweredHit(cushions, 1));
	assert(CushionChargeRules::ConsumeEmpoweredHit(cushions, 2));
	assert(!CushionChargeRules::ConsumeEmpoweredHit(cushions, 3));
	assert(std::abs(velocity.Length() - 7.2f) < 0.001f);
	CushionChargeRules::EndPlayerShot(cushions);
	CushionChargeRules::BeginPlayerShot(cushions);
	CushionChargeRules::ReflectionConfig unlimitedCharger;
	unlimitedCharger.generatesStacks = true;
	unlimitedCharger.generationLimit = 25;
	unlimitedCharger.stacksPerReflection = 2;
	for (int index = 0; index < 25; ++index)
		CushionChargeRules::ApplyValidReflection(
			cushions, unlimitedCharger, velocity);
	assert(cushions.stackCount == 53);
	assert(cushions.validReflectionsThisShot == 25);
	assert(cushions.generatedThisShot == 25);

	CushionChargeRules::State pinball{};
	pinball.stackCount = 5;
	assert(!CushionChargeRules::ActivatePinball(
		pinball, 42, 6, 3, 0.9f));
	assert(pinball.stackCount == 5 && pinball.pinballTargetId == 0);
	pinball.stackCount = 6;
	assert(CushionChargeRules::ActivatePinball(
		pinball, 42, 6, 3, 0.9f));
	assert(pinball.stackCount == 0);
	assert(CushionChargeRules::RegisterPinballCollision(pinball, 42) == 0);
	assert(CushionChargeRules::RegisterPinballCollision(pinball, 42) == 0);
	assert(CushionChargeRules::RegisterPinballCollision(pinball, 42) == 1);
	CushionChargeRules::State plusTwoPinball{};
	plusTwoPinball.stackCount = 6;
	assert(CushionChargeRules::ActivatePinball(
		plusTwoPinball, 84, 6, 2, 0.9f));
	assert(CushionChargeRules::RegisterPinballCollision(
		plusTwoPinball, 84) == 0);
	assert(CushionChargeRules::RegisterPinballCollision(
		plusTwoPinball, 84) == 1);
	assert(CushionChargeRules::RegisterPinballCollision(
		plusTwoPinball, 84) == 1);
	assert(CushionChargeRules::RegisterPinballCollision(
		plusTwoPinball, 84) == 2);
	Vector3 slowAfter(2.0f, 0.0f, 0.0f);
	CushionChargeRules::PreservePinballSpeed(
		pinball, 42, Vector3(10.0f, 0.0f, 0.0f), slowAfter);
	assert(std::abs(slowAfter.Length() - 9.0f) < 0.001f);
	Vector3 fastAfter(12.0f, 0.0f, 0.0f);
	CushionChargeRules::PreservePinballSpeed(
		pinball, 42, Vector3(10.0f, 0.0f, 0.0f), fastAfter);
	assert(std::abs(fastAfter.Length() - 10.0f) < 0.001f);

	std::cout << "ball synergy rules: OK\n";
	return 0;
}
