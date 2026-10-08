#include "../AnchorStackRules.h"
#include "../CushionChargeRules.h"
#include "../HeavyCollisionRules.h"
#include "../PierceTraceRules.h"

#include <cassert>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Vector3;

int main()
{
	HeavyCollisionRules::State heavy{};
	HeavyCollisionRules::BeginShot(heavy);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 1, 14.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 2, 23.0f, true);
	HeavyCollisionRules::RecordEnemyMovement(heavy, 3, 99.0f, false);
	assert(HeavyCollisionRules::EndShot(heavy) == 6);
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

	std::cout << "ball synergy rules: OK\n";
	return 0;
}
